// GraphicsEngine.cpp
#include "GraphicsEngine.h"
#include <glm/ext/matrix_clip_space.hpp>
#include "GraphicsEngineBase.h"
#include "GraphicsCallbacks.h"
#include "SSBOManager.h"
#include "deferredRenderer/DeferredRenderer.h"
#include "MeshManager2D/MeshManager2D.h"
#include "instanceHandler/InstanceHandler.h"
#include "rayVolume/RayVolumeHandler.h"
#include "cdlod/CdlodHandler.h"
#include "TextureStore.h"
#include "shadowRenderer/ShadowRenderer.h"
#include "utils/HashFunctions.h"
#include <iostream>
#include <filesystem>

GraphicsEngine::GraphicsEngine(
    TimeHandler* timeHandler,
    int screenWidth,
    int screenHeight,
    const std::string& windowTitle,
    size_t maxTriangles,
    size_t maxMeshes,
    GraphicsEngineBase::Mode mode,
    const std::filesystem::path& controlRecordingDir)
{
    // Create GraphicsEngineBase
    m_graphicsEngineBase =
        std::make_shared<GraphicsEngineBase>(timeHandler, mode, controlRecordingDir);
    
    // Configure window
    m_graphicsEngineBase->m_screen_width = screenWidth;
    m_graphicsEngineBase->m_screen_height = screenHeight;
    
    if (!windowTitle.empty()) {
        glfwSetWindowTitle(m_graphicsEngineBase->m_window, windowTitle.c_str());
    }

    // Create SSBO manager and pass to mesh handler
    m_ssboManager = std::make_unique<SSBOManager>(maxMeshes);
    // The one owner of every content texture; the renderers below only cite them.
    m_textureStore = std::make_unique<TextureStore>();
    m_meshHandler = std::make_unique<MeshHandler>(maxTriangles, m_ssboManager.get(),
                                                  m_textureStore.get());

    // Create deferred renderer
    m_deferredRenderer = std::make_unique<DeferredRenderer>();
    m_deferredRenderer->setupGBuffer(screenWidth, screenHeight);

    // Create instance handler
    m_instanceHandler =
        std::make_unique<InstanceHandler>(m_ssboManager.get(), m_textureStore.get());

    // Create ray-volume handler (proxy-geometry volumetric effects)
    m_rayVolumeHandler = std::make_unique<RayVolumeHandler>(m_ssboManager.get());

    // Create CDLOD handler (distance-subdivided cube-quadtree bodies)
    m_cdlodHandler = std::make_unique<CdlodHandler>(m_ssboManager.get(), m_textureStore.get());

    // Create 2D mesh manager
    m_meshManager2D = std::make_unique<MeshManager2D>(m_textureStore.get());

    // Create shadow renderer
    m_shadowRenderer = std::make_unique<ShadowRenderer>();
    //m_shadowRenderer->setupShadowMaps(8192, 8192, 3, {50.0, 200.0, 800.0});
    // Cascade radii and caster reach are both scene-scale metres: the radii bound
    // what each cascade shades, the reach how far off a caster may stand from it.
    m_shadowRenderer->setupShadowMaps(4096, 4096, 4, {8., 64.0, 512.0, 4096.0}, 10000.0);
    //m_shadowRenderer->setupShadowMaps(4096, 4096, 4, {27., 81.0, 243.0, 729.0});
    //m_shadowRenderer->setupShadowMaps(4096, 4096, 3, {50.0, 200.0, 800.0});
    //m_shadowRenderer->setupShadowMaps(2048, 2048, 3, {50.0, 200.0, 800.0});
    //m_shadowRenderer->setupShadowMaps(1024, 1024, 3, {25.0, 100.0, 400.0});
    //m_shadowRenderer->setupShadowMaps(512, 512, 3, {12.0, 50.0, 200.0});

    // Register framebuffer and window callbacks
    m_graphicsEngineBase->registerFramebufferCallback([this](int width, int height) {
        m_deferredRenderer->resizeGBuffer(width, height);
    });

    // Now that framebuffer callbacks are wired up, apply the actual framebuffer
    // size so the viewport, screen size, and G-buffer all match reality.
    m_graphicsEngineBase->applyInitialFramebufferSize();
}

