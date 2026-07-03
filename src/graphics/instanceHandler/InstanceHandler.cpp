// InstanceHandler.cpp
#include "InstanceHandler.h"
#include "../ShaderProgram.h"
#include "../SSBOManager.h"
#include "../AssimpLoader.h"
#include "math/DekkerArithmetic.h"
#include <iostream>
#include <algorithm>
#include <set>

// Static member initialization
uint64_t Geometry::s_nextGeometryId = 0;
uint64_t Instance::s_nextInstanceId = 0;

Geometry::~Geometry() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO != 0) glDeleteBuffers(1, &m_VBO);
    if (m_EBO != 0) glDeleteBuffers(1, &m_EBO);
    if (m_instanceVBO != 0) glDeleteBuffers(1, &m_instanceVBO);
}

std::weak_ptr<Instance> Geometry::addInstance(
    int ssboIndex, int colorTextureUnit, int normalTextureUnit,
    int materialTextureUnit, const glm::dvec4& color, int maskTextureUnit) {
    auto instance = std::make_shared<Instance>();
    instance->m_ssboIndex = ssboIndex;
    instance->m_colorTextureUnit = colorTextureUnit;
    instance->m_normalTextureUnit = normalTextureUnit;
    instance->m_materialTextureUnit = materialTextureUnit;
    instance->m_maskTextureUnit = maskTextureUnit;
    instance->m_color = color;
    
    // Set buffer index to end of current data
    instance->m_bufferIndex = static_cast<uint32_t>(m_instances.size());
    
    // Add to both vectors at same index
    m_instances.push_back(instance);

    //instance->m_colorTextureUnit = -1;
    InstanceData data = createInstanceData(instance.get());
    m_instanceData.push_back(data);
    
    // Check if we need to grow the buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    if (m_instanceData.size() > m_instanceBufferCapacity) {
        // Double the buffer capacity
        m_instanceBufferCapacity = std::max(static_cast<size_t>(1), m_instanceBufferCapacity * 2);
        // Allocate larger buffer (uninitialized)
        glBufferData(GL_ARRAY_BUFFER, m_instanceBufferCapacity * sizeof(InstanceData), 
                     nullptr, GL_DYNAMIC_DRAW);
        // Copy only the valid data
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_instanceData.size() * sizeof(InstanceData), 
                       m_instanceData.data());
    } else {
        // Just update the new instance data
        glBufferSubData(GL_ARRAY_BUFFER, (m_instanceData.size() - 1) * sizeof(InstanceData), 
                       sizeof(InstanceData), &data);
    }
    
    //std::cout << "Geometry " << m_uniqueId << ": Added instance " << instance->m_uniqueId 
    //          << " at index " << instance->m_bufferIndex << std::endl;
    
    return instance;
}

void Geometry::removeInstance(std::weak_ptr<Instance> instanceWeak) {
    auto instance = instanceWeak.lock();
    if (!instance) return;
    
    uint32_t index = instance->m_bufferIndex;
    if (index >= m_instances.size()) {
        std::cerr << "Geometry: Invalid buffer index in removeInstance" << std::endl;
        return;
    }
    
    // If not the last element, move last element to this position
    if (index != m_instances.size() - 1) {
        // Move last instance to this position in both vectors
        m_instances[index] = m_instances.back();
        m_instanceData[index] = m_instanceData.back();
        // Update moved instance's buffer index
        m_instances[index]->m_bufferIndex = index;
    }
    
    // Remove last element from both vectors
    m_instances.pop_back();
    m_instanceData.pop_back();
    
    // Update GPU buffer only if we moved an instance
    if (!m_instanceData.empty() && index != m_instanceData.size()) {
         glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        glBufferSubData(GL_ARRAY_BUFFER, index * sizeof(InstanceData), 
                       sizeof(InstanceData), &m_instanceData[index]);
    }
    
    //std::cout << "Geometry " << m_uniqueId << ": Removed instance " << instance->m_uniqueId << std::endl;
}

