// CdlodBody.cpp
#include "CdlodBody.h"
#include "CdlodPatchGeometry.h"
#include <algorithm>

CdlodBody::CdlodBody(int ssboIndex, const CdlodConfig& config, size_t surfaceIndex,
                     const CdlodPatchGeometry& patchGeometry)
    : m_ssboIndex{ssboIndex}, m_config{config}, m_surfaceIndex{surfaceIndex},
      m_tree{CdlodTreeParams{config.m_halfExtent, k_maxDepthLimit,
                             config.m_lodRangeFactor}} {
    setupOpenGL(patchGeometry);
}

CdlodBody::~CdlodBody() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_instanceVBO != 0) glDeleteBuffers(1, &m_instanceVBO);
}

void CdlodBody::setupOpenGL(const CdlodPatchGeometry& patchGeometry) {
    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    // The shared index buffer is the whole of the per-vertex input: the vertex
    // stage reconstructs the grid coordinate from gl_VertexID.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, patchGeometry.getIndexBuffer());

    glGenBuffers(1, &m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

    // Locations start at 4, leaving 0..3 free so the layout stays readable
    // against the instanced-geometry shaders, and keeping ssboIndex at 8 where
    // those shaders also have it.
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(CdlodPatchInstance),
                          (void*)offsetof(CdlodPatchInstance, m_nodeOffset));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(CdlodPatchInstance),
                          (void*)offsetof(CdlodPatchInstance, m_nodeSize));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glVertexAttribIPointer(6, 1, GL_INT, sizeof(CdlodPatchInstance),
                           (void*)offsetof(CdlodPatchInstance, m_faceIndex));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    glVertexAttribIPointer(7, 1, GL_INT, sizeof(CdlodPatchInstance),
                           (void*)offsetof(CdlodPatchInstance, m_nodeLevel));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);

    glVertexAttribIPointer(8, 1, GL_INT, sizeof(CdlodPatchInstance),
                           (void*)offsetof(CdlodPatchInstance, m_ssboIndex));
    glEnableVertexAttribArray(8);
    glVertexAttribDivisor(8, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void CdlodBody::update(const glm::dvec3& cameraBodyPosition) {
    m_cameraBodyPosition = cameraBodyPosition;
    m_tree.updateAndSelect(cameraBodyPosition, m_ssboIndex, m_instances);
    uploadInstances();
}

void CdlodBody::uploadInstances() {
    if (m_instances.empty()) return;

    if (m_instances.size() > m_instanceCapacity) {
        m_instanceCapacity = std::max(m_instances.size(), m_instanceCapacity * 2);
    }

    // The whole selection is rewritten every frame, so the buffer is always
    // reallocated at capacity rather than patched. That doubles as orphaning:
    // the previous frame's draw may still be reading the old storage, and
    // discarding it lets the driver hand back fresh memory instead of waiting
    // for that draw to retire.
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, m_instanceCapacity * sizeof(CdlodPatchInstance),
                 nullptr, GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, m_instances.size() * sizeof(CdlodPatchInstance),
                    m_instances.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
