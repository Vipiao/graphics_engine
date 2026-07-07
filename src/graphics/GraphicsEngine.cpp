// GraphicsEngine.cpp
#include "GraphicsEngine.h"
#include "GraphicsEngineBase.h"
#include "GraphicsCallbacks.h"
#include "SSBOManager.h"
#include "deferredRenderer/DeferredRenderer.h"
#include "MeshManager2D/MeshManager2D.h"
#include "instanceHandler/InstanceHandler.h"
#include "shadowRenderer/ShadowRenderer.h"
#include "utils/HashFunctions.h"
#include <iostream>
#include <filesystem>

GraphicsEngine::GraphicsEngine(
    int screenWidth,
    int screenHeight,
    const std::string& windowTitle,
    size_t maxTriangles,
    size_t maxMeshes,
    GraphicsEngineBase::Mode mode)
{
    // Create GraphicsEngineBase
    m_graphicsEngineBase = std::make_shared<GraphicsEngineBase>(mode);
    
    // Configure window
    m_graphicsEngineBase->m_screen_width = screenWidth;
    m_graphicsEngineBase->m_screen_height = screenHeight;
    
    if (!windowTitle.empty()) {
        glfwSetWindowTitle(m_graphicsEngineBase->m_window, windowTitle.c_str());
    }

    // Create SSBO manager and pass to mesh handler
    m_ssboManager = std::make_unique<SSBOManager>(maxMeshes);
    m_meshHandler = std::make_unique<MeshHandler>(maxTriangles, m_ssboManager.get());

    // Create deferred renderer
    m_deferredRenderer = std::make_unique<DeferredRenderer>();
    m_deferredRenderer->setupGBuffer(screenWidth, screenHeight);

    // Create instance handler
    m_instanceHandler = std::make_unique<InstanceHandler>(m_ssboManager.get());

    // Create 2D mesh manager
    m_meshManager2D = std::make_unique<MeshManager2D>(1000);

    // Create shadow renderer
    m_shadowRenderer = std::make_unique<ShadowRenderer>();
    //m_shadowRenderer->setupShadowMaps(8192, 8192, 3, {50.0, 200.0, 800.0});
    m_shadowRenderer->setupShadowMaps(4096, 4096, 4, {8., 32.0, 128.0, 512.0});
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
    m_instanceHandler.reset();
    m_shadowRenderer.reset();
    m_deferredRenderer.reset();
    m_meshHandler.reset();
    m_ssboManager.reset();
}

void GraphicsEngine::beginFrame() {
    m_graphicsEngineBase->clearScreen();
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

    unsigned int shadowMapTextureArray = 0;
    std::vector<glm::dmat4> cascadeMatricesViewSpace;
    
    if (m_shadowsEnabled) {
        // Render shadow map
        m_shadowRenderer->beginShadowPass(m_lightDirection, getCamPos(), getFrameNum());
        renderShadowPass();
        m_shadowRenderer->endShadowPass();
        
        shadowMapTextureArray = m_shadowRenderer->getShadowMapTextureArray();
    }
    
    // Begin deferred geometry pass
    m_deferredRenderer->beginGeometryPass();

    // Render geometry to G-buffer
    m_meshHandler->renderGeometry(frameParams);

    // Render instanced geometry to G-buffer
    m_instanceHandler->renderGeometry(
        frameParams, /*renderOpaque=*/true, /*renderTransparent=*/false);

    // End geometry pass and do lighting pass
    m_deferredRenderer->endGeometryPassAndRenderLighting(
        frameParams,
        m_shadowRenderer->getNumCascades(), // number of cascades
        m_shadowRenderer->getLightSpaceMatricesForViewSpace(frameParams.view),
        m_shadowRenderer->getCascadeBiasScales(), // cascade bias scales
        m_shadowRenderer->getCascadeOrthoSizes(),
        shadowMapTextureArray,            // shadow map texture array
        m_shadowsEnabled                  // whether shadows are enabled
    );

    // Render transparent instances with Weighted Blended OIT: accumulate the
    // transparent fragments order-independently, then composite over the lit
    // scene left bound by the lighting pass.
    m_deferredRenderer->beginOITPass();
    m_instanceHandler->renderOIT(frameParams);
    m_deferredRenderer->compositeOIT();

    // Post-processing pass: resample the finished scene to the screen
    // (Panini distortion + blue-noise dither).
    m_deferredRenderer->renderPostProcessing(frameParams);

    // Render 2D overlay
    float aspectRatio = getScreenWidth() / (float)getScreenHeight();
    glm::mat4 projection2D = glm::ortho(-1.0f, 1.0f, -1.0f/aspectRatio, 1.0f/aspectRatio, 0.0f, 1.0f);
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

        // Render depth-only pass for shadow mapping. Only opaque objects cast shadows.
        m_meshHandler->renderDepth(
            depthParams, /*renderOpaque=*/true, /*renderTransparent=*/false);

        m_instanceHandler->renderDepth(
            depthParams, /*renderOpaque=*/true, /*renderTransparent=*/false);
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
    int32_t colorTextureUnit,
    int32_t normalTextureUnit,
    int32_t materialTextureUnit,
    uint64_t physicsTimeStep,
    double emissiveScalar,
    int32_t maskTextureUnit)
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
        colorTextureUnit,
        normalTextureUnit,
        materialTextureUnit,
        physicsTimeStep,
        emissiveScalar,
        maskTextureUnit
    );
}

void GraphicsEngine::removeMesh(int meshId) {
    m_meshHandler->removeMesh(meshId);
}

MeshHandler::Texture GraphicsEngine::createTexture(const std::string& texturePath) {
    return m_meshHandler->createTexture(texturePath);
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

    if (loadModelIntoMesh(ssboIndex, modelPath, ignoreTextureCoordinates).empty()) {
        removeMesh(ssboIndex);
        m_ssboManager->deallocateIndex(ssboIndex);
        return -1;
    }
    
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
        colorTextureUnit,
        normalTextureUnit,
        materialTextureUnit,
        0,                          // Default physics time step
        1.0                         // Default emissive scalar
    );

    return ssboIndex;
}

std::vector<uint32_t> GraphicsEngine::loadModelIntoMesh(
    int meshId,
    const std::string& modelPath,
    bool ignoreTextureCoordinates)
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
            std::vector<uint32_t> triangleIds = m_meshHandler->appendTrianglesToMesh(
                meshId,
                &positions,
                &normals,
                &tangents,
                &uvs
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
   
   return {allSuccess, allSuccess ?
      "All graphics engine shaders reloaded successfully" : allErrors};
}