void Geometry::updateInstanceInBuffer(Instance* instance) {
    if (instance->m_bufferIndex >= m_instanceData.size()) {
        return;
    }
    
    // Update CPU data
    m_instanceData[instance->m_bufferIndex] = createInstanceData(instance);
    //m_instanceData[instance->m_bufferIndex].colorTextureUnit = -1;
    
    // Update GPU buffer at specific offset
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, instance->m_bufferIndex * sizeof(InstanceData), 
                    sizeof(InstanceData), &m_instanceData[instance->m_bufferIndex]);
}

InstanceData Geometry::createInstanceData(Instance* instance) {
    InstanceData data;
    
    data.localPosition = glm::vec3(instance->m_localPosition);
    data.padding1 = 0.0f;
    data.localOrientation = glm::vec4(static_cast<float>(instance->m_localOrientation.x), 
                                    static_cast<float>(instance->m_localOrientation.y),
                                    static_cast<float>(instance->m_localOrientation.z), 
                                    static_cast<float>(instance->m_localOrientation.w));
    data.localScale = glm::vec3(instance->m_localScale);
    data.padding2 = 0.0f;
    data.color = glm::vec4(instance->m_color);
    data.ssboIndex = instance->m_ssboIndex;
    data.colorTextureUnit = instance->m_colorTextureUnit;
    data.normalTextureUnit = instance->m_normalTextureUnit;
    data.materialTextureUnit = instance->m_materialTextureUnit;
    data.maskTextureUnit = instance->m_maskTextureUnit;
    
    //data.colorTextureUnit = -1;
    
    return data;
}

InstanceHandler::InstanceHandler(SSBOManager* ssboManager)
    : m_ssboManager(ssboManager) {
    
    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }
    
    // Create shader program (use instance-specific shaders)
    createShaderProgram();

    // Create G-buffer shader program
    m_gbufferShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_vertex_shader.vert");
    m_gbufferShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/gbuffer_fragment_shader.frag");
    m_gbufferShaderProgram.linkShaders();

    // Create depth shader program
    m_depthShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_depth_vertex_shader.vert");
    m_depthShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/depth_fragment_shader.frag");
    m_depthShaderProgram.linkShaders();
}

InstanceHandler::~InstanceHandler() {
    // ShaderProgram destructor handles shader cleanup
}

void InstanceHandler::createShaderProgram() {
    // Use instance-specific vertex shader but reuse fragment shader
    m_shaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_vertex_shader.vert");
    m_shaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/fragment_shader.frag");
    m_shaderProgram.linkShaders();
}

int InstanceHandler::createTexture(const std::string& texturePath) {
    return m_textureManager.createTexture(texturePath);
}

std::weak_ptr<Geometry> InstanceHandler::createGeometry(const std::string& modelPath) {
    auto geometry = std::make_shared<Geometry>();
    
    loadGeometryFromFile(geometry.get(), modelPath);
    m_geometries.push_back(geometry);
    
    return geometry;
}

void InstanceHandler::releaseGeometry(std::weak_ptr<Geometry> geometryWeak) {
    auto geometry = geometryWeak.lock();
    if (!geometry) return;
    
    // Remove geometry
    m_geometries.erase(
        std::remove_if(m_geometries.begin(), m_geometries.end(),
            [geometry](const std::shared_ptr<Geometry>& geom) {
                return geom->m_uniqueId == geometry->m_uniqueId;
            }),
        m_geometries.end()
    );
}