GraphicsEngine::~GraphicsEngine() {
    // CRITICAL: Destroy OpenGL resources before GraphicsEngineBase destroys the context
    // These must be destroyed in reverse dependency order
    m_meshManager2D.reset();
    m_rayVolumeHandler.reset();
    m_cdlodHandler.reset();
    m_instanceHandler.reset();
    m_shadowRenderer.reset();
    m_deferredRenderer.reset();
    m_meshHandler.reset();
    m_ssboManager.reset();
    // Last: every texture handle above points into it.
    m_textureStore.reset();
}

void GraphicsEngine::beginFrame() {
    m_graphicsEngineBase->clearScreen();
    // After clearScreen, so the vsync throttle is behind us and the sample
    // lands at the frame's true start rather than inside the previous frame's
    // presentation wait.
    m_graphicsEngineBase->updateFrameTiming();
    m_graphicsEngineBase->updateInput();
}

void GraphicsEngine::render() {
    m_graphicsEngineBase->calculateCameraVelocity();
    renderScene();
}

void GraphicsEngine::endFrame() {
    m_graphicsEngineBase->checkGLErrors();
    m_graphicsEngineBase->swapBuffers();
    m_graphicsEngineBase->incrementFrame();
}

void GraphicsEngine::renderScene() {
    // Per-frame parameters shared by the camera-view render passes
    FrameRenderParams frameParams{
        m_graphicsEngineBase->getViewMatrix(),
        m_graphicsEngineBase->getProjectionMatrix(),
        getFrameNum(),
        m_currentInterpolationTimeStep,
        m_interpolationTimeRemainder,
        m_lightDirection,
        getCamPos(),
        m_graphicsEngineBase->m_paniniHorizontal,
        m_graphicsEngineBase->m_paniniVertical,
        m_graphicsEngineBase->getPaniniFitScale(),
        m_graphicsEngineBase->m_ditherStrength
    };

    // The cascades are placed before anything selects against them: what each one
    // can be shadowed by is what decides which patches have to be drawn into it.
    if (m_shadowsEnabled) {
        // View direction in world axes: the view matrix's third row is the camera's
        // backward, so negating it gives what the cascades are pushed along.
        const glm::dvec3 camForward{-glm::dvec3{frameParams.view[0][2], frameParams.view[1][2],
                                                frameParams.view[2][2]}};
        m_shadowRenderer->updateCascades(m_lightDirection, camForward, getFrameNum());
    }

    // Node selection for the CDLOD bodies, once for the whole frame: the shadow
    // cascades and the G-buffer must draw the same selection, not two that
    // disagree at a subdivision boundary. The cascades only group that one
    // selection, so a cascade draws a prefix of what the camera draws whole.
    // No volumes leaves the selection in one tier, which every pass draws whole.
    m_cdlodHandler->update(frameParams, m_shadowsEnabled ? m_shadowRenderer->getCasterVolumes()
                                                         : std::vector<Cylinder>{});

    unsigned int shadowMapTextureArray = 0;

    if (m_shadowsEnabled) {
        m_shadowRenderer->beginShadowPass();
        renderShadowPass();
        m_shadowRenderer->endShadowPass();
        
        shadowMapTextureArray = m_shadowRenderer->getShadowMapTextureArray();
    }
    
    // Begin deferred geometry pass
    m_deferredRenderer->beginGeometryPass();

    // Render geometry to G-buffer
    m_meshHandler->renderGeometry(frameParams);

    // Render opaque instanced geometry to G-buffer
    m_instanceHandler->renderGeometry(frameParams);

    // Render CDLOD bodies to G-buffer
    m_cdlodHandler->renderGeometry(frameParams);

    // End geometry pass and do lighting pass
    m_deferredRenderer->endGeometryPassAndRenderLighting(
        frameParams,
        m_shadowRenderer->getNumCascades(), // number of cascades
        m_shadowRenderer->getLightSpaceMatricesForViewSpace(frameParams.view),
        m_shadowRenderer->getCascadeBiasScales(), // cascade bias scales
        m_shadowRenderer->getCascadeOrthoSizes(),
        ShadowRenderer::k_cascadePush,    // how far each cascade centre is pushed
        shadowMapTextureArray,            // shadow map texture array
        m_shadowsEnabled                  // whether shadows are enabled
    );

    // Render transparent instances with Weighted Blended OIT: accumulate the
    // transparent fragments order-independently, then composite over the lit
    // scene left bound by the lighting pass.
    m_deferredRenderer->beginOITPass();
    m_instanceHandler->renderOIT(frameParams);

    // Ray-volume sub-pass: proxy-geometry volumetric effects accumulate into the
    // same WBOIT targets, sampling the opaque depth (no hardware depth test) and
    // drawing back faces so they survive the camera being inside them.
    m_deferredRenderer->beginRayVolumeSubPass();
    m_rayVolumeHandler->render(
        frameParams,
        m_deferredRenderer->getGBufferDepthTexture(),
        m_deferredRenderer->getSceneColorTexture(),
        m_deferredRenderer->getGBufferWidth(),
        m_deferredRenderer->getGBufferHeight());
    m_deferredRenderer->endRayVolumeSubPass();

    m_deferredRenderer->compositeOIT();

    // Overlay pass: in-world UI drawn forward over the finished 3D scene, on
    // top of opaques, transparents and ray volumes alike. Runs before
    // post-processing so overlays share the scene's Panini distortion and
    // world-ray hit tests keep matching what is on screen.
    m_deferredRenderer->beginOverlayPass();
    m_instanceHandler->renderOverlay(frameParams);
    m_deferredRenderer->endOverlayPass();

    // Post-processing pass: resample the finished scene to the screen
    // (Panini distortion + blue-noise dither).
    m_deferredRenderer->renderPostProcessing(frameParams);

    // Render 2D overlay. Zero-to-one and reversed, as every projection here is:
    // the overlay's own plane is z 0, which the swap puts at the near plane, in
    // front of whatever the frame left in the depth buffer.
    float aspectRatio = getScreenWidth() / (float)getScreenHeight();
    glm::mat4 projection2D = glm::orthoRH_ZO(-1.0f, 1.0f, -1.0f/aspectRatio,
                                             1.0f/aspectRatio, 1.0f, 0.0f);
    m_meshManager2D->render(projection2D);
}

