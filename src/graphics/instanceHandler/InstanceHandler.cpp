// InstanceHandler.cpp
#include "InstanceHandler.h"
#include "../ShaderProgram.h"
#include "../SSBOManager.h"
#include "../InstanceFrameUniforms.h"
#include <algorithm>
#include <stdexcept>

InstanceHandler::InstanceHandler(SSBOManager* ssboManager)
    : m_ssboManager(ssboManager) {
    
    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }
    
    // Create overlay (forward) shader program
    createShaderProgram();

    // Create G-buffer shader program
    m_gbufferShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_vertex_shader.vert");
    m_gbufferShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/gbuffer_fragment_shader.frag");
    m_gbufferShaderProgram.linkShaders();

    // Create depth shader program
    m_depthShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_depth_vertex_shader.vert");
    m_depthShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/depth_fragment_shader.frag");
    m_depthShaderProgram.linkShaders();

    // Create Weighted Blended OIT shader program (shares the instance vertex shader)
    m_oitShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_vertex_shader.vert");
    m_oitShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/oit_fragment_shader.frag");
    m_oitShaderProgram.linkShaders();
}

InstanceHandler::~InstanceHandler() {
    // ShaderProgram destructor handles shader cleanup
}

void InstanceHandler::createShaderProgram() {
    // Instance vertex shader with the shared forward fragment shader
    m_overlayShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_vertex_shader.vert");
    m_overlayShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/fragment_shader.frag");
    m_overlayShaderProgram.linkShaders();
}

int InstanceHandler::createTexture(const std::string& texturePath) {
    return m_textureManager.createTexture(texturePath);
}

std::weak_ptr<Geometry> InstanceHandler::createGeometry(const std::string& modelPath,
                                                       RenderLayer layer) {
    auto geometry = Geometry::loadFromFile(modelPath);
    geometry->setRenderLayer(layer);
    m_geometries.push_back(geometry);

    return geometry;
}

std::weak_ptr<Geometry> InstanceHandler::createGeometry(
    const std::vector<GeometryVertex>& vertices, RenderLayer layer) {
    auto geometry = Geometry::createFromVertices(vertices);
    geometry->setRenderLayer(layer);
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

void InstanceHandler::renderGeometry(const FrameRenderParams& params) {
    if (m_geometries.empty()) return;

    // Use G-buffer shader program
    m_gbufferShaderProgram.use();

    renderGeometryHelper(params, RenderLayer::Opaque);
}

void InstanceHandler::renderDepth(const FrameRenderParams& params) {
    if (m_geometries.empty()) return;

    // Use depth-only shader program
    m_depthShaderProgram.use();

    renderGeometryHelper(params, RenderLayer::Opaque);
}

void InstanceHandler::renderOverlay(const FrameRenderParams& params) {
    if (m_geometries.empty()) return;

    // Use the forward shader (shading matches the transparent/OIT model)
    m_overlayShaderProgram.use();

    // Set light direction in view space
    GLint lightDirLoc = glGetUniformLocation(m_overlayShaderProgram.getID(), "u_lightDir");
    if (lightDirLoc != -1) {
        glm::vec4 lightDirView =
            glm::mat4(params.view) * glm::vec4(glm::vec3(params.lightDir), 0.0f);
        glm::vec3 lightDirFloat(lightDirView.x, lightDirView.y, lightDirView.z);
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirFloat));
    }

    renderGeometryHelper(params, RenderLayer::Overlay);
}

void InstanceHandler::renderOIT(const FrameRenderParams& params) {
    if (m_geometries.empty()) return;

    // Use the OIT accumulation shader
    m_oitShaderProgram.use();

    // Set light direction (transparent shading matches the forward model)
    GLint lightDirLoc = glGetUniformLocation(m_oitShaderProgram.getID(), "u_lightDir");
    if (lightDirLoc != -1) {
        glm::vec4 lightDirView =
            glm::mat4(params.view) * glm::vec4(glm::vec3(params.lightDir), 0.0f);
        glm::vec3 lightDirFloat(lightDirView.x, lightDirView.y, lightDirView.z);
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirFloat));
    }

    renderGeometryHelper(params, RenderLayer::Transparent);
}

void InstanceHandler::renderGeometryHelper(
    const FrameRenderParams& params, RenderLayer layer) {

    // Get currently active shader program
    GLint currentProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    if (currentProgram == 0) {
        throw std::runtime_error("No shader program is currently active");
    }
    unsigned int programID = static_cast<unsigned int>(currentProgram);
    
    // Per-frame camera/time uniforms (view, projection, time, Dekker camera).
    setInstanceFrameUniforms(programID, params);

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
    
    for (const auto& geometry : m_geometries) {
        if (geometry->m_instanceData.empty()) continue;
        if (geometry->m_renderLayer != layer) continue;

        glBindVertexArray(geometry->m_VAO);

        if (geometry->m_hasIndices) {
            glDrawElementsInstanced(GL_TRIANGLES, geometry->m_indexCount, GL_UNSIGNED_INT, 0,
                                  static_cast<GLsizei>(geometry->m_instanceData.size()));
        } else {
            glDrawArraysInstanced(GL_TRIANGLES, 0, geometry->m_vertexCount,
                                static_cast<GLsizei>(geometry->m_instanceData.size()));
        }
    }

    glBindVertexArray(0);
}

std::pair<bool, std::string> InstanceHandler::reloadShaders() {
   std::string allErrors;
   bool allSuccess = true;
   
   auto [success1, error1] = m_overlayShaderProgram.reloadShaders();
   auto [success2, error2] = m_gbufferShaderProgram.reloadShaders();
   auto [success3, error3] = m_depthShaderProgram.reloadShaders();
   auto [success4, error4] = m_oitShaderProgram.reloadShaders();

   if (!success1) { allSuccess = false; allErrors += "Main shader: " + error1 + "\n"; }
   if (!success2) { allSuccess = false; allErrors += "GBuffer shader: " + error2 + "\n"; }
   if (!success3) { allSuccess = false; allErrors += "Depth shader: " + error3 + "\n"; }
   if (!success4) { allSuccess = false; allErrors += "OIT shader: " + error4 + "\n"; }
   
   return {allSuccess, allSuccess ? "All InstanceHandler shaders reloaded successfully" : allErrors};
}