void InstanceHandler::loadGeometryFromFile(Geometry* geometry, const std::string& modelPath) {
    std::vector<AssetMeshData> meshes;
    AssimpLoader::load(modelPath, &meshes, false);
    
    if (meshes.empty()) {
        throw std::runtime_error("No meshes found in model file");
    }
    
    // Combine all meshes into single geometry
    std::vector<GeometryVertex> vertices;
    std::vector<uint32_t> indices;
    
    for (const AssetMeshData& mesh : meshes) {
        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
        
        // Add vertices
        for (size_t i = 0; i < mesh.positionsData.size(); ++i) {
            GeometryVertex vertex;
            vertex.position = glm::vec3(mesh.positionsData[i][0], mesh.positionsData[i][1], mesh.positionsData[i][2]);
            vertex.normal = glm::vec3(mesh.normalsData[i][0], mesh.normalsData[i][1], mesh.normalsData[i][2]);
            vertex.tangent = glm::vec3(mesh.tangentsData[i][0], mesh.tangentsData[i][1], mesh.tangentsData[i][2]);
            vertex.uv = glm::vec2(mesh.uvsData[i][0], mesh.uvsData[i][1]);
            vertices.push_back(vertex);
        }
        
        // Add indices
        if (!mesh.indices.empty()) {
            for (int index : mesh.indices) {
                indices.push_back(baseVertex + static_cast<uint32_t>(index));
            }
        } else {
            // Generate indices for triangle list
            for (size_t i = 0; i < mesh.positionsData.size(); ++i) {
                indices.push_back(baseVertex + static_cast<uint32_t>(i));
            }
        }
    }
    
    setupGeometryOpenGL(geometry, vertices, indices);
}

void InstanceHandler::setupGeometryOpenGL(Geometry* geometry, 
                                        const std::vector<GeometryVertex>& vertices,
                                        const std::vector<uint32_t>& indices) {
    geometry->m_vertexCount = static_cast<uint32_t>(vertices.size());
    geometry->m_indexCount = static_cast<uint32_t>(indices.size());
    geometry->m_hasIndices = !indices.empty();
    
    // Generate VAO
    glGenVertexArrays(1, &geometry->m_VAO);
    glBindVertexArray(geometry->m_VAO);
    
    // Generate and fill VBO
    glGenBuffers(1, &geometry->m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, geometry->m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GeometryVertex), 
                 vertices.data(), GL_STATIC_DRAW);
    
    // Setup vertex attributes (per-vertex data)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, position));
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, normal));
    glEnableVertexAttribArray(1);
    
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, tangent));
    glEnableVertexAttribArray(2);
    
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, uv));
    glEnableVertexAttribArray(3);
    
    // Create instance buffer
    glGenBuffers(1, &geometry->m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, geometry->m_instanceVBO);
    
    // Setup instance attributes (per-instance data)
    // Local position
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, localPosition));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);
    
    // Local orientation quaternion
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, localOrientation));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);
    
    // Local scale
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, localScale));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    // Instance color (location 7)
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);
    
    // Mesh index for SSBO lookup (location 8)
    glVertexAttribIPointer(8, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, ssboIndex));
    glEnableVertexAttribArray(8);
    glVertexAttribDivisor(8, 1);
    
    // Color texture unit (location 9)
    glVertexAttribIPointer(9, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, colorTextureUnit));
    glEnableVertexAttribArray(9);
    glVertexAttribDivisor(9, 1);

    // Normal texture unit (location 10)
    glVertexAttribIPointer(10, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, normalTextureUnit));
    glEnableVertexAttribArray(10);
    glVertexAttribDivisor(10, 1);
    
    // Material texture unit (location 11)
    glVertexAttribIPointer(11, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, materialTextureUnit));
    glEnableVertexAttribArray(11);
    glVertexAttribDivisor(11, 1);

    // Mask texture unit (location 12)
    glVertexAttribIPointer(12, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, maskTextureUnit));
    glEnableVertexAttribArray(12);
    glVertexAttribDivisor(12, 1);

    // Generate and fill EBO if we have indices
    if (geometry->m_hasIndices) {
        glGenBuffers(1, &geometry->m_EBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry->m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), 
                     indices.data(), GL_STATIC_DRAW);
    }
    
    glBindVertexArray(0);
}

