// GraphicsEngine.h
#pragma once

#include "GraphicsEngineBase.h"
// MeshHandler.h is needed for the nested MeshHandler::Texture return type.
#include "meshHandler/MeshHandler.h"
// InstancedGeometry.h provides RenderLayer and the Geometry/Instance handle
// types callers use with the instanced-geometry API below.
#include "instancedGeometry/InstancedGeometry.h"
// CdlodConfig.h carries the creation parameters of a CDLOD body by value.
#include "cdlod/CdlodConfig.h"
// CdlodPatchBounds.h carries the caller's shape hook and the patch frame type.
#include "cdlod/CdlodPatchBounds.h"
// Texture2D.h provides TextureSpec, which callers fill in to hand a surface
// generated image data.
#include "Texture2D.h"
#include "TextureCube.h"
// filesystem carries the recording path the constructor passes through.
#include <filesystem>

class TextureStore;
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Subsystems are held by pointer — include their headers where you use them.
class DeferredRenderer;
class RayVolumeHandler;
class MeshManager2D;
class InstanceHandler;
class SSBOManager;
class ShadowRenderer;
class CdlodHandler;
struct CdlodInstance;
struct CdlodSurface;

class TimeHandler;

class GraphicsEngine {
public:
    GraphicsEngine(
        TimeHandler* timeHandler,
        int screenWidth = 800,
        int screenHeight = 600,
        const std::string& windowTitle = "Graphics Engine",
        size_t maxTriangles = 10000,
        size_t maxMeshes = 100,
        GraphicsEngineBase::Mode mode = GraphicsEngineBase::Mode::NONE,
        // Where the mouse and keyboard journals live. Passed through rather than
        // defaulted deeper, so one caller places every stream of a session
        // together.
        const std::filesystem::path& controlRecordingDir = "recording_mouse_keyboard"
    );
    
    ~GraphicsEngine();

    GraphicsEngineBase* getGraphicsEngineBase() const { return m_graphicsEngineBase.get(); }

    // Access to GraphicsEngineBase properties
    GLFWwindow* getWindow() { return getGraphicsEngineBase()->m_window; }
    unsigned int getScreenWidth() { return getGraphicsEngineBase()->m_screen_width; }
    unsigned int getScreenHeight() { return getGraphicsEngineBase()->m_screen_height; }
    glm::dvec3& getCamPos() { return getGraphicsEngineBase()->m_camPos; }
    glm::dvec3& getCamVel() { return getGraphicsEngineBase()->m_camVel; }
    // Frames per second actually achieved. This is the rate to convert with:
    // a windowed surface is routinely throttled below the monitor's mode, and
    // a nominal rate makes every seconds-to-frames conversion run at the ratio
    // between the two. Measured per frame in beginFrame.
    double getFrameRate() { return getGraphicsEngineBase()->m_measuredFrameRate; }
    // Start of the current frame, sampled once in beginFrame. Anything pacing
    // off the frame reads it here rather than sampling the clock again.
    std::chrono::time_point<std::chrono::high_resolution_clock> getFrameStartTime() {
        return getGraphicsEngineBase()->m_frameStartTime;
    }
    // The monitor mode's refresh rate; for pacing against the display only.
    int getMonitorRefreshRate() { return getGraphicsEngineBase()->m_monitorRefreshRate; }
    glm::dquat& getCamOri() { return getGraphicsEngineBase()->m_camOri; }
    uint64_t getFrameNum() { return getGraphicsEngineBase()->m_frameNum; }
    double& getFieldOfView() { return getGraphicsEngineBase()->m_fieldOfView; }
    // Panini projection strengths. 0 = standard rectilinear (off), 1 = max distortion.
    double& getPaniniHorizontal() { return getGraphicsEngineBase()->m_paniniHorizontal; }
    double& getPaniniVertical() { return getGraphicsEngineBase()->m_paniniVertical; }
    // Derived output zoom of the Panini post pass; pass to screen-anchor projection.
    double getPaniniFitScale() { return getGraphicsEngineBase()->getPaniniFitScale(); }
    // Blue-noise dither amplitude of the post-processing pass, in color
    // units. 0 = off; 1.0 / 255.0 covers one 8-bit quantization step.
    double& getDitherStrength() { return getGraphicsEngineBase()->m_ditherStrength; }
    MouseHandler* getMouseHandler() { return getGraphicsEngineBase()->m_mouseHandler; }
    KeyboardHandler* getKeyboardHandler() { return getGraphicsEngineBase()->m_keyboardHandler; }

