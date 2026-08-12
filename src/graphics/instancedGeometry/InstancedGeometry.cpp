// InstancedGeometry.cpp
#include "InstancedGeometry.h"
#include "../AssimpLoader.h"
#include "../ShaderProgram.h"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <stdexcept>

// Static member initialization
uint64_t Geometry::s_nextGeometryId = 0;
uint64_t Instance::s_nextInstanceId = 0;

Geometry::~Geometry() {
    if (m_VAO != 0) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO != 0) glDeleteBuffers(1, &m_VBO);
    if (m_EBO != 0) glDeleteBuffers(1, &m_EBO);
    if (m_instanceVBO != 0) glDeleteBuffers(1, &m_instanceVBO);
}

std::shared_ptr<Geometry> Geometry::loadFromFile(const std::string& modelPath) {
    auto geometry = std::make_shared<Geometry>();

    std::vector<AssetMeshData> meshes;
    AssimpLoader::load(modelPath, &meshes, false);

    if (meshes.empty()) {
        throw std::runtime_error("No meshes found in model file");
    }

    // Combine all meshes into single geometry
    std::vector<GeometryVertex> vertices;
    std::vector<uint32_t> indices;

    for (const AssetMeshData& mesh : meshes) {
        uint32_t baseVertex = static_cast<uint32_t>(vertices.size());

        // Add vertices
        for (size_t i = 0; i < mesh.positionsData.size(); ++i) {
            GeometryVertex vertex;
            vertex.position = glm::vec3(mesh.positionsData[i][0], mesh.positionsData[i][1], mesh.positionsData[i][2]);
            vertex.normal = glm::vec3(mesh.normalsData[i][0], mesh.normalsData[i][1], mesh.normalsData[i][2]);
            vertex.tangent = glm::vec3(mesh.tangentsData[i][0], mesh.tangentsData[i][1], mesh.tangentsData[i][2]);
            vertex.uv = glm::vec2(mesh.uvsData[i][0], mesh.uvsData[i][1]);
            vertices.push_back(vertex);
        }

        // Add indices
        if (!mesh.indices.empty()) {
            for (int index : mesh.indices) {
                indices.push_back(baseVertex + static_cast<uint32_t>(index));
            }
        } else {
            // Generate indices for triangle list
            for (size_t i = 0; i < mesh.positionsData.size(); ++i) {
                indices.push_back(baseVertex + static_cast<uint32_t>(i));
            }
        }
    }

    geometry->setupOpenGL(vertices, indices);
    return geometry;
}

std::shared_ptr<Geometry> Geometry::createFromVertices(
    const std::vector<GeometryVertex>& vertices) {
    if (vertices.empty() || vertices.size() % 3 != 0) {
        throw std::runtime_error("Geometry vertices do not form whole triangles");
    }

    auto geometry = std::make_shared<Geometry>();

    std::vector<uint32_t> indices;
    indices.reserve(vertices.size());
    for (size_t ii = 0; ii < vertices.size(); ++ii) {
        indices.push_back(static_cast<uint32_t>(ii));
    }

    geometry->setupOpenGL(vertices, indices);
    return geometry;
}

int Geometry::addTexture(std::weak_ptr<Texture2D> texture) {
    const std::shared_ptr<Texture2D> registered{texture.lock()};
    if (!registered) {
        throw std::runtime_error("Geometry::addTexture: texture has expired");
    }

    // The store hands out one texture per source, so an equal handle means this
    // geometry already has a unit for it. Units are the scarce thing here.
    for (size_t unit{0}; unit < m_textureUnits.size(); ++unit) {
        if (m_textureUnits[unit].lock() == registered) return static_cast<int>(unit);
    }

    if (m_textureUnits.size() >= static_cast<size_t>(ShaderProgram::s_maxTextureUnits)) {
        throw std::runtime_error(
            "Geometry::addTexture: all " +
            std::to_string(ShaderProgram::s_maxTextureUnits) +
            " texture units of this geometry are taken");
    }

    m_textureUnits.push_back(texture);
    return static_cast<int>(m_textureUnits.size() - 1);
}

std::weak_ptr<Instance> Geometry::addInstance(
    int ssboIndex, int colorTextureUnit, int normalTextureUnit,
    int materialTextureUnit, const glm::dvec4& color, int maskTextureUnit) {
    // A unit numbers this geometry's own textures, so one obtained from another
    // geometry names whatever happens to sit at that index here -- a wrong image
    // rather than an error. -1 is "no texture".
    const auto unitIsOurs{[this](int unit) {
        return unit == -1 || (unit >= 0 && static_cast<size_t>(unit) < m_textureUnits.size());
    }};
    assert(unitIsOurs(colorTextureUnit) && unitIsOurs(normalTextureUnit) &&
           unitIsOurs(materialTextureUnit) && unitIsOurs(maskTextureUnit) &&
           "Texture unit was not registered against this geometry");

    auto instance = std::make_shared<Instance>();
    instance->m_ssboIndex = ssboIndex;
    instance->m_colorTextureUnit = colorTextureUnit;
    instance->m_normalTextureUnit = normalTextureUnit;
    instance->m_materialTextureUnit = materialTextureUnit;
    instance->m_maskTextureUnit = maskTextureUnit;
    instance->m_color = color;

    // Set buffer index to end of current data
    instance->m_bufferIndex = static_cast<uint32_t>(m_instances.size());

    // Add to both vectors at same index
    m_instances.push_back(instance);

    InstanceData data = createInstanceData(instance.get());
    m_instanceData.push_back(data);

    // Check if we need to grow the buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    if (m_instanceData.size() > m_instanceBufferCapacity) {
        // Double the buffer capacity
        m_instanceBufferCapacity = std::max(static_cast<size_t>(1), m_instanceBufferCapacity * 2);
        // Allocate larger buffer (uninitialized)
        glBufferData(GL_ARRAY_BUFFER, m_instanceBufferCapacity * sizeof(InstanceData),
                     nullptr, GL_DYNAMIC_DRAW);
        // Copy only the valid data
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_instanceData.size() * sizeof(InstanceData),
                       m_instanceData.data());
    } else {
        // Just update the new instance data
        glBufferSubData(GL_ARRAY_BUFFER, (m_instanceData.size() - 1) * sizeof(InstanceData),
                       sizeof(InstanceData), &data);
    }

    return instance;
}