void InstanceHandler::render(const glm::mat4& view, const glm::mat4& projection, 
                           uint64_t frame, uint64_t time, double timeRemainder, 
                           const glm::dvec3& lightDir, const glm::dvec3& camPos,
                           bool renderOpaque, bool renderTransparent) {
    if (m_geometries.empty()) return;
    
    // Use forward rendering shader program
    m_shaderProgram.use();
    
    
    // Set light direction (unique to forward rendering)
    GLint lightDirLoc = glGetUniformLocation(m_shaderProgram.getID(), "u_lightDir");
    if (lightDirLoc != -1) {
        // Transform light direction to view space
        glm::vec4 lightDirView = view * glm::vec4(lightDir, 0.0);
        glm::vec3 lightDirFloat(lightDirView.x, lightDirView.y, lightDirView.z);
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirFloat));
    }

    // Use helper for common rendering logic
    renderGeometryHelper(view, projection, frame, time, timeRemainder, camPos, renderOpaque, renderTransparent);
}

void InstanceHandler::renderGeometry(const glm::mat4& view, const glm::mat4& projection, 
                                   uint64_t frame, uint64_t time, double timeRemainder, 
                                   const glm::dvec3& /* lightDir */, const glm::dvec3& camPos,
                                   bool renderOpaque, bool renderTransparent) {
    if (m_geometries.empty()) return;
    
    // Use G-buffer shader program
    m_gbufferShaderProgram.use();
    
    // Use helper for common rendering logic
    renderGeometryHelper(view, projection, frame, time, timeRemainder, camPos, renderOpaque, renderTransparent);
}

void InstanceHandler::renderDepth(const glm::mat4& view, const glm::mat4& projection, 
                                uint64_t frame, uint64_t time, double timeRemainder, 
                                const glm::dvec3& camPos,
                                bool renderOpaque, bool renderTransparent) {
    if (m_geometries.empty()) return;
    
    // Use depth-only shader program
    m_depthShaderProgram.use();
    
    // Use helper for common rendering logic
    renderGeometryHelper(view, projection, frame, time, timeRemainder, camPos, renderOpaque, renderTransparent);
}

