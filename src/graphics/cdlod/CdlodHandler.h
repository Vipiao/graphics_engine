// CdlodHandler.h
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "../ShaderProgram.h"
#include "../FrameRenderParams.h"
#include "CdlodConfig.h"

class SSBOManager;
class CdlodBody;
class CdlodPatchGeometry;

/**
 * @brief Renderer for continuous-distance level-of-detail bodies.
 *
 * A body is a cube subdivided by a quadtree per face, drawn as one instanced
 * call over the selected nodes. Nothing here knows what the body represents;
 * an asteroid, a planet and a debug cube are the same object at this level, and
 * what makes them differ is the displacement injected into the vertex stage.
 *
 * Bodies are placed in the world through the shared transform SSBO, like every
 * other instanced renderer: the caller allocates an index and drives it with
 * SSBOManager::updateMeshTransform, so anything else riding on the same body can
 * cite that index and move with it.
 *
 * Draws opaquely into the G-buffer and casts shadows through the ordinary depth
 * pass. Pass state belongs to DeferredRenderer and ShadowRenderer; this binds
 * programs and issues draws.
 */
class CdlodHandler {
public:
    explicit CdlodHandler(SSBOManager* ssboManager);
    ~CdlodHandler();

    CdlodHandler(const CdlodHandler&) = delete;
    CdlodHandler& operator=(const CdlodHandler&) = delete;

    // Build the vertex programs from the patch scaffold with the given surface
    // snippet injected. Empty snippetPath uses the built-in sphere. Returns a
    // surface index; index 0 is the built-in one and always exists.
    size_t createSurface(const std::string& snippetPath = "");

    // The returned handle stays valid until removeBody; it is not an index into
    // any array the caller can observe.
    size_t createBody(int ssboIndex, const CdlodConfig& config, size_t surfaceIndex = 0);
    void removeBody(size_t bodyHandle);

    // Re-selects every body's visible nodes for this frame. Must run before the
    // passes below so they draw one consistent selection.
    void update(const FrameRenderParams& params);

    // G-buffer pass.
    void renderGeometry(const FrameRenderParams& params);
    // Shadow-cascade depth pass. Always solid, whatever the debug view is set to.
    void renderDepth(const FrameRenderParams& params);

    // Debug view: draws the patches as wireframe and tints each quadtree level,
    // so the state of the subdivision is visible. Scoped to this renderer, unlike
    // GraphicsEngineBase's global triangle mode.
    void setWireframe(bool wireframe) { m_wireframe = wireframe; }
    bool getWireframe() const { return m_wireframe; }

    std::pair<bool, std::string> reloadShaders();

private:
    // One injected surface shape and the programs that draw it. Both stages
    // carry the same snippet: the depth pass has to displace a vertex to the
    // exact place the G-buffer pass does, or a body's shadow parts company with
    // the body. Held by pointer so a rebuild can swap a whole surface at once.
    struct Surface {
        std::string snippetPath;
        ShaderProgram gbufferProgram;
        ShaderProgram depthProgram;
    };

    SSBOManager* m_ssboManager{nullptr};
    std::unique_ptr<CdlodPatchGeometry> m_patchGeometry;
    // Removed bodies leave a null slot behind so live handles keep their meaning.
    std::vector<std::unique_ptr<CdlodBody>> m_bodies;
    std::vector<std::unique_ptr<Surface>> m_surfaces;

    bool m_wireframe{false};

    // The stage at stagePath with the surface snippet spliced in at its marker.
    // Throws if the snippet or the marker is missing.
    static std::string buildStageSource(const char* stagePath,
                                        const std::string& snippetPath);
    static void buildSurfacePrograms(Surface& surface);

    // Draws every live body, grouped by the surface that shapes it, in the
    // caller's current render state.
    void renderAllSurfaces(const FrameRenderParams& params, bool isGeometryPass);
    // Per-frame uniforms and the instanced draw for one surface's bodies.
    void renderSurfaceBodies(Surface& surface, size_t surfaceIndex,
                             const FrameRenderParams& params, bool isGeometryPass);
};
