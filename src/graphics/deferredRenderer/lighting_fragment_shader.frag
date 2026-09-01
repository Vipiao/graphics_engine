#version 460 core

#include "../shared_shaders/phong_lighting.glsl"

out vec4 FragColor;

uniform sampler2D gAlbedo;
uniform sampler2D gNormal;
uniform sampler2D gMaterial;
uniform sampler2D gDepth;
uniform sampler2DArrayShadow u_shadowMap;
uniform int u_numCascades;
uniform mat4 u_lightSpaceMatrices[4];
uniform float u_cascadeBiasScales[4];
uniform float u_cascadeOrthoSizes[4];
uniform float u_cascadePush;
uniform bool u_shadowsEnabled;
uniform vec3 u_lightDir;
uniform mat4 u_projection;
uniform mat4 u_inverseProjection;
uniform vec2 u_screenSize;
uniform bool u_ssaoEnabled;
uniform float u_ssaoRadius;
uniform float u_ssaoBias;
uniform vec3 u_ssaoSamples[32];
// Tileable blue noise threshold map, shared with the post-processing pass.
uniform sampler2D u_blueNoise;
// Per-frame toroidal shift of the noise tile (R2 sequence, computed CPU side).
uniform ivec2 u_blueNoiseOffset;

in vec2 texCoord;

vec3 debugColor = vec3(0.);

// Tiled blue noise in [0, 1). shift picks a decorrelated stream by reading a
// distant texel of the same tile; u_blueNoiseOffset animates it per frame.
float blueNoise(ivec2 shift) {
   ivec2 noiseSize = textureSize(u_blueNoise, 0);
   ivec2 coord = (ivec2(gl_FragCoord.xy) + u_blueNoiseOffset + shift) % noiseSize;
   return texelFetch(u_blueNoise, coord, 0).r;
}

// Blue noise jitter in [-1, 1].
float blueNoiseJitter(ivec2 shift) {
   return blueNoise(shift) * 2.0 - 1.0;
}

vec3 reconstructPosition(vec2 screenCoord, float depth) {
   // Convert screen coordinates to NDC
   vec2 ndc = screenCoord * 2.0 - 1.0;
   
   // Clip space is mapped zero-to-one, so the sampled depth is already the z it
   // wants; only xy has to come across from texture coordinates.
   vec4 clipSpace = vec4(ndc, depth, 1.0);
   
   // Transform to view space
   vec4 viewSpace = u_inverseProjection * clipSpace;
   return viewSpace.xyz / viewSpace.w;
}

float reconstructPositionZ(vec2 screenCoord, float depth) {
   // Convert screen coordinates to NDC
   vec2 ndc = screenCoord * 2.0 - 1.0;
   
   // Clip space is mapped zero-to-one, so the sampled depth is already the z it
   // wants; only xy has to come across from texture coordinates.
   vec4 clipSpace = vec4(ndc, depth, 1.0);
   
   // Extract Z and W rows from the matrix and compute only what we need
   vec4 zRow = vec4(u_inverseProjection[0].z, u_inverseProjection[1].z, u_inverseProjection[2].z, u_inverseProjection[3].z);
   vec4 wRow = vec4(u_inverseProjection[0].w, u_inverseProjection[1].w, u_inverseProjection[2].w, u_inverseProjection[3].w);
   float viewSpaceZ = dot(zRow, clipSpace);
   float viewSpaceW = dot(wRow, clipSpace);
   return viewSpaceZ / viewSpaceW;
}