void InstanceHandler::renderGeometryHelper(
    const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
    double timeRemainder, const glm::dvec3& camPos,
    bool renderOpaque, bool renderTransparent) {
    
    // Get currently active shader program
    GLint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram == 0) {
        throw std::runtime_error("No shader program is currently active");
    }
    unsigned int programID = static_cast<unsigned int>(currentProgram);
    
    // Set uniforms (same as MeshHandler::renderGeometry)
    GLint viewLoc = glGetUniformLocation(programID, "view");
    GLint projectionLoc = glGetUniformLocation(programID, "projection");
    GLint frameLoc = glGetUniformLocation(programID, "u_frame");
    GLint timeLoc = glGetUniformLocation(programID, "u_time");
    GLint timeRemainderLoc = glGetUniformLocation(programID, "u_timeRemainder");
    GLint cameraPosHighLoc = glGetUniformLocation(programID, "u_cameraPositionHigh");
    GLint cameraPosLowLoc = glGetUniformLocation(programID, "u_cameraPositionLow");
    
    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (projectionLoc != -1) glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projection));
    if (frameLoc != -1) glUniform1ui(frameLoc, frame);
    if (timeLoc != -1) glUniform1ui(timeLoc, time);
    if (timeRemainderLoc != -1) glUniform1f(timeRemainderLoc, static_cast<float>(timeRemainder));
    
    // Set camera position as Dekker number
    if (cameraPosHighLoc == -1 || cameraPosLowLoc == -1) {
        throw std::runtime_error("Camera position Dekker uniforms not found in shader");
    }
    {
        typedef DekkerArithmetic<float> DekkerFloat;
        DekkerFloat::DekkerNumber camX(camPos.x);
        DekkerFloat::DekkerNumber camY(camPos.y);
        DekkerFloat::DekkerNumber camZ(camPos.z);
        glm::vec3 camPosHigh(camX.main, camY.main, camZ.main);
        glm::vec3 camPosLow(camX.error, camY.error, camZ.error);
        glUniform3fv(cameraPosHighLoc, 1, glm::value_ptr(camPosHigh));
        glUniform3fv(cameraPosLowLoc, 1, glm::value_ptr(camPosLow));
    }
    
    // Bind all textures
    for (const auto& texture : m_textureManager.m_textures) {
        // Debug check: ensure texture unit is within shader array bounds
        if (texture.textureUnit >= ShaderProgram::s_maxTextureUnits) {
            throw std::runtime_error("InstanceHandler texture unit " + std::to_string(texture.textureUnit) +
                                   " exceeds shader array size (" +
                                   std::to_string(ShaderProgram::s_maxTextureUnits) +
                                   ") for texture: " + texture.path);
        }
        glActiveTexture(GL_TEXTURE0 + texture.textureUnit);
        glBindTexture(GL_TEXTURE_2D, texture.textureId);
        
        // Set texture uniform
        std::string textureName = "u_textures[" + std::to_string(texture.textureUnit) + "]";
        GLint textureLoc = glGetUniformLocation(programID, textureName.c_str());
        if (textureLoc != -1) {
            glUniform1i(textureLoc, static_cast<GLint>(texture.textureUnit));
        }
    }
    
    // Set depth testing and render each geometry with its instances
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    
    for (const auto& geometry : m_geometries) {
        if (geometry->m_instanceData.empty()) continue;

        // Filter based on transparency settings
        bool isTransparent = geometry->m_enableAlphaBlending;
        if ((isTransparent && !renderTransparent) || (!isTransparent && !renderOpaque)) {
            continue;
        }

        // Save current OpenGL state
        GLfloat savedDepthRange[2];
        glGetFloatv(GL_DEPTH_RANGE, savedDepthRange);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        
        // Apply geometry-specific depth compression
        if (geometry->m_depthCompression < 1.0) {
            glDepthRange(0.0, static_cast<GLdouble>(geometry->m_depthCompression));
        }
        
        // Apply geometry-specific alpha blending
        if (geometry->m_enableAlphaBlending) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        glBindVertexArray(geometry->m_VAO);
        
        if (geometry->m_hasIndices) {
            glDrawElementsInstanced(GL_TRIANGLES, geometry->m_indexCount, GL_UNSIGNED_INT, 0, 
                                  static_cast<GLsizei>(geometry->m_instanceData.size()));
        } else {
            glDrawArraysInstanced(GL_TRIANGLES, 0, geometry->m_vertexCount, 
                                static_cast<GLsizei>(geometry->m_instanceData.size()));
        }

        // Restore OpenGL state
        glDepthRange(savedDepthRange[0], savedDepthRange[1]);
        
        if (geometry->m_enableAlphaBlending && !blendEnabled) {
            glDisable(GL_BLEND);
        } else if (!geometry->m_enableAlphaBlending && blendEnabled) {
            glEnable(GL_BLEND);
        }
    }
    
    glBindVertexArray(0);
}

std::pair<bool, std::string> InstanceHandler::reloadShaders() {
   std::string allErrors;
   bool allSuccess = true;
   
   auto [success1, error1] = m_shaderProgram.reloadShaders();
   auto [success2, error2] = m_gbufferShaderProgram.reloadShaders();
   auto [success3, error3] = m_depthShaderProgram.reloadShaders();
   
   if (!success1) { allSuccess = false; allErrors += "Main shader: " + error1 + "\n"; }
   if (!success2) { allSuccess = false; allErrors += "GBuffer shader: " + error2 + "\n"; }
   if (!success3) { allSuccess = false; allErrors += "Depth shader: " + error3 + "\n"; }
   
   return {allSuccess, allSuccess ? "All InstanceHandler shaders reloaded successfully" : allErrors};
}





