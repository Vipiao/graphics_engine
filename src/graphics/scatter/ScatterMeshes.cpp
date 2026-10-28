// ScatterMeshes.cpp
#include "ScatterMeshes.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include "../AssimpLoader.h"
#include "../instancedGeometry/InstancedGeometry.h"

namespace {

// The texture layer of the material named name, or of the unnamed one
uint32_t materialLayer(const std::vector<ScatterMaterial>& materials, const std::string& name,
                       const std::string& modelPath) {
    std::optional<uint32_t> unnamed;
    for (uint32_t layer{0}; layer < materials.size(); ++layer) {
        if (materials[layer].m_name == name) return layer;
        if (materials[layer].m_name.empty()) unnamed = layer;
    }
    if (!unnamed) {
        throw std::runtime_error("Scatter model " + modelPath + " has material " + name +
                                 ", which its kind does not name");
    }
    return *unnamed;
}

// Appends a model's sub-meshes as one mesh, indices counting from its first
// vertex.
ScatterMeshRange appendModel(const std::string& modelPath,
                             const std::vector<ScatterMaterial>& materials,
                             std::vector<GeometryVertex>& vertices, std::vector<float>& layers,
                             std::vector<uint32_t>& indices) {
    std::vector<AssetMeshData> subMeshes;
    AssimpLoader::load(modelPath, &subMeshes, false);
    if (subMeshes.empty()) {
        throw std::runtime_error("No meshes found in scatter model: " + modelPath);
    }

    ScatterMeshRange range;
    range.m_firstIndex = static_cast<uint32_t>(indices.size());
    range.m_baseVertex = static_cast<int32_t>(vertices.size());

    uint32_t meshVertexCount{0};
    for (const AssetMeshData& subMesh : subMeshes) {
        // Untextured kinds have no materials
        const uint32_t layer{
            materials.empty() ? 0u : materialLayer(materials, subMesh.materialName, modelPath)};

        for (size_t i = 0; i < subMesh.positionsData.size(); ++i) {
            GeometryVertex vertex;
            vertex.position = glm::vec3(subMesh.positionsData[i][0],
                                        subMesh.positionsData[i][1],
                                        subMesh.positionsData[i][2]);
            vertex.normal = glm::vec3(subMesh.normalsData[i][0], subMesh.normalsData[i][1],
                                      subMesh.normalsData[i][2]);
            vertex.tangent = glm::vec3(subMesh.tangentsData[i][0], subMesh.tangentsData[i][1],
                                       subMesh.tangentsData[i][2]);
            vertex.uv = glm::vec2(subMesh.uvsData[i][0], subMesh.uvsData[i][1]);
            vertices.push_back(vertex);
            layers.push_back(static_cast<float>(layer));
        }

        // Without indices, a sub-mesh is a plain triangle list
        if (!subMesh.indices.empty()) {
            for (int index : subMesh.indices) {
                indices.push_back(meshVertexCount + static_cast<uint32_t>(index));
            }
        } else {
            for (size_t i = 0; i < subMesh.positionsData.size(); ++i) {
                indices.push_back(meshVertexCount + static_cast<uint32_t>(i));
            }
        }
        meshVertexCount += static_cast<uint32_t>(subMesh.positionsData.size());
    }

    range.m_indexCount = static_cast<uint32_t>(indices.size()) - range.m_firstIndex;
    return range;
}

// [-1, 1]^2 at z = 0; the impostor vertex stage turns it to the camera.
ScatterMeshRange appendQuad(std::vector<GeometryVertex>& vertices, std::vector<float>& layers,
                            std::vector<uint32_t>& indices) {
    ScatterMeshRange range;
    range.m_firstIndex = static_cast<uint32_t>(indices.size());
    range.m_baseVertex = static_cast<int32_t>(vertices.size());

    for (int corner{0}; corner < 4; ++corner) {
        const glm::vec2 square{(corner & 1) ? 1.0f : -1.0f, (corner & 2) ? 1.0f : -1.0f};
        GeometryVertex vertex;
        vertex.position = glm::vec3{square, 0.0f};
        vertex.normal = glm::vec3{0.0f, 0.0f, 1.0f};
        vertex.tangent = glm::vec3{1.0f, 0.0f, 0.0f};
        vertex.uv = 0.5f * square + 0.5f;
        vertices.push_back(vertex);
        layers.push_back(0.0f);
    }
    for (uint32_t index : {0u, 1u, 2u, 2u, 1u, 3u}) {
        indices.push_back(index);
    }

    range.m_indexCount = 6;
    return range;
}

}  // namespace

