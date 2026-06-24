#version 460 core

layout(location = 0) out vec4 gAlbedo;    // RGB: albedo, A: metallic
layout(location = 1) out vec4 gNormal;    // RGB: world normal, A: roughness
layout(location = 2) out vec4 gMaterial;  // R: emissive, G: texture flags, B: occlusion, A: alpha

uniform sampler2D u_textures[32];

in vec3 vert_normal;
in mat3 vert_TBN;
in vec3 vert_pos;
in vec2 vert_uv;
in vec4 vert_color;
flat in int vert_colorTextureUnit;
flat in int vert_normalTextureUnit;
in float vert_occlusionFactor;
flat in int vert_materialTextureUnit;
flat in float vert_emissiveScalar;

vec3 rgb2hsv(vec3 rgb) {
    float maxVal = max(max(rgb.r, rgb.g), rgb.b);
    float minVal = min(min(rgb.r, rgb.g), rgb.b);
    float delta = maxVal - minVal;
    
    vec3 hsv;
    hsv.z = maxVal;
    
    if (maxVal > 0.0) {
        hsv.y = delta / maxVal;
    } else {
        hsv.y = 0.0;
    }
    
    if (delta == 0.0) {
        hsv.x = 0.0;
    } else if (maxVal == rgb.r) {
        hsv.x = 60.0 * mod((rgb.g - rgb.b) / delta, 6.0);
    } else if (maxVal == rgb.g) {
        hsv.x = 60.0 * (2.0 + (rgb.b - rgb.r) / delta);
    } else {
        hsv.x = 60.0 * (4.0 + (rgb.r - rgb.g) / delta);
    }
    
    hsv.x /= 360.0;
    return hsv;
}

vec3 hsv2rgb(vec3 hsv) {
    float h = hsv.x * 360.0;
    float s = hsv.y;
    float v = hsv.z;
    
    float c = v * s;
    float x = c * (1.0 - abs(mod(h / 60.0, 2.0) - 1.0));
    float m = v - c;
    
    vec3 rgb;
    if (h < 60.0) rgb = vec3(c, x, 0.0);
    else if (h < 120.0) rgb = vec3(x, c, 0.0);
    else if (h < 180.0) rgb = vec3(0.0, c, x);
    else if (h < 240.0) rgb = vec3(0.0, x, c);
    else if (h < 300.0) rgb = vec3(x, 0.0, c);
    else rgb = vec3(c, 0.0, x);
    
    return rgb + m;
}

vec4 applyHSVTransform(vec4 vertexColor, vec4 textureColor) {
    vec3 vertHSV = rgb2hsv(vertexColor.rgb);
    vec3 texHSV = rgb2hsv(textureColor.rgb);
    
    vec3 transformedHSV;
    transformedHSV.x = mod(vertHSV.x + texHSV.x, 1.0);
    transformedHSV.y = vertHSV.y * texHSV.y;
    transformedHSV.z = vertHSV.z * texHSV.z;
    float alpha = vertexColor.a * textureColor.a;

    return vec4(hsv2rgb(transformedHSV), alpha);
}

void main() {
   // Get normal from normal map or use vertex normal
   vec3 normal;
   if (vert_normalTextureUnit == -1) {
      normal = normalize(vert_normal);
   } else {
      vec3 normalMap = texture(u_textures[vert_normalTextureUnit], vert_uv).rgb;
      normalMap = normalMap * 2.0 - 1.0;
      normal = normalize(vert_TBN * normalMap);
   }

   // Get object color from texture or use vertex color
   vec3 objectColor;
   float alpha = 1.0;
   if (vert_colorTextureUnit >= 0) {
      vec4 textureColor = texture(u_textures[vert_colorTextureUnit], vert_uv);
      vec4 result = applyHSVTransform(vert_color, textureColor);
      objectColor = result.rgb;
      alpha = result.a;
   } else {
      objectColor = vert_color.rgb;
      alpha = vert_color.a;
   }

   if(alpha < 1./255.) discard;

   // Calculate emissive strength
   float emissiveStrength;
   if (vert_emissiveScalar < 0.001) {
      emissiveStrength = 0.0;
   } else if (vert_materialTextureUnit == -1) {
      emissiveStrength = vert_emissiveScalar;
   } else {
      float textureValue = texture(u_textures[vert_materialTextureUnit], vert_uv).r;
      emissiveStrength = textureValue * vert_emissiveScalar;
   }

   // Output to G-buffer
   gAlbedo = vec4(objectColor, 0.0);  // A: metallic factor (hardcoded to 0 for now)
   gNormal = vec4(normal * 0.5 + 0.5, 0.5);  // Encode normal to [0,1], A: roughness
   gMaterial = vec4(emissiveStrength, 1.0, vert_occlusionFactor, alpha);  // R: emissive, G: geometry flag, B: occlusion, A: alpha
}