void Geometry::removeInstance(std::weak_ptr<Instance> instanceWeak) {
    auto instance = instanceWeak.lock();
    if (!ownsInstance(instance.get())) {
        std::cerr << "Geometry: Instance not owned by this geometry in removeInstance"
                  << std::endl;
        return;
    }

    uint32_t index = instance->m_bufferIndex;

    // If not the last element, move last element to this position
    if (index != m_instances.size() - 1) {
        // Move last instance to this position in both vectors
        m_instances[index] = m_instances.back();
        m_instanceData[index] = m_instanceData.back();
        // Update moved instance's buffer index
        m_instances[index]->m_bufferIndex = index;
    }

    // Remove last element from both vectors
    m_instances.pop_back();
    m_instanceData.pop_back();

    // Update GPU buffer only if we moved an instance
    if (!m_instanceData.empty() && index != m_instanceData.size()) {
         glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        glBufferSubData(GL_ARRAY_BUFFER, index * sizeof(InstanceData),
                       sizeof(InstanceData), &m_instanceData[index]);
    }
}

void Geometry::updateInstanceInBuffer(Instance* instance) {
    if (!ownsInstance(instance)) {
        return;
    }

    // Update CPU data
    m_instanceData[instance->m_bufferIndex] = createInstanceData(instance);

    // Update GPU buffer at specific offset
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, instance->m_bufferIndex * sizeof(InstanceData),
                    sizeof(InstanceData), &m_instanceData[instance->m_bufferIndex]);
}

bool Geometry::ownsInstance(const Instance* instance) const {
    return instance && instance->m_bufferIndex < m_instances.size()
        && m_instances[instance->m_bufferIndex].get() == instance;
}

InstanceData Geometry::createInstanceData(Instance* instance) {
    InstanceData data;

    data.localPosition = glm::vec3(instance->m_localPosition);
    data.padding1 = 0.0f;
    data.localOrientation = glm::vec4(static_cast<float>(instance->m_localOrientation.x),
                                    static_cast<float>(instance->m_localOrientation.y),
                                    static_cast<float>(instance->m_localOrientation.z),
                                    static_cast<float>(instance->m_localOrientation.w));
    data.localScale = glm::vec3(instance->m_localScale);
    data.padding2 = 0.0f;
    data.color = glm::vec4(instance->m_color);
    data.ssboIndex = instance->m_ssboIndex;
    data.colorTextureUnit = instance->m_colorTextureUnit;
    data.normalTextureUnit = instance->m_normalTextureUnit;
    data.materialTextureUnit = instance->m_materialTextureUnit;
    data.maskTextureUnit = instance->m_maskTextureUnit;

    return data;
}

void Geometry::setupOpenGL(const std::vector<GeometryVertex>& vertices,
                           const std::vector<uint32_t>& indices) {
    m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_indexCount = static_cast<uint32_t>(indices.size());
    m_hasIndices = !indices.empty();

    // Generate VAO
    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    // Generate and fill VBO
    glGenBuffers(1, &m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GeometryVertex),
                 vertices.data(), GL_STATIC_DRAW);

    // Setup vertex attributes (per-vertex data)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, position));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, normal));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, tangent));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GeometryVertex), (void*)offsetof(GeometryVertex, uv));
    glEnableVertexAttribArray(3);

    // Create instance buffer
    glGenBuffers(1, &m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

    // Setup instance attributes (per-instance data)
    // Local position
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, localPosition));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    // Local orientation quaternion
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, localOrientation));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    // Local scale
    glVertexAttribPointer(6, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, localScale));
    glEnableVertexAttribArray(6);
    glVertexAttribDivisor(6, 1);

    // Instance color (location 7)
    glVertexAttribPointer(7, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, color));
    glEnableVertexAttribArray(7);
    glVertexAttribDivisor(7, 1);

    // Mesh index for SSBO lookup (location 8)
    glVertexAttribIPointer(8, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, ssboIndex));
    glEnableVertexAttribArray(8);
    glVertexAttribDivisor(8, 1);

    // Color texture unit (location 9)
    glVertexAttribIPointer(9, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, colorTextureUnit));
    glEnableVertexAttribArray(9);
    glVertexAttribDivisor(9, 1);

    // Normal texture unit (location 10)
    glVertexAttribIPointer(10, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, normalTextureUnit));
    glEnableVertexAttribArray(10);
    glVertexAttribDivisor(10, 1);

    // Material texture unit (location 11)
    glVertexAttribIPointer(11, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, materialTextureUnit));
    glEnableVertexAttribArray(11);
    glVertexAttribDivisor(11, 1);

    // Mask texture unit (location 12)
    glVertexAttribIPointer(12, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, maskTextureUnit));
    glEnableVertexAttribArray(12);
    glVertexAttribDivisor(12, 1);

    // Generate and fill EBO if we have indices
    if (m_hasIndices) {
        glGenBuffers(1, &m_EBO);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t),
                     indices.data(), GL_STATIC_DRAW);
    }

    glBindVertexArray(0);
}