ScatterMeshes::ScatterMeshes(const std::vector<ScatterKind>& kinds) {
    std::vector<GeometryVertex> vertices;
    // Per vertex, its material's texture layer
    std::vector<float> layers;
    std::vector<uint32_t> indices;

    for (const ScatterKind& kind : kinds) {
        m_firstMeshOfKind.push_back(static_cast<uint32_t>(m_ranges.size()));

        float boundingRadius{0.0f};
        for (const ScatterLod& lod : kind.m_lods) {
            const ScatterMeshRange range{
                appendModel(lod.m_modelPath, kind.m_materials, vertices, layers, indices)};
            m_ranges.push_back(range);

            const size_t firstVertex{static_cast<size_t>(range.m_baseVertex)};
            glm::vec3 lowest{vertices[firstVertex].position};
            glm::vec3 highest{lowest};
            for (size_t i = firstVertex; i < vertices.size(); ++i) {
                boundingRadius = std::max(boundingRadius, glm::length(vertices[i].position));
                lowest = glm::min(lowest, vertices[i].position);
                highest = glm::max(highest, vertices[i].position);
            }

            const glm::vec3 centre{0.5f * (lowest + highest)};
            float radius{0.0f};
            for (size_t i = firstVertex; i < vertices.size(); ++i) {
                radius = std::max(radius, glm::length(vertices[i].position - centre));
            }
            m_centres.push_back(centre);
            m_radii.push_back(radius);
        }
        m_boundingRadii.push_back(boundingRadius);
    }
    m_lodMeshCount = static_cast<uint32_t>(m_ranges.size());

    for (const ScatterKind& kind : kinds) {
        if (!kind.m_impostor) {
            m_impostorMeshOfKind.push_back(k_noMesh);
            continue;
        }
        m_impostorMeshOfKind.push_back(static_cast<uint32_t>(m_ranges.size()));
        m_ranges.push_back(appendQuad(vertices, layers, indices));
        m_centres.push_back(glm::vec3{0.0f});
        m_radii.push_back(glm::length(glm::vec2{1.0f}));
    }

    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GeometryVertex), vertices.data(),
                 GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex),
                          (void*)offsetof(GeometryVertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex),
                          (void*)offsetof(GeometryVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex),
                          (void*)offsetof(GeometryVertex, tangent));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex),
                          (void*)offsetof(GeometryVertex, uv));
    glEnableVertexAttribArray(3);

    glGenBuffers(1, &m_layerBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_layerBuffer);
    glBufferData(GL_ARRAY_BUFFER, layers.size() * sizeof(float), layers.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(float), nullptr);
    glEnableVertexAttribArray(4);

    // Recorded in the VAO
    glGenBuffers(1, &m_indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(),
                 GL_STATIC_DRAW);

    glBindVertexArray(0);
}

ScatterMeshes::~ScatterMeshes() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_vertexBuffer != 0) glDeleteBuffers(1, &m_vertexBuffer);
    if (m_layerBuffer != 0) glDeleteBuffers(1, &m_layerBuffer);
    if (m_indexBuffer != 0) glDeleteBuffers(1, &m_indexBuffer);
}

uint32_t ScatterMeshes::meshIndex(uint32_t kind, uint32_t lod) const {
    assert(kind < m_firstMeshOfKind.size() && "Kind out of range");
    const uint32_t index{m_firstMeshOfKind[kind] + lod};
    // The next kind's first mesh, or the impostor quads, bound this kind's LODs
    assert((kind + 1 < m_firstMeshOfKind.size() ? index < m_firstMeshOfKind[kind + 1]
                                                : index < m_lodMeshCount) &&
           "LOD out of range");
    return index;
}