void GraphicsEngine::renderShadowPass() {
    // Render depth to each cascade layer
    unsigned int numCascades = m_shadowRenderer->getNumCascades();
    const std::vector<glm::dmat4>& lightSpaceMatrices = m_shadowRenderer->getLightSpaceMatrices();
    
    for (unsigned int cascadeIndex = 0; cascadeIndex < numCascades; ++cascadeIndex) {
        // Bind the current cascade layer
        m_shadowRenderer->bindCascadeLayer(cascadeIndex);

        // Identity view, light projection for transform. Panini strengths stay at
        // their default 0: the shadow pass renders from the light, never distorted.
        FrameRenderParams depthParams{
            glm::dmat4(1.0),
            lightSpaceMatrices[cascadeIndex],
            getFrameNum(),
            m_currentInterpolationTimeStep,
            m_interpolationTimeRemainder,
            m_lightDirection,
            getCamPos()
        };
        depthParams.casterTier = cascadeIndex;

        // Render depth-only pass for shadow mapping. Only opaque objects cast shadows.
        // The pass culls front faces for all three, set once in beginShadowPass.
        m_meshHandler->renderDepth(
            depthParams, /*renderOpaque=*/true, /*renderTransparent=*/false);

        m_instanceHandler->renderDepth(depthParams);

        m_cdlodHandler->renderDepth(depthParams);
    }
}

void GraphicsEngine::setRenderParameters(uint64_t interpolationTimeStep, double timeRemainder) {
    m_currentInterpolationTimeStep = interpolationTimeStep;
    m_interpolationTimeRemainder = timeRemainder;
}

void GraphicsEngine::createMesh(int ssboIndex) {
    m_meshHandler->addMesh(ssboIndex);
}

void GraphicsEngine::updateMeshTransform(
    int meshId,
    const glm::dvec3& position,
    const glm::dvec3& velocity,
    const glm::dquat& orientation,
    const glm::dvec3& angVelAxis,
    double angVel,
    const glm::dvec3& centerOfRotation,
    const glm::dvec3& scale,
    uint64_t physicsTimeStep,
    double emissiveScalar)
{
    m_ssboManager->updateMeshTransform(
        meshId,
        position,
        velocity,
        orientation,
        angVelAxis,
        angVel,
        centerOfRotation,
        scale,
        physicsTimeStep,
        emissiveScalar
    );
}

void GraphicsEngine::removeMesh(int meshId) {
    m_meshHandler->removeMesh(meshId);
}

