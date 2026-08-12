// InstanceHandler.cpp
#include "InstanceHandler.h"
#include "../TextureStore.h"
#include "../ShaderProgram.h"
#include "../SSBOManager.h"
#include "../InstanceFrameUniforms.h"
#include <algorithm>
#include <stdexcept>

InstanceHandler::InstanceHandler(SSBOManager* ssboManager, TextureStore* textureStore)
    : m_textureStore(textureStore), m_ssboManager(ssboManager) {
    
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

int InstanceHandler::createTexture(std::weak_ptr<Geometry> geometryWeak,
                                   const std::string& texturePath) {
    const std::shared_ptr<Geometry> geometry{geometryWeak.lock()};
    if (!geometry) {
        throw std::runtime_error("InstanceHandler::createTexture: geometry has expired");
    }

    // Loaded once for the whole engine, then given a slot in this geometry. Two
    // geometries wearing the same texture share the pixels and spend a unit each.
    return geometry->addTexture(m_textureStore->createFromFile(texturePath));
}

void InstanceHandler::bindGeometryTextures(const Geometry& geometry,
                                           ShaderProgram& program) {
    for (size_t unit{0}; unit < geometry.m_textureUnits.size(); ++unit) {
        const std::shared_ptr<Texture2D> texture{geometry.m_textureUnits[unit].lock()};
        if (!texture) continue;

        m_textureStore->bindTexture(static_cast<int>(unit), texture->getID());

        const GLint textureLoc{program.getTextureUnitLocation(static_cast<int>(unit))};
        if (textureLoc != -1) {
            glUniform1i(textureLoc, static_cast<GLint>(unit));
        }
    }
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

    renderGeometryHelper(m_gbufferShaderProgram, params, RenderLayer::Opaque);
}

void InstanceHandler::renderDepth(const FrameRenderParams& params) {
    if (m_geometries.empty()) return;

    // Use depth-only shader program
    m_depthShaderProgram.use();

    renderGeometryHelper(m_depthShaderProgram, params, RenderLayer::Opaque);
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

    renderGeometryHelper(m_overlayShaderProgram, params, RenderLayer::Overlay);
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

    renderGeometryHelper(m_oitShaderProgram, params, RenderLayer::Transparent);
}

void InstanceHandler::renderGeometryHelper(
    ShaderProgram& program, const FrameRenderParams& params, RenderLayer layer) {

    const unsigned int programID{program.getID()};

    // Per-frame camera/time uniforms (view, projection, time, Dekker camera).
    setInstanceFrameUniforms(programID, params);

    // The passes between this one and the last bind their own targets over these
    // units, so what the store remembers about them no longer holds.
    m_textureStore->invalidateBindings();

    for (const auto& geometry : m_geometries) {
        if (geometry->m_instanceData.empty()) continue;
        if (geometry->m_renderLayer != layer) continue;

        // Per draw, not per pass: a geometry binds only the textures its own
        // instances name, so the 32 units bound the busiest single geometry
        // rather than everything this renderer has loaded. Consecutive
        // geometries wearing the same texture cost nothing, since the store
        // skips a bind that would not change the unit.
        bindGeometryTextures(*geometry, program);

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