float calculateSSAO(vec3 fragPos, vec3 normal) {
   if (!u_ssaoEnabled) {
      return 1.0;
   }

   // Scale radius based on distance from camera to maintain consistent world-space coverage
   // Objects further away need larger view-space radius to maintain same world-space effect
   float distanceFromCamera = length(fragPos);
   float scaledRadius = u_ssaoRadius * (1.0 + distanceFromCamera * 0.01);
   
   // Rotate the sample kernel per pixel with a blue noise angle: neighboring
   // pixels get maximally different rotations, so undersampling shows up as
   // fine grain instead of banded artifacts.
   float angle = blueNoise(ivec2(0, 0)) * 6.283185307179586;
   vec3 randomVec = vec3(cos(angle), sin(angle), 0.0);
   
   // Create TBN matrix
   vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
   vec3 bitangent = cross(normal, tangent);
   mat3 TBN = mat3(tangent, bitangent, normal);
   
   float occlusion = 0.0;
   int sampleCount = 32; // Match the uniform array size
   
   //float weight = 0.;
   //int count = 0;
   for (int i = 0; i < sampleCount; ++i) {
      // Get sample position in world space
      vec3 samplePos = TBN * u_ssaoSamples[i];
      samplePos = fragPos + samplePos * scaledRadius;
      
      // Project to screen space
      vec4 offset = u_projection * vec4(samplePos, 1.0);
      offset.xyz /= offset.w;
      offset.xyz = offset.xyz * 0.5 + 0.5;
      
      // Check if sample is within screen bounds
      if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) {
         continue;
      }
      
      // Sample depth at this position
      //vec3 reconstructedPos = reconstructPosition(offset.xy, texture(gDepth, offset.xy).r);
      //float sampleDepth = reconstructedPos.z;
      float sampleDepth = reconstructPositionZ(offset.xy, texture(gDepth, offset.xy).r);
      
      // Range check to reduce artifacts
      // Use scaled radius for consistent range checking at all distances
      float depthDifference = abs(fragPos.z - sampleDepth);
      float rangeThreshold = scaledRadius * 2.0;  // Samples beyond 2x radius are ignored
      float rangeCheck = smoothstep(rangeThreshold, rangeThreshold * 0.5, depthDifference);
      //weight += rangeCheck;

      // Compare depths
      //occlusion += (sampleDepth >= samplePos.z + u_ssaoBias ? 1.0 : 0.0) * rangeCheck;
      float depthDiff = sampleDepth - samplePos.z;
      float occlusionContribution = smoothstep(-u_ssaoBias, u_ssaoBias, depthDiff);
      occlusion += occlusionContribution * rangeCheck;
      //if(occlusionContribution > 0.5) count++;
   }
   
   occlusion = 1.0 - (occlusion / float(sampleCount));
   //occlusion = 1.0 - (occlusion / weight);
   //if(count > 21) return u_timeRemainder*4.;
   
   return occlusion;
}

// Fraction of a cascade's reach given over to the fade into the next cascade
// out. Radial, so a roll of the light basis slides no part of it.
const float k_cascadeFadeBand = 0.20;

// Cascade to sample, and how far the fragment sits into that cascade's fade
// band: 0 well inside it, rising to 1 at the cascade's edge.
struct CascadeChoice {
    int index;
    float fade;
};

// Finest cascade whose sphere still holds the fragment. The sphere of a cascade's
// ortho size is inscribed in its box: inside that sphere is inside the box however
// the box is rolled about the light axis, and the boundary is round, so the roll
// never moves it. Both are pushed along the view direction by the same amount, so
// the fragment has to be measured from the centre the projection was built around.
CascadeChoice selectCascade(vec3 fragPos) {
    for (int i = 0; i < u_numCascades; ++i) {
        float size = u_cascadeOrthoSizes[i];
        // View space looks down -z, so that is the direction the centres go.
        vec3 centre = vec3(0.0, 0.0, -u_cascadePush * size);
        float radius = length(fragPos - centre) / size;
        if (radius < 1.0) {
            return CascadeChoice(i, smoothstep(1.0 - k_cascadeFadeBand, 1.0, radius));
        }
    }

    // Outside all cascades
    return CascadeChoice(-1, 0.0);
}

// Shadow-map texels the lookup slides along the surface normal. Wide enough to
// carry the 3x3 PCF footprint clear of the surface it is standing on.
const float k_normalOffsetTexels = 2.0;

// What the normal offset leaves behind, in half-texels of depth. Reverse-Z keeps
// quantisation far below this, so it only has to cover the residue.
const float k_depthBiasTexels = 2.0;