std::vector<uint32_t> GraphicsEngine::appendTrianglesToMesh(
    int meshIndex,
    const std::vector<glm::dvec3>* vertices,
    const std::vector<glm::dvec3>* normals,
    const std::vector<glm::dvec3>* tangents,
    const std::vector<glm::dvec2>* uvs,
    const std::vector<glm::dvec4>* colors,
    const std::vector<uint32_t>* textureUnits) {
    return m_meshHandler->appendTrianglesToMesh(
        meshIndex, vertices, normals, tangents, uvs, colors, textureUnits);
}

void GraphicsEngine::removeTrianglesFromMesh(int meshIndex,
                                             const std::vector<uint32_t>* triangleIds) {
    m_meshHandler->removeTrianglesFromMesh(meshIndex, triangleIds);
}

MeshHandler::Texture GraphicsEngine::createTexture(const std::string& texturePath) {
    return m_meshHandler->createTexture(texturePath);
}

std::weak_ptr<Geometry> GraphicsEngine::createInstanceGeometry(const std::string& modelPath,
                                                               RenderLayer layer) {
    return m_instanceHandler->createGeometry(modelPath, layer);
}

std::weak_ptr<Geometry> GraphicsEngine::createInstanceGeometry(
    const std::vector<GeometryVertex>& vertices, RenderLayer layer) {
    return m_instanceHandler->createGeometry(vertices, layer);
}

void GraphicsEngine::releaseInstanceGeometry(std::weak_ptr<Geometry> geometry) {
    m_instanceHandler->releaseGeometry(geometry);
}

int GraphicsEngine::createInstanceTexture(std::weak_ptr<Geometry> geometry,
                                          const std::string& texturePath) {
    return m_instanceHandler->createTexture(std::move(geometry), texturePath);
}

size_t GraphicsEngine::createRayVolumeMaterial(const std::string& bodySnippetPath) {
    return m_rayVolumeHandler->createMaterial(bodySnippetPath);
}

std::weak_ptr<Geometry> GraphicsEngine::createRayVolumeGeometry(const std::string& modelPath,
                                                                size_t materialIndex) {
    return m_rayVolumeHandler->createGeometry(modelPath, materialIndex);
}

void GraphicsEngine::releaseRayVolumeGeometry(std::weak_ptr<Geometry> geometry) {
    m_rayVolumeHandler->releaseGeometry(geometry);
}

std::weak_ptr<Instance> GraphicsEngine::addRayVolumeInstance(
    std::weak_ptr<Geometry> geometry, int ssboIndex, const glm::dvec4& color,
    const glm::dvec4& state, const glm::dvec4& velocity) {
    return m_rayVolumeHandler->addInstance(geometry, ssboIndex, color, state, velocity);
}

void GraphicsEngine::setRayVolumeInstanceValues(std::weak_ptr<Geometry> geometry,
                                                std::weak_ptr<Instance> instance,
                                                const glm::dvec4& state,
                                                const glm::dvec4& velocity) {
    m_rayVolumeHandler->setInstanceValues(geometry, instance, state, velocity);
}

void GraphicsEngine::removeRayVolumeInstance(std::weak_ptr<Geometry> geometry,
                                             std::weak_ptr<Instance> instance) {
    m_rayVolumeHandler->removeInstance(geometry, instance);
}

void GraphicsEngine::setSsaoEnabled(bool enabled) {
    SSAOSettings settings{m_deferredRenderer->getSSAOSettings()};
    settings.enabled = enabled;
    m_deferredRenderer->setSSAOSettings(settings);
}

bool GraphicsEngine::getSsaoEnabled() const {
    return m_deferredRenderer->getSSAOSettings().enabled;
}

std::weak_ptr<CdlodSurface> GraphicsEngine::createCdlodSurface(
    const std::string& snippetPath) {
    return m_cdlodHandler->createSurface(snippetPath);
}

void GraphicsEngine::removeCdlodSurface(std::weak_ptr<CdlodSurface> surface) {
    m_cdlodHandler->removeSurface(surface);
}

std::weak_ptr<CdlodInstance> GraphicsEngine::createCdlodInstance(
    int ssboIndex, const CdlodConfig& config, std::vector<CdlodPatchFrame> rootFrames,
    std::shared_ptr<const ICdlodPatchBounds> bounds, std::weak_ptr<CdlodSurface> surface) {
    return m_cdlodHandler->createInstance(ssboIndex, config, std::move(rootFrames),
                                          std::move(bounds), std::move(surface));
}

void GraphicsEngine::removeCdlodInstance(std::weak_ptr<CdlodInstance> instance) {
    m_cdlodHandler->removeInstance(instance);
}

