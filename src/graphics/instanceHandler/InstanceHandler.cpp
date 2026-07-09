// InstanceHandler.cpp
#include "InstanceHandler.h"
#include "../ShaderProgram.h"
#include "../SSBOManager.h"
#include "math/DekkerArithmetic.h"
#include <algorithm>

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

    // Create Weighted Blended OIT shader program (shares the instance vertex shader)
    m_oitShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/instanceHandler/instance_vertex_shader.vert");
    m_oitShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/oit_fragment_shader.frag");
    m_oitShaderProgram.linkShaders();
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

std::weak_ptr<Geometry> InstanceHandler::createGeometry(const std::string& modelPath,
                                                       bool transparent) {
    auto geometry = Geometry::loadFromFile(modelPath);
    geometry->setAlphaBlending(transparent);
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

void InstanceHandler::render(const FrameRenderParams& params,
                           bool renderOpaque, bool renderTransparent) {
    if (m_geometries.empty()) return;

    // Use forward rendering shader program
    m_shaderProgram.use();


    // Set light direction (unique to forward rendering)
    GLint lightDirLoc = glGetUniformLocation(m_shaderProgram.getID(), "u_lightDir");
    if (lightDirLoc != -1) {
        // Transform light direction to view space
        glm::vec4 lightDirView =
            glm::mat4(params.view) * glm::vec4(glm::vec3(params.lightDir), 0.0f);
        glm::vec3 lightDirFloat(lightDirView.x, lightDirView.y, lightDirView.z);
        glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirFloat));
    }

    // Use helper for common rendering logic
    renderGeometryHelper(params, renderOpaque, renderTransparent);
}

void InstanceHandler::renderGeometry(const FrameRenderParams& params,
                                   bool renderOpaque, bool renderTransparent) {
    if (m_geometries.empty()) return;

    // Use G-buffer shader program
    m_gbufferShaderProgram.use();

    // Use helper for common rendering logic
    renderGeometryHelper(params, renderOpaque, renderTransparent);
}

void InstanceHandler::renderDepth(const FrameRenderParams& params,
                                bool renderOpaque, bool renderTransparent) {
    if (m_geometries.empty()) return;

    // Use depth-only shader program
    m_depthShaderProgram.use();

    // Use helper for common rendering logic
    renderGeometryHelper(params, renderOpaque, renderTransparent);
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

    // Only transparent geometries; blend/depth-write state owned by the caller.
    renderGeometryHelper(params, /*renderOpaque=*/false, /*renderTransparent=*/true,
                         /*oitBlend=*/true);
}

void InstanceHandler::renderGeometryHelper(
    const FrameRenderParams& params,
    bool renderOpaque, bool renderTransparent, bool oitBlend) {

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

    // Convert double precision parameters to float precision for OpenGL
    glm::mat4 viewFloat{ params.view };
    glm::mat4 projectionFloat{ params.projection };

    if (viewLoc != -1) glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(viewFloat));
    if (projectionLoc != -1)
        glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projectionFloat));
    if (frameLoc != -1) glUniform1ui(frameLoc, params.frame);
    if (timeLoc != -1) glUniform1ui(timeLoc, params.time);
    if (timeRemainderLoc != -1)
        glUniform1f(timeRemainderLoc, static_cast<float>(params.timeRemainder));

    // Set camera position as Dekker number
    if (cameraPosHighLoc == -1 || cameraPosLowLoc == -1) {
        throw std::runtime_error("Camera position Dekker uniforms not found in shader");
    }
    {
        typedef DekkerArithmetic<float> DekkerFloat;
        DekkerFloat::DekkerNumber camX(params.camPos.x);
        DekkerFloat::DekkerNumber camY(params.camPos.y);
        DekkerFloat::DekkerNumber camZ(params.camPos.z);
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
        
        // Apply geometry-specific alpha blending. In the OIT pass the caller owns
        // the blend state (per-target accumulation), so leave it untouched.
        if (!oitBlend && geometry->m_enableAlphaBlending) {
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

        if (!oitBlend) {
            if (geometry->m_enableAlphaBlending && !blendEnabled) {
                glDisable(GL_BLEND);
            } else if (!geometry->m_enableAlphaBlending && blendEnabled) {
                glEnable(GL_BLEND);
            }
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
   auto [success4, error4] = m_oitShaderProgram.reloadShaders();

   if (!success1) { allSuccess = false; allErrors += "Main shader: " + error1 + "\n"; }
   if (!success2) { allSuccess = false; allErrors += "GBuffer shader: " + error2 + "\n"; }
   if (!success3) { allSuccess = false; allErrors += "Depth shader: " + error3 + "\n"; }
   if (!success4) { allSuccess = false; allErrors += "OIT shader: " + error4 + "\n"; }
   
   return {allSuccess, allSuccess ? "All InstanceHandler shaders reloaded successfully" : allErrors};
}