float calculateShadow(
   vec3 fragPos, vec3 normal, vec3 lightDir, int cascadeIndex
) {
    if (!u_shadowsEnabled) {
        return 1.0; // No shadow
    }

    if (cascadeIndex < 0) {
        return 1.0; // Outside all cascades, no shadow
    }

    vec2 texelSize = 1.0 / vec2(textureSize(u_shadowMap, 0).xy);
    float texelWorld = 2.0 * u_cascadeOrthoSizes[cascadeIndex] * texelSize.x;

    // Slide the lookup along the surface instead of pushing its depth away from
    // the light: none when the surface faces the light, all of it when the surface
    // runs along it, which is what the sine measures. Bounded by a texel however
    // the surface leans, where a depth push would grow as 1/tan and tear the
    // shadow off the contact it belongs to.
    float ndotl = clamp(dot(normal, lightDir), 0.0, 1.0);
    float slide = sqrt(1.0 - ndotl * ndotl);
    vec3 samplePos = fragPos + normal * (k_normalOffsetTexels * texelWorld * slide);

    vec4 fragPosLightSpace = u_lightSpaceMatrices[cascadeIndex] * vec4(samplePos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;

    // Clip space is mapped zero-to-one for this pass, so only xy still needs the
    // half-scale into texture space; z already reads as the depth the map holds.
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // Check if fragment is outside shadow map bounds
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0; // Outside shadow map, assume no shadow
    }

    float currentDepth = projCoords.z;
    float normalizedBias = k_depthBiasTexels * u_cascadeBiasScales[cascadeIndex];

    // Blue noise jitter (animated per frame through the noise offset) turns
    // the PCF grid into a soft penumbra without visible noise clumps.
    vec2 jitter = vec2(blueNoiseJitter(ivec2(29, 7)), blueNoiseJitter(ivec2(47, 59)));
    vec2 jitteredOffset = jitter * texelSize * 0.5;

    // The reference the sampler compares each texel against, biased once here
    // rather than per tap. A cleared texel sits at zero and the reference never
    // falls below it, so an untouched map reads as lit without a test for it.
    float reference = currentDepth + normalizedBias;

    // PCF: every tap is a hardware 2x2 comparison, so the nine below cover a 4x4
    // neighbourhood and come back already resolved to a lit fraction.
    float shadow = 0.0;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            vec2 sampleOffset = vec2(x, y) * texelSize + jitteredOffset;
            shadow += texture(u_shadowMap,
                              vec4(projCoords.xy + sampleOffset, float(cascadeIndex),
                                   reference));
        }
    }
    shadow /= 9.0; // Average the 9 samples

    return shadow;
}

vec4 calculateSSR(vec3 fragPos, vec3 normal, vec3 viewDir, vec3 lightDir) {
    // Calculate reflection direction
    vec3 reflectionDir = reflect(-viewDir, normal);
    
    // Hardcode scale as 5.0
    float scale = 10.0 * (1.-dot(normal, reflectionDir));
    //float scale = 10.0;
    
    // Choose number of steps
    int numSteps = 32;
    
    // March linearly in view space along the reflection ray
    vec3 stepSize = (reflectionDir * scale) / float(numSteps);
    
    // Jitter: offset the ray start with blue noise so undersampling along the
    // march dissolves into fine grain instead of stair-step bands.
    float jitter = blueNoiseJitter(ivec2(17, 31));
    vec3 currentPos = fragPos + stepSize * jitter;
    for (int i = 1; i <= numSteps; i++) {
        // Get current position in view space
        //float stepSizeScale = abs(pow(abs(dot(viewDir, reflectionDir)), 4.));
        currentPos += stepSize;
        
        // Check if ray goes behind the camera (positive Z in view space)
        if (currentPos.z > 0.0) {
            break;
        }
        
        // Project current position to screen space
        vec4 screenPos = u_projection * vec4(currentPos, 1.0);
        screenPos.xyz /= screenPos.w;
        
        // Convert to [0,1] range for sampling
        vec2 screenUV = screenPos.xy * 0.5 + 0.5;
        float projectedDepth = screenPos.z * 0.5 + 0.5;
        
        //// Check bounds
        //if (screenUV.x < 0.0 || screenUV.x > 1.0 || 
        //    screenUV.y < 0.0 || screenUV.y > 1.0) {
        //    break;
        //}

        // Calculate screen edge fade factor
        float ff = smoothstep(0.0, 0.1, screenUV.x) *
                   (1.0 - smoothstep(0.9, 1.0, screenUV.x)) *
                   smoothstep(0.0, 0.1, screenUV.y) *
                   (1.0 - smoothstep(0.9, 1.0, screenUV.y));
        
        // Calculate fresnel factor
        float fresnelFactor = 1.0 - pow(max(dot(viewDir, normal), 0.0), 1.);
        ff *= mix(fresnelFactor, 1., 0.5);
        
        // Skip if fade factor is too low
        if (ff < 0.01) {
            continue;
        }
        
        // Sample depth at current screen position
        float sampledDepth = texture(gDepth, screenUV).r;

        // Reconstruct world position of the sampled surface
        vec3 surfacePos = reconstructPosition(screenUV, sampledDepth);
        
        // Calculate actual view space penetration depth  
        float penetrationDepth = surfacePos.z - currentPos.z; // Both in view space
        float maxThickness = 1.6*scale/float(numSteps); // World space units - much easier to tune!
         
        // If ray has penetrated past surface but within reasonable thickness
        // (penetrationDepth > 0 means currentPos has gone past surfacePos)
        //float thicknessScale = 1./abs(pow(abs(dot(viewDir, reflectionDir)), 4.));
        
        if (penetrationDepth > 0.0 && penetrationDepth < maxThickness) {
            // We've hit a surface, get the color
            vec3 surfaceNormal = normalize(texture(gNormal, screenUV).rgb * 2.0 - 1.0);
            //if (dot(surfaceNormal, reflectionDir) >= 0.) return vec4(0.);
            if (abs(dot(surfaceNormal, normal)) > 0.99) return vec4(0.);
            vec3 reflectedColor = texture(gAlbedo, screenUV).rgb * ff;
            
            // Add distance fade - closer hits have more weight
            float distanceFade = float(i) / float(numSteps);
            distanceFade = 1.0 - distanceFade;
            float finalWeight = ff * distanceFade;

            // Diffuse shading
            float diff = max(dot(surfaceNormal, lightDir), 0.0);
            reflectedColor *= mix(diff, 1., 0.5);
            
            return vec4(reflectedColor, finalWeight);
        }
    }
    
    // No hit found
    return vec4(0.0, 0.0, 0.0, 0.0);
}

