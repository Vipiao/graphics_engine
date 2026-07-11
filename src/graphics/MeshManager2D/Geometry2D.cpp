// src/graphics/MeshManager2D/Geometry2D.cpp
#include "Geometry2D.h"
#include <iostream>
#include <algorithm>

Geometry2D::Geometry2D(const std::vector<Vertex2D>& vertices,
                       const std::vector<unsigned int>& indices,
                       GLuint textureId,
                       int textureUnit,
                       bool enableTransparency)
    : m_uniqueId(s_nextGeometryId++), m_vertices(vertices), m_indices(indices),
      m_textureId(textureId), m_textureUnit(textureUnit),
      m_enableTransparency(enableTransparency), m_instanceVBO(0),
      m_VAO(0), m_VBO(0), m_EBO(0) {

    setupOpenGL();
}

Geometry2D::~Geometry2D() {
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    glDeleteBuffers(1, &m_EBO);
    glDeleteBuffers(1, &m_instanceVBO);
}

void Geometry2D::setupOpenGL() {
    glGenVertexArrays(1, &m_VAO);
    glBindVertexArray(m_VAO);

    // Interleaved vertex buffer (position + texcoord)
    glGenBuffers(1, &m_VBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex2D),
                 m_vertices.data(), GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                         (void*)offsetof(Vertex2D, position));
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D),
                         (void*)offsetof(Vertex2D, texCoord));
    glEnableVertexAttribArray(1);

    // Element buffer
    glGenBuffers(1, &m_EBO);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int),
                 m_indices.data(), GL_STATIC_DRAW);

    // Instance buffer; storage is (re)allocated as instances are added
    glGenBuffers(1, &m_instanceVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

    // Position (vec2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData2D),
                         (void*)offsetof(InstanceData2D, position));
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);

    // Scale (vec2)
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData2D),
                         (void*)offsetof(InstanceData2D, scale));
    glEnableVertexAttribArray(3);
    glVertexAttribDivisor(3, 1);

    // Color (vec4)
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData2D),
                         (void*)offsetof(InstanceData2D, color));
    glEnableVertexAttribArray(4);
    glVertexAttribDivisor(4, 1);

    // Orientation (float)
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData2D),
                         (void*)offsetof(InstanceData2D, orientation));
    glEnableVertexAttribArray(5);
    glVertexAttribDivisor(5, 1);

    glBindVertexArray(0);
}

std::weak_ptr<Instance2D> Geometry2D::addInstance() {
    auto instance = std::make_shared<Instance2D>();

    // Set buffer index to end of current data
    instance->m_bufferIndex = static_cast<uint32_t>(m_instances.size());

    // Add to both vectors at same index
    m_instances.push_back(instance);

    InstanceData2D data = createInstanceData(instance.get());
    m_instanceData.push_back(data);

    // Check if we need to grow the buffer
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    if (m_instanceData.size() > m_instanceBufferCapacity) {
        // Double the buffer capacity
        m_instanceBufferCapacity = std::max(static_cast<size_t>(1), m_instanceBufferCapacity * 2);
        // Allocate larger buffer (uninitialized)
        glBufferData(GL_ARRAY_BUFFER, m_instanceBufferCapacity * sizeof(InstanceData2D),
                     nullptr, GL_DYNAMIC_DRAW);
        // Copy only the valid data
        glBufferSubData(GL_ARRAY_BUFFER, 0, m_instanceData.size() * sizeof(InstanceData2D),
                       m_instanceData.data());
    } else {
        // Just update the new instance data
        uploadInstanceData(m_instanceData.size() - 1);
    }

    return instance;
}

void Geometry2D::removeInstance(std::weak_ptr<Instance2D> instanceWeak) {
    auto instance = instanceWeak.lock();
    if (!ownsInstance(instance.get())) {
        std::cerr << "Geometry2D: Instance not owned by this geometry in removeInstance"
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
    if (index < m_instanceData.size()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
        uploadInstanceData(index);
    }
}

void Geometry2D::updateInstanceInBuffer(Instance2D* instance) {
    if (!ownsInstance(instance)) {
        return;
    }

    // Update CPU data
    m_instanceData[instance->m_bufferIndex] = createInstanceData(instance);

    // Update GPU buffer at specific offset
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    uploadInstanceData(instance->m_bufferIndex);
}

bool Geometry2D::ownsInstance(const Instance2D* instance) const {
    return instance && instance->m_bufferIndex < m_instances.size()
        && m_instances[instance->m_bufferIndex].get() == instance;
}

InstanceData2D Geometry2D::createInstanceData(const Instance2D* instance) const {
    InstanceData2D data{};
    data.position    = glm::vec2(instance->m_position);
    data.scale       = glm::vec2(instance->m_scale);
    data.color       = glm::vec4(instance->m_color);
    data.orientation = static_cast<float>(instance->m_orientation);
    return data;
}

void Geometry2D::uploadInstanceData(size_t index) const {
    glBufferSubData(GL_ARRAY_BUFFER,
                   index * sizeof(InstanceData2D),
                   sizeof(InstanceData2D),
                   &m_instanceData[index]);
}

void Geometry2D::render() const {
    if (m_instances.empty()) {
        return;
    }

    // Handle transparency
    if (m_enableTransparency) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }

    glBindVertexArray(m_VAO);

    // Bind texture if available
    if (m_textureId != 0) {
        glActiveTexture(GL_TEXTURE0 + m_textureUnit);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
    }

    // Instanced draw call
    glDrawElementsInstanced(GL_TRIANGLES,
                           static_cast<GLsizei>(m_indices.size()),
                           GL_UNSIGNED_INT, 0,
                           static_cast<GLsizei>(m_instances.size()));

    glBindVertexArray(0);
}
