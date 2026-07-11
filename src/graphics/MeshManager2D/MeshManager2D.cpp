// src/graphics/MeshManager2D.cpp
#include "MeshManager2D.h"
#include "../STBImageLoader.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>

MeshManager2D::MeshManager2D() {
    initializeShaders();
}

MeshManager2D::~MeshManager2D() {
}

void MeshManager2D::initializeShaders() {
    // Basic 2D instanced vertex shader
    std::string vertexShader = R"(
        #version 460 core
        
        layout (location = 0) in vec2 aPosition;
        layout (location = 1) in vec2 aTexCoord;
        layout (location = 2) in vec2 aInstancePosition;
        layout (location = 3) in vec2 aInstanceScale;
        layout (location = 4) in vec4 aInstanceColor;
        layout (location = 5) in float aInstanceOrientation;
        
        uniform mat4 uProjection;
        
        out vec2 vTexCoord;
        out vec4 vColor;
        
        void main() {
            // Apply instance transformations
            float cosTheta = cos(aInstanceOrientation);
            float sinTheta = sin(aInstanceOrientation);
            
            // Scale
            vec2 scaledPos = aPosition * aInstanceScale;
            
            // Rotate
            vec2 rotatedPos;
            rotatedPos.x = scaledPos.x * cosTheta - scaledPos.y * sinTheta;
            rotatedPos.y = scaledPos.x * sinTheta + scaledPos.y * cosTheta;
            
            // Translate
            vec2 finalPos = rotatedPos + aInstancePosition;
            
            gl_Position = uProjection * vec4(finalPos, 0.0, 1.0);
            vTexCoord = aTexCoord;
            vColor = aInstanceColor;
        }
    )";
    
    // Basic 2D fragment shader
    std::string fragmentShader = R"(
        #version 460 core
        
        in vec2 vTexCoord;
        in vec4 vColor;
        out vec4 FragColor;
        
        uniform sampler2D uTexture;
        uniform bool uHasTexture;

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
            if (uHasTexture) {
                vec4 texColor = texture(uTexture, vTexCoord);
                // Apply HSV transformation between vertex color and texture
                vec4 result = applyHSVTransform(vColor, texColor);
                
                if (result.a < 1.0/255.0) {
                    discard;
                }
                FragColor = result;
            } else {
                // No texture: use vertex color directly (already in RGBA)
                if (vColor.a < 1.0/255.0) {
                    discard;
                }
                FragColor = vColor;
            }
        }
    )";
    
    m_shaderProgram.loadVertexShader(vertexShader);
    m_shaderProgram.loadFragmentShader(fragmentShader);
    m_shaderProgram.linkShaders();
}

int MeshManager2D::createTexture(const std::string& path) {
    return m_textureManager.createTexture(path);
}

std::weak_ptr<Geometry2D> MeshManager2D::loadMesh(const std::string& geometryPath,
                                                 const std::string& texturePath,
                                                 int textureUnit,
                                                 bool enableTransparency) {
    // Load geometry data using AssimpLoader
    std::vector<AssetMeshData> meshDataList;
    try {
        AssimpLoader::load(geometryPath, &meshDataList);
    } catch (const std::exception& e) {
        std::cerr << "MeshManager2D: Failed to load geometry from " << geometryPath << ": " << e.what() << std::endl;
        return std::weak_ptr<Geometry2D>();
    }

    if (meshDataList.empty()) {
        std::cerr << "MeshManager2D: No mesh data found in " << geometryPath << std::endl;
        return std::weak_ptr<Geometry2D>();
    }
    
    // Use first mesh for 2D (assuming single mesh)
    const AssetMeshData& meshData = meshDataList[0];
    
    // Convert 3D data to 2D (use X,Y components)
    std::vector<Vertex2D> vertices;
    std::vector<unsigned int> indices;
    
    for (size_t i = 0; i < meshData.indices.size(); ++i) {
        int idx = meshData.indices[i];
        
        Vertex2D vertex;
        vertex.position = glm::vec2(meshData.positionsData[idx][0], meshData.positionsData[idx][1]);
        
        if (idx < static_cast<int>(meshData.uvsData.size())) {
            vertex.texCoord = glm::vec2(meshData.uvsData[idx][0], meshData.uvsData[idx][1]);
        } else {
            vertex.texCoord = glm::vec2(0.0f, 0.0f);
        }
        
        vertices.push_back(vertex);
        indices.push_back(static_cast<unsigned int>(i));
    }
    
    if (vertices.empty() || indices.empty()) {
        std::cerr << "MeshManager2D: Failed to load geometry from " << geometryPath << std::endl;
        return std::weak_ptr<Geometry2D>();
    }
    
    // Handle texture
    GLuint textureId = 0;
    int finalTextureUnit = -1;
    if (!texturePath.empty() && textureUnit == -1) {
        finalTextureUnit = createTexture(texturePath);
        if (finalTextureUnit >= 0) {
            textureId = m_textureManager.m_textures[finalTextureUnit].textureId;
        }
    } else if (textureUnit >= 0) {
        finalTextureUnit = textureUnit;
        // TODO: Support
        throw std::runtime_error("Error: Setting textures not implemented.");
    }
    
    auto geometry = std::make_shared<Geometry2D>(vertices, indices, textureId,
                                                 finalTextureUnit, enableTransparency);

    std::weak_ptr<Geometry2D> result = geometry;
    m_geometries.push_back(std::move(geometry));

    return result;
}

void MeshManager2D::releaseGeometry(std::weak_ptr<Geometry2D> geometryWeak) {
    auto geometry = geometryWeak.lock();
    if (!geometry) return;

    // Remove geometry
    m_geometries.erase(
        std::remove_if(m_geometries.begin(), m_geometries.end(),
            [geometry](const std::shared_ptr<Geometry2D>& geom) {
                return geom->m_uniqueId == geometry->m_uniqueId;
            }),
        m_geometries.end()
    );
}

void MeshManager2D::render(const glm::mat4& projection) {
    // Save current blend state
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);
    GLint srcBlend, dstBlend;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcBlend);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstBlend);

    m_shaderProgram.use();
    
    // Set uniforms
    m_shaderProgram.setUniformMatrix4f("uProjection", projection);
    
    // Render all geometries (they handle their own texture binding)
    for (const auto& geometry : m_geometries) {
        // Enable blending if geometry has transparency
        if (geometry->hasTransparency()) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        m_shaderProgram.setUniformInt("uTexture", geometry->getTextureUnit());
        m_shaderProgram.setUniformBool("uHasTexture", geometry->getTextureId() != 0);
        
        geometry->render();

        // Restore blend state after transparent geometry
        if (geometry->hasTransparency() && !blendWasEnabled) {
            glDisable(GL_BLEND);
        }
    }

    // Restore original blend state
    if (!blendWasEnabled) {
        glDisable(GL_BLEND);
    }
}

size_t MeshManager2D::getTotalInstanceCount() const {
    size_t total = 0;
    for (const auto& geometry : m_geometries) {
        total += geometry->getInstanceCount();
    }
    return total;
}