void main() {
   // Sample G-buffer
   vec4 albedoSample = texture(gAlbedo, texCoord);
   vec4 normalSample = texture(gNormal, texCoord);
   vec4 materialSample = texture(gMaterial, texCoord);
   float depth = texture(gDepth, texCoord).r;

   vec3 albedo = albedoSample.rgb;
   float metallic = albedoSample.a;
   vec3 normal = normalize(normalSample.rgb * 2.0 - 1.0);  // Decode normal from [0,1] to [-1,1]
   float roughness = normalSample.a;
   vec3 fragPos = reconstructPosition(texCoord, depth);
   float emissiveStrength = materialSample.r;
   float geometryFlag = materialSample.g;
   float alpha = materialSample.a;

   // Detect background pixels and discard them to preserve sky background
   if (geometryFlag < 0.5) {
       discard;
   }
   
   // Early exit for full emissive materials
   if (emissiveStrength >= 0.999) {
      FragColor = vec4(albedo, alpha);
      return;
   }

   // Calculate SSAO
   float ssaoFactor = calculateSSAO(fragPos, normal);
   ssaoFactor = pow(ssaoFactor, 1.0);
   //ssaoFactor = 1.;
   
   // For directional light, use the light direction directly
   vec3 lightDir = normalize(-u_lightDir);
   vec3 viewDir = normalize(-fragPos); // Camera is at origin in view space
   
   // No distance attenuation for directional light
   float attenuation = 1.0;
   
   // Calculate shadow factor
   CascadeChoice cascade = selectCascade(fragPos);
   float shadowFactor = calculateShadow(fragPos, normal, lightDir, cascade.index);
   if (cascade.fade > 0.0) {
      // Toward the rim, cross-fade into the next cascade out. Past the last one
      // there is nothing coarser left to reach for, so fade to unshadowed.
      float outerShadow = cascade.index + 1 < u_numCascades
         ? calculateShadow(fragPos, normal, lightDir, cascade.index + 1)
         : 1.0;
      shadowFactor = mix(shadowFactor, outerShadow, cascade.fade);
   }
   //shadowFactor = 1.;

   // Phong lighting model: SSAO darkens the ambient term fully and the
   // direct terms slightly; shadowing only affects the direct terms.
   float ff = mix(1.0, ssaoFactor, 0.2);
   vec3 result = phongLighting(albedo, roughness, metallic, normal, lightDir, viewDir,
      ssaoFactor, ff * attenuation * shadowFactor);
   result = mix(result, albedo, emissiveStrength);
   
   // Add screen space reflections with Fresnel
   //float fresnelFactor = pow(max(dot(viewDir, normal), 0.0), 1.);
   vec4 reflectionResult = calculateSSR(fragPos, normal, viewDir, lightDir);
   vec3 reflectedColor = reflectionResult.rgb;
   float reflectionWeight = reflectionResult.a;
   reflectionWeight = min(0.5, reflectionWeight);
   // The environment half of the split-sum approximation (Karis 2013): what a
   // surface returns of its surroundings takes the same Fresnel term as the
   // direct lobe, so both move off one number. A rough surface scatters the
   // reflected rays apart and returns less of any one of them.
   float reflectionStrength = 0.5 * (1.0 - roughness);
   vec3 reflectionContribution =
      reflectedColor * reflectionStrength *
      reflectionWeight * mix(shadowFactor, 1., 0.4);
   result += reflectionContribution *
      fresnelSchlick(materialF0(albedo, metallic), dot(normal, viewDir));
   
   FragColor = vec4(result, 1.0);
   //debugColor.yz = result.yz;
   //FragColor = vec4(debugColor, 1.);
}