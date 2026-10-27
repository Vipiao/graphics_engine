// ScatterMeshes.h
#pragma once

#include <cstdint>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "ScatterConfig.h"

// The mesh fields of a DrawElementsIndirectCommand.
struct ScatterMeshRange {
    uint32_t m_firstIndex{0};
    uint32_t m_indexCount{0};
    // Added to every index, so each mesh's indices count from 0
    int32_t m_baseVertex{0};
};

/**
 * @brief Every mesh of a set in one vertex and one index buffer, so one
 * multi-draw draws them all.
 *
 * Numbered kind by kind, LODs nearest first (meshIndex), then one quad per kind
 * with an impostor (impostorMesh). Vertices are GeometryVertex at locations 0 to 3.
 */
class ScatterMeshes {
public:
    // Throws if a model cannot be loaded or has no meshes.
    explicit ScatterMeshes(const std::vector<ScatterKind>& kinds);
    ~ScatterMeshes();

    ScatterMeshes(const ScatterMeshes&) = delete;
    ScatterMeshes& operator=(const ScatterMeshes&) = delete;

    static constexpr uint32_t k_noMesh{0xFFFFFFFFu};

    uint32_t meshIndex(uint32_t kind, uint32_t lod) const;
    // Or k_noMesh
    uint32_t impostorMesh(uint32_t kind) const { return m_impostorMeshOfKind[kind]; }
    uint32_t getMeshCount() const { return static_cast<uint32_t>(m_ranges.size()); }
    // The LOD meshes come first, the impostor quads after
    uint32_t getLodMeshCount() const { return m_lodMeshCount; }
    const ScatterMeshRange& getRange(uint32_t meshIndex) const { return m_ranges[meshIndex]; }

    // Around the item's origin, over all of the kind's LODs, before scale
    float getBoundingRadius(uint32_t kind) const { return m_boundingRadii[kind]; }

    // A tight sphere around one mesh: the middle of its box, and its farthest
    // vertex from there
    glm::vec3 getCentre(uint32_t meshIndex) const { return m_centres[meshIndex]; }
    float getRadius(uint32_t meshIndex) const { return m_radii[meshIndex]; }

    GLuint getVAO() const { return m_VAO; }

private:
    // Per mesh
    std::vector<ScatterMeshRange> m_ranges;
    std::vector<glm::vec3> m_centres;
    std::vector<float> m_radii;
    // Per kind
    std::vector<uint32_t> m_firstMeshOfKind;
    std::vector<uint32_t> m_impostorMeshOfKind;
    std::vector<float> m_boundingRadii;
    uint32_t m_lodMeshCount{0};

    GLuint m_VAO{0};
    GLuint m_vertexBuffer{0};
    GLuint m_indexBuffer{0};
};
