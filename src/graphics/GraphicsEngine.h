// GraphicsEngine.h
#pragma once

#include "GraphicsEngineBase.h"
#include "GraphicsCallbacks.h"
#include "meshHandler/MeshHandler.h"
#include "deferredRenderer/DeferredRenderer.h"
#include "AssimpLoader.h"
#include "MeshManager2D/MeshManager2D.h"
#include "instanceHandler/InstanceHandler.h"
#include "SSBOManager.h"
#include "shadowRenderer/ShadowRenderer.h"
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class GraphicsEngine {
public:
    GraphicsEngine(
        int screenWidth = 800,
        int screenHeight = 600,
        const std::string& windowTitle = "Graphics Engine",
        size_t maxTriangles = 10000,
        size_t maxMeshes = 100,
        GraphicsEngineBase::Mode mode = GraphicsEngineBase::Mode::NONE
    );
    
    ~GraphicsEngine();

    GraphicsEngineBase* getGraphicsEngineBase() const { return m_graphicsEngineBase.get(); }

    // Access to GraphicsEngineBase properties
    GLFWwindow* getWindow() { return getGraphicsEngineBase()->m_window; }
    unsigned int getScreenWidth() { return getGraphicsEngineBase()->m_screen_width; }
    unsigned int getScreenHeight() { return getGraphicsEngineBase()->m_screen_height; }
    glm::dvec3& getCamPos() { return getGraphicsEngineBase()->m_camPos; }
    glm::dvec3& getCamVel() { return getGraphicsEngineBase()->m_camVel; }
    int getFrameRate() { return getGraphicsEngineBase()->m_frameRate; }
    glm::dquat& getCamOri() { return getGraphicsEngineBase()->m_camOri; }
    uint64_t getFrameNum() { return getGraphicsEngineBase()->m_frameNum; }
    double& getFieldOfView() { return getGraphicsEngineBase()->m_fieldOfView; }
    MouseHandler* getMouseHandler() { return getGraphicsEngineBase()->m_mouseHandler; }
    KeyboardHandler* getKeyboardHandler() { return getGraphicsEngineBase()->m_keyboardHandler; }

    // Graphics engine functionality
    void beginFrame();
    void render();
    void endFrame();
    void setTriangleRenderMode(bool useTriangles) { m_graphicsEngineBase->setTriangleRenderMode(useTriangles); }
    bool getTriangleRenderMode() { return m_graphicsEngineBase->getTriangleRenderMode(); }
    
    void createMesh(int ssboIndex);

    // Render parameter setting - for interpolation between states
    void setRenderParameters(uint64_t interpolationTimeStep, double timeRemainder);
    
    void updateMeshTransform(
        int meshId,
        const glm::dvec3& position,
        const glm::dvec3& velocity,
        const glm::dquat& orientation,
        const glm::dvec3& angVelAxis,
        double angVel,
        const glm::dvec3& centerOfRotation,
        const glm::dvec3& scale = glm::dvec3(1.0, 1.0, 1.0),
        int32_t colorTextureUnit = -1,
        int32_t normalTextureUnit = -1,
        int32_t materialTextureUnit = -1,
        uint64_t physicsTimeStep = 0,
        double emissiveScalar = 1.0,
        int32_t maskTextureUnit = -1
    );
    
    void removeMesh(int meshId);
    
    MeshHandler::Texture createTexture(const std::string& texturePath);
    
    int loadModel(
        const std::string& modelPath,
        const std::string& colorTexturePath = "",
        const std::string& normalTexturePath = "",
        const std::string& materialTexturePath = "",
        bool ignoreTextureCoordinates = false,
        int* outColorTextureUnit = nullptr,
        int* outNormalTextureUnit = nullptr,
        int* outMaterialTextureUnit = nullptr
    );
    
    std::vector<uint32_t> loadModelIntoMesh(
        int meshId,
        const std::string& modelPath,
        bool ignoreTextureCoordinates = false
    );

    // 2D mesh manager access
    MeshManager2D* getMeshManager2D() { return m_meshManager2D.get(); }

    // Instance handler access
    InstanceHandler* getInstanceHandler() { return m_instanceHandler.get(); }
    
    // Render parameter access
    std::pair<uint64_t, double> getRenderParameters() const { return {m_currentInterpolationTimeStep, m_interpolationTimeRemainder}; }

    void setShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }

    // Shader reloading
    std::pair<bool, std::string> reloadShaders();

    std::unique_ptr<SSBOManager> m_ssboManager;
    std::unique_ptr<MeshHandler> m_meshHandler;
    uint64_t currentTime{0};

private:
    std::shared_ptr<GraphicsEngineBase> m_graphicsEngineBase;

    std::unique_ptr<DeferredRenderer> m_deferredRenderer;
    std::unique_ptr<MeshManager2D> m_meshManager2D;
    std::unique_ptr<InstanceHandler> m_instanceHandler;
    std::unique_ptr<ShadowRenderer> m_shadowRenderer;

    // Render parameters for interpolation
    uint64_t m_currentInterpolationTimeStep = 0;
    double m_interpolationTimeRemainder = 0.0;

    // Shadow parameters
    bool m_shadowsEnabled = true;
    glm::dvec3 m_lightDirection = glm::normalize(glm::dvec3(1.0, -1.0, -1.0));
    
    // Helper method for shadow rendering
    void renderShadowPass();

    // Internal rendering method
    void renderScene();
};