    // Graphics engine functionality
    void beginFrame();
    void render();
    void endFrame();
    void setWindowPos(int xPos, int yPos) { m_graphicsEngineBase->setWindowPos(xPos, yPos); }
    void toggleFullscreen() { m_graphicsEngineBase->toggleFullscreen(); }
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
        uint64_t physicsTimeStep = 0,
        double emissiveScalar = 1.0
    );
    
    void removeMesh(int meshId);

    // Triangle-mesh editing for a grid mesh (forwards to the internal MeshHandler).
    std::vector<uint32_t> appendTrianglesToMesh(
        int meshIndex,
        const std::vector<glm::dvec3>* vertices,
        const std::vector<glm::dvec3>* normals,
        const std::vector<glm::dvec3>* tangents,
        const std::vector<glm::dvec2>* uvs,
        const std::vector<double>* occlusionFactors = nullptr,
        const std::vector<glm::dvec4>* colors = nullptr,
        const std::vector<uint32_t>* textureUnits = nullptr);
    void removeTrianglesFromMesh(int meshIndex, const std::vector<uint32_t>* triangleIds);

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
    
    // textureUnits is packed by MeshHandler::packTextureUnits and written onto
    // every vertex of the model; the default leaves it untextured.
    std::vector<uint32_t> loadModelIntoMesh(
        int meshId,
        const std::string& modelPath,
        bool ignoreTextureCoordinates = false,
        uint32_t textureUnits = MeshHandler::noTextureUnits()
    );

    // 2D mesh manager access
    MeshManager2D* getMeshManager2D() { return m_meshManager2D.get(); }

    // Instanced-geometry resources. Thin forwarders over the internal
    // InstanceHandler: the render layer selects the pass that draws the
    // geometry (opaque G-buffer, transparent OIT, or overlay). The returned
    // Geometry/Instance handles are the interface for per-instance updates
    // (addInstance, updateInstanceInBuffer, ...).
    std::weak_ptr<Geometry> createInstanceGeometry(
        const std::string& modelPath, RenderLayer layer = RenderLayer::Opaque);
    // For geometry generated at runtime: a triangle list in the standard vertex
    // layout, uploaded once and instanced like any loaded model.
    std::weak_ptr<Geometry> createInstanceGeometry(
        const std::vector<GeometryVertex>& vertices, RenderLayer layer = RenderLayer::Opaque);
    void releaseInstanceGeometry(std::weak_ptr<Geometry> geometry);
    // Registers a texture against the geometry that will draw it, and returns the
    // unit to pass to that geometry's addInstance. The unit is scoped to the
    // geometry: the same texture on two geometries gets a unit in each, while the
    // pixels are loaded once and shared.
    int createInstanceTexture(std::weak_ptr<Geometry> geometry,
                              const std::string& texturePath);

    // Ray-volume resources: proxy-geometry volumetric effects composited with the
    // transparency (OIT) pass. A material is an injected shading body; geometries
    // are proxy meshes for a material; instances carry the animated value vector
    // (state + velocity) the shader integrates.
    size_t createRayVolumeMaterial(const std::string& bodySnippetPath = "");
    std::weak_ptr<Geometry> createRayVolumeGeometry(const std::string& modelPath,
                                                    size_t materialIndex);
    void releaseRayVolumeGeometry(std::weak_ptr<Geometry> geometry);
    std::weak_ptr<Instance> addRayVolumeInstance(
        std::weak_ptr<Geometry> geometry, int ssboIndex,
        const glm::dvec4& color = glm::dvec4(1.0),
        const glm::dvec4& state = glm::dvec4(0.0),
        const glm::dvec4& velocity = glm::dvec4(0.0));
    void setRayVolumeInstanceValues(std::weak_ptr<Geometry> geometry,
                                    std::weak_ptr<Instance> instance,
                                    const glm::dvec4& state, const glm::dvec4& velocity);
    void removeRayVolumeInstance(std::weak_ptr<Geometry> geometry,
                                 std::weak_ptr<Instance> instance);

    // CDLOD instances: cube-quadtree surfaces subdivided by distance, drawn
    // opaque and shadow casting. An instance is placed through the shared
    // transform SSBO, so the caller allocates an index
    // (m_ssboManager->allocateIndex()) and drives it with updateMeshTransform,
    // exactly like a mesh or an instanced geometry. That lets anything else on
    // the same body cite the same index.
    //
    // A surface is the GLSL that shapes the body: it maps a point on the base
    // sphere to the final surface, and returns the normal there. Empty path
    // gives the bare sphere. Instances sharing a surface share its programs.
    // There is no geometry level in between, unlike createInstanceGeometry: a
    // subdivided body has no mesh to share, only its own quadtree.
    std::weak_ptr<CdlodSurface> createCdlodSurface(const std::string& snippetPath);
    // Drops the engine's reference; the surface lives on until its last instance
    void removeCdlodSurface(std::weak_ptr<CdlodSurface> surface);
    // rootFrames are the quadtree's starting squares, in the body's own frame;
    // bounds says where any frame derived from them renders. With the snippet,
    // the three are the body's shape -- the engine has no other notion of it.
    std::weak_ptr<CdlodInstance> createCdlodInstance(
        int ssboIndex, const CdlodConfig& config,
        std::vector<CdlodPatchFrame> rootFrames,
        std::shared_ptr<const ICdlodPatchBounds> bounds,
        std::weak_ptr<CdlodSurface> surface);
    void removeCdlodInstance(std::weak_ptr<CdlodInstance> instance);
    // Data for the surface's snippet to read, under names the snippet declares.
    // Set or replace by name, and shared by every instance wearing the surface.
    // What a texture means, and how the snippet projects a body-space point onto
    // it, are the caller's to decide: the engine only makes it reachable.
    void setCdlodSurfaceTexture(std::weak_ptr<CdlodSurface> surface,
                                const std::string& samplerName, const TextureSpec& spec);
    // The same for a map the snippet reads by direction, which is what a field
    // covering a whole body once wants: no repeat to hide and no seam to close.
    void setCdlodSurfaceCubeTexture(std::weak_ptr<CdlodSurface> surface,
                                    const std::string& samplerName,
                                    const CubeTextureSpec& spec);
    void setCdlodSurfaceUniform(std::weak_ptr<CdlodSurface> surface,
                                const std::string& name, float value);
    // Debug view of the subdivision: wireframe patches tinted by quadtree level.
    // Scoped to the CDLOD instances, unlike setTriangleRenderMode above.
    void setCdlodWireframe(bool wireframe);
    bool getCdlodWireframe() const;
    // Quads along one patch edge, and so the triangles a selected patch costs.
    // One decision for every CDLOD body, since they share one index buffer, and
    // safe to change at any time. Must be even; throws if it cannot be used.
    void setCdlodPatchQuads(int patchQuads);
    int getCdlodPatchQuads() const;

    // Render parameter access
    std::pair<uint64_t, double> getRenderParameters() const { return {m_currentInterpolationTimeStep, m_interpolationTimeRemainder}; }

    void setShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }

    // Screen-space ambient occlusion, which reads the depth buffer rather than
    // the shaded normals. Worth turning off when a surface's geometry and its
    // normals deliberately disagree, since it will shade the geometry.
    void setSsaoEnabled(bool enabled);
    bool getSsaoEnabled() const;

    // Shader reloading
    std::pair<bool, std::string> reloadShaders();

    std::unique_ptr<SSBOManager> m_ssboManager;
    uint64_t currentTime{0};