void GraphicsEngine::setCdlodSurfaceTexture(std::weak_ptr<CdlodSurface> surface,
                                            const std::string& samplerName,
                                            const TextureSpec& spec) {
    m_cdlodHandler->setSurfaceTexture(std::move(surface), samplerName, spec);
}

void GraphicsEngine::setCdlodSurfaceCubeTexture(std::weak_ptr<CdlodSurface> surface,
                                                const std::string& samplerName,
                                                const CubeTextureSpec& spec) {
    m_cdlodHandler->setSurfaceCubeTexture(std::move(surface), samplerName, spec);
}

void GraphicsEngine::setCdlodSurfaceUniform(std::weak_ptr<CdlodSurface> surface,
                                            const std::string& name, float value) {
    m_cdlodHandler->setSurfaceUniform(std::move(surface), name, value);
}

void GraphicsEngine::setCdlodWireframe(bool wireframe) {
    m_cdlodHandler->setWireframe(wireframe);
}

bool GraphicsEngine::getCdlodWireframe() const {
    return m_cdlodHandler->getWireframe();
}

void GraphicsEngine::setCdlodPatchQuads(int patchQuads) {
    m_cdlodHandler->setPatchQuads(patchQuads);
}

int GraphicsEngine::getCdlodPatchQuads() const {
    return m_cdlodHandler->getPatchQuads();
}

