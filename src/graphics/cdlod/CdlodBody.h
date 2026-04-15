// CdlodBody.h
#pragma once

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <glad/glad.h>
#include "CdlodConfig.h"
#include "CdlodTree.h"

class CdlodPatchGeometry;

/**
 * @brief One subdivided body: its quadtree, and the buffers that draw its leaves.
 *
 * The body's world transform is not stored here. It lives in the shared
 * SSBO under m_ssboIndex, so anything else placed on this body -- instanced
 * geometry, other renderers -- can cite the same index and move with it.
 */
class CdlodBody {
public:
    CdlodBody(int ssboIndex, const CdlodConfig& config, size_t surfaceIndex,
              const CdlodPatchGeometry& patchGeometry);
    ~CdlodBody();

    CdlodBody(const CdlodBody&) = delete;
    CdlodBody& operator=(const CdlodBody&) = delete;

    // Re-selects the visible nodes and uploads them. Once per frame, before the
    // passes that draw them, so the shadow cascades and the G-buffer all draw
    // the same selection. cameraBodyPosition is the camera in this body's own
    // frame, at the pose the vertex stage will draw the body at this frame.
    void update(const glm::dvec3& cameraBodyPosition);

    GLuint getVAO() const { return m_VAO; }
    GLsizei getInstanceCount() const { return static_cast<GLsizei>(m_instances.size()); }
    const CdlodConfig& getConfig() const { return m_config; }
    int getSsboIndex() const { return m_ssboIndex; }
    size_t getSurfaceIndex() const { return m_surfaceIndex; }
    // The camera this frame's selection was made against. The vertex stage
    // morphs against the same value, so the morph cannot complete at a different
    // distance than the one the merge was decided at.
    const glm::dvec3& getCameraBodyPosition() const { return m_cameraBodyPosition; }

private:
    // Hard floor on how far the tree may subdivide. Nothing configures the depth:
    // a node stops splitting once the camera is further than m_lodRangeFactor of
    // its edge lengths away, so the camera's own height above the surface bounds
    // the depth on its own, and this only catches a camera that reaches the
    // surface itself. Node offsets reach the resolution limit of the 32-bit
    // instance record around here, which is what sets the value.
    static constexpr int k_maxDepthLimit{16};

    int m_ssboIndex{-1};
    CdlodConfig m_config{};
    // Which injected surface shape draws this body; an index into the handler's
    // surfaces, since the programs are shared between every body using them.
    size_t m_surfaceIndex{0};
    CdlodTree m_tree;
    glm::dvec3 m_cameraBodyPosition{0.0};

    GLuint m_VAO{0};
    GLuint m_instanceVBO{0};
    size_t m_instanceCapacity{0};
    std::vector<CdlodPatchInstance> m_instances;

    // Builds the VAO: the shared patch index buffer plus this body's own
    // instance attributes, all at divisor 1. No per-vertex attributes exist.
    void setupOpenGL(const CdlodPatchGeometry& patchGeometry);
    void uploadInstances();
};
