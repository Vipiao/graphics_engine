#version 460 core

out vec4 FragColor;

uniform vec3 u_cameraPositionHigh;
uniform vec3 u_cameraPositionLow;
uniform sampler2D u_textures[32];
uniform uint u_time;
uniform float u_timeRemainder;
uniform vec3 u_lightDir;

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

// Convert RGB to HSV
vec3 rgb2hsv(vec3 rgb) {
    float maxVal = max(max(rgb.r, rgb.g), rgb.b);
    float minVal = min(min(rgb.r, rgb.g), rgb.b);
    float delta = maxVal - minVal;
    
    vec3 hsv;
    hsv.z = maxVal; // V (Value)
    
    if (maxVal > 0.0) {
        hsv.y = delta / maxVal; // S (Saturation)
    } else {
        hsv.y = 0.0;
    }
    
    if (delta == 0.0) {
        hsv.x = 0.0; // H (Hue) undefined for gray
    } else if (maxVal == rgb.r) {
        hsv.x = 60.0 * mod((rgb.g - rgb.b) / delta, 6.0);
    } else if (maxVal == rgb.g) {
        hsv.x = 60.0 * (2.0 + (rgb.b - rgb.r) / delta);
    } else {
        hsv.x = 60.0 * (4.0 + (rgb.r - rgb.g) / delta);
    }
    
    hsv.x /= 360.0; // Normalize to [0,1]
    return hsv;
}

// Convert HSV to RGB
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

// Apply HSV transform: vertex color transforms texture color
vec4 applyHSVTransform(vec4 vertexColor, vec4 textureColor) {
    vec3 vertHSV = rgb2hsv(vertexColor.rgb);
    vec3 texHSV = rgb2hsv(textureColor.rgb);
    
    vec3 transformedHSV;
    transformedHSV.x = mod(vertHSV.x + texHSV.x, 1.0); // H: additive with wrap
    transformedHSV.y = vertHSV.y * texHSV.y;           // S: multiplicative  
    transformedHSV.z = vertHSV.z * texHSV.z;           // V: multiplicative
    float alpha = vertexColor.a * textureColor.a;      // A: multiplicative

    return vec4(hsv2rgb(transformedHSV), alpha);
}

void main() {
   // Get normal from normal map or use vertex normal
   vec3 normal;
   if (vert_normalTextureUnit == -1) {
      normal = normalize(vert_normal);
   } else {
      // Sample normal map and transform to world space using TBN matrix
      vec3 normalMap = texture(u_textures[vert_normalTextureUnit], vert_uv).rgb;
      normalMap = normalMap * 2.0 - 1.0;  // Convert from [0,1] to [-1,1] range
      normal = normalize(vert_TBN * normalMap);  // Transform from tangent to world space
   }

   // Get object color from texture or use white as default
   vec3 objectColor;
   float alpha = 1.0;
   if (vert_colorTextureUnit >= 0) {
      vec4 textureColor = texture(u_textures[vert_colorTextureUnit], vert_uv);

      // Apply HSV transform: vertex color transforms texture color
      
      vec4 result = applyHSVTransform(vert_color, textureColor);
      objectColor = result.rgb;
      alpha = result.a;
   } else {
      // No texture: use vertex color directly
      objectColor = vert_color.rgb;
      alpha = vert_color.a;
   }

   // Continue with lighting calculations only if not fully emissive
   if(alpha < 1./255.) discard;
   
   // Calculate emissive strength based on scalar and texture
   float emissiveStrength;
   if (vert_emissiveScalar < 0.001) {
      emissiveStrength = 0.0;
   } else if (vert_materialTextureUnit == -1) {
      // No texture: use scalar directly
      emissiveStrength = vert_emissiveScalar;
   } else {
      // Has texture: multiply texture by scalar
      float textureValue = texture(u_textures[vert_materialTextureUnit], vert_uv).r;
      emissiveStrength = textureValue * vert_emissiveScalar;
   }

   // Early exit for full emissive materials
   if (emissiveStrength >= 0.999) {
      FragColor = vec4(objectColor, alpha);
      return;
   }

   // Calculate light and view directions (directional light)
   vec3 lightDir = normalize(-u_lightDir);
   vec3 viewDir = normalize(-vert_pos); // Camera is at origin in L-space
   
   // No distance attenuation for directional light
   float attenuation = 1.0;

   // Phong lighting model components
   
   // 1. Ambient light - base illumination
   float ambientStrength = 0.3;
   vec3 ambient = ambientStrength * objectColor;
   
   // 2. Diffuse light - varies with surface orientation to light
   float diff = max(dot(normal, lightDir), 0.0);
   vec3 diffuse = diff * objectColor;
   
   // 3. Specular light - reflective highlights
   float specularStrength = 0.5;
   vec3 reflectDir = reflect(-lightDir, normal);
   float spec = pow(max(min(dot(viewDir, reflectDir) + 0.001, 1.), 0.0), 128.0);
   vec3 specular = specularStrength * spec * vec3(1.0);
   
   // Apply occlusion factor to all lighting components
   // 0.0 = fully occluded, 1.0 = no occlusion
   ambient *= vert_occlusionFactor;
   diffuse *= vert_occlusionFactor;
   specular *= vert_occlusionFactor;
   
   // Combine all lighting components
   vec3 result = ambient + (diffuse + specular) * attenuation;

   // Blend between lit and emissive based on material texture
   result = mix(result, objectColor, emissiveStrength);
   
   FragColor = vec4(result, alpha);
}