private:
    std::shared_ptr<GraphicsEngineBase> m_graphicsEngineBase;

    // Declared before the renderers so it outlives every handle into it.
    std::unique_ptr<TextureStore> m_textureStore;

    std::unique_ptr<DeferredRenderer> m_deferredRenderer;
    std::unique_ptr<MeshManager2D> m_meshManager2D;
    std::unique_ptr<MeshHandler> m_meshHandler;
    std::unique_ptr<InstanceHandler> m_instanceHandler;
    std::unique_ptr<RayVolumeHandler> m_rayVolumeHandler;
    std::unique_ptr<CdlodHandler> m_cdlodHandler;
    std::unique_ptr<ShadowRenderer> m_shadowRenderer;

    // Render parameters for interpolation
    uint64_t m_currentInterpolationTimeStep = 0;
    double m_interpolationTimeRemainder = 0.0;

    // Shadow parameters
    bool m_shadowsEnabled = true;
    // The direction the light travels, not the direction toward it: the lighting
    // stage negates it. +y puts the source below, lighting the face a body parked
    // along +y turns toward the camera.
    glm::dvec3 m_lightDirection = glm::normalize(glm::dvec3(1.0, 1.0, -1.0));
    
    // Helper method for shadow rendering
    void renderShadowPass();

    // Internal rendering method
    void renderScene();
};