int GraphicsEngine::loadModel(
    const std::string& modelPath,
    const std::string& colorTexturePath,
    const std::string& normalTexturePath,
    const std::string& materialTexturePath,
    bool ignoreTextureCoordinates,
    int* outColorTextureUnit,
    int* outNormalTextureUnit,
    int* outMaterialTextureUnit)
{
    int ssboIndex = m_ssboManager->allocateIndex();
    createMesh(ssboIndex);

    // Textures are resolved before the geometry now: their units are written
    // into the vertices, so they have to be known before the vertices exist.
    int32_t colorTextureUnit = -1;
    int32_t normalTextureUnit = -1;
    int32_t materialTextureUnit = -1;

    if (!colorTexturePath.empty()) {
        try {
            MeshHandler::Texture colorTexture = createTexture(colorTexturePath);
            colorTextureUnit = colorTexture.m_textureUnit;
            
            // Store the color texture unit in the output parameter if provided
            if (outColorTextureUnit != nullptr) {
                *outColorTextureUnit = colorTextureUnit;
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to load color texture: " << e.what() << std::endl;
        }
    }
    
    if (!normalTexturePath.empty()) {
        try {
            MeshHandler::Texture normalTexture = createTexture(normalTexturePath);
            normalTextureUnit = normalTexture.m_textureUnit;
            
            // Store the normal texture unit in the output parameter if provided
            if (outNormalTextureUnit != nullptr) {
                *outNormalTextureUnit = normalTextureUnit;
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to load normal texture: " << e.what() << std::endl;
        }
    }

    if (!materialTexturePath.empty()) {
        try {
            MeshHandler::Texture materialTexture = createTexture(materialTexturePath);
            materialTextureUnit = materialTexture.m_textureUnit;
            
            // Store the material texture unit in the output parameter if provided
            if (outMaterialTextureUnit != nullptr) {
                *outMaterialTextureUnit = materialTextureUnit;
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to load material texture: " << e.what() << std::endl;
        }
    }
    
    if (loadModelIntoMesh(ssboIndex, modelPath, ignoreTextureCoordinates,
                          MeshHandler::packTextureUnits(colorTextureUnit, normalTextureUnit,
                                                        materialTextureUnit, -1))
            .empty()) {
        removeMesh(ssboIndex);
        m_ssboManager->deallocateIndex(ssboIndex);
        return -1;
    }

    glm::dvec3 position(0.0, 0.0, 0.0);
    glm::dvec3 velocity(0.0, 0.0, 0.0);
    glm::dquat orientation(1.0, 0.0, 0.0, 0.0);
    glm::dvec3 angVelAxis(0.0, 1.0, 0.0);
    double angVel = 0.0;
    glm::dvec3 centerOfRotation(0.0, 0.0, 0.0);

    updateMeshTransform(
        ssboIndex,
        position,
        velocity,
        orientation,
        angVelAxis,
        angVel,
        centerOfRotation,
        glm::dvec3(1.0, 1.0, 1.0), // Default scale
        0,                          // Default physics time step
        1.0                         // Default emissive scalar
    );

    return ssboIndex;
}

std::vector<uint32_t> GraphicsEngine::loadModelIntoMesh(
    int meshId,
    const std::string& modelPath,
    bool ignoreTextureCoordinates,
    uint32_t textureUnits)
{
    if (!std::filesystem::exists(modelPath)) {
        std::cerr << "Model file not found: " << modelPath << std::endl;
        return {};
    }
    
    std::vector<uint32_t> allTriangleIds;
    
    try {
        std::vector<AssetMeshData> meshes;
        AssimpLoader::load(modelPath, &meshes, ignoreTextureCoordinates);
        
        for (const AssetMeshData& mesh : meshes) {
            std::vector<glm::dvec3> positions;
            std::vector<glm::dvec3> normals;
            std::vector<glm::dvec3> tangents;
            std::vector<glm::dvec2> uvs;
            
            if (!mesh.indices.empty()) {
                for (size_t i = 0; i < mesh.indices.size(); i++) {
                    int idx = mesh.indices[i];
                    
                    if (idx < static_cast<int>(mesh.positionsData.size())) {
                        const auto& pos = mesh.positionsData[idx];
                        const auto& norm = mesh.normalsData[idx];
                        const auto& tang = mesh.tangentsData[idx];
                        const auto& texUV = mesh.uvsData[idx];
                        
                        positions.push_back(glm::dvec3(pos[0], pos[1], pos[2]));
                        normals.push_back(glm::dvec3(norm[0], norm[1], norm[2]));
                        tangents.push_back(glm::dvec3(tang[0], tang[1], tang[2]));
                        uvs.push_back(glm::dvec2(texUV[0], texUV[1]));
                    }
                }
            } else {
                for (size_t i = 0; i < mesh.positionsData.size(); i++) {
                    const auto& pos = mesh.positionsData[i];
                    const auto& norm = mesh.normalsData[i];
                    const auto& tang = mesh.tangentsData[i];
                    const auto& texUV = mesh.uvsData[i];
                    
                    positions.push_back(glm::dvec3(pos[0], pos[1], pos[2]));
                    normals.push_back(glm::dvec3(norm[0], norm[1], norm[2]));
                    tangents.push_back(glm::dvec3(tang[0], tang[1], tang[2]));
                    uvs.push_back(glm::dvec2(texUV[0], texUV[1]));
                }
            }
            
            if (positions.empty()) {
                continue;
            }
            // One material for the whole model, written onto every vertex of it.
            const std::vector<uint32_t> vertexTextureUnits(positions.size(), textureUnits);

            std::vector<uint32_t> triangleIds = m_meshHandler->appendTrianglesToMesh(
                meshId,
                &positions,
                &normals,
                &tangents,
                &uvs,
                nullptr,
                &vertexTextureUnits
            );
            
            allTriangleIds.insert(allTriangleIds.end(), triangleIds.begin(), triangleIds.end());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading model: " << e.what() << std::endl;
        return {};
    }
    
    return allTriangleIds;
}

std::pair<bool, std::string> GraphicsEngine::reloadShaders() {
   std::string allErrors;
   bool allSuccess = true;
   
   auto [success1, error1] = m_meshHandler->reloadShaders();
   auto [success2, error2] = m_instanceHandler->reloadShaders();
   auto [success3, error3] = m_deferredRenderer->reloadShaders();
   auto [success4, error4] = m_shadowRenderer->reloadShaders();
   auto [success5, error5] = m_rayVolumeHandler->reloadShaders();
   auto [success6, error6] = m_cdlodHandler->reloadShaders();

   if (!success1) {
      allSuccess = false;
      allErrors += "MeshHandler: " + error1 + "\n";
   }
   if (!success2) {
      allSuccess = false;
      allErrors += "InstanceHandler: " + error2 + "\n";
   }
   if (!success3) {
      allSuccess = false;
      allErrors += "DeferredRenderer: " + error3 + "\n";
   }
   if (!success4) {
      allSuccess = false;
      allErrors += "ShadowRenderer: " + error4 + "\n";
   }
   if (!success5) {
      allSuccess = false;
      allErrors += "RayVolumeHandler: " + error5 + "\n";
   }
   if (!success6) {
      allSuccess = false;
      allErrors += "CdlodHandler: " + error6 + "\n";
   }

   return {allSuccess, allSuccess ?
      "All graphics engine shaders reloaded successfully" : allErrors};
}