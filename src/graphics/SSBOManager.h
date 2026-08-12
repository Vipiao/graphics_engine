// SSBOManager.h
#pragma once

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>

// CPU-side twin of the GLSL MeshData in
// src/graphics/shared_shaders/mesh_transform.glsl (std430 layout). Any field
// change must be mirrored there, keeping the total size a multiple of 16.
#pragma pack(push, 1)
struct MeshData {
    glm::vec4 positionHigh{};        // Offset= 0, size=16 bytes. High part of Dekker position
    glm::vec4 positionLow{};         // Offset=16, size=16 bytes. Low part of Dekker position
    glm::vec4 velocity{};            // Offset=32, size=16 bytes.
    glm::vec4 orientation{};         // Offset=48, size=16 bytes. Quaternion
    glm::vec4 angVel{};              // Offset=64, size=16 bytes. Unit axis (xyz)
    glm::vec4 centerOfRotation{};    // Offset=80, size=16 bytes.
    glm::vec4 scale{};               // Offset=96, size=16 bytes. (xyz = scale, w = padding)
    uint32_t time{};                 // Offset=112, size= 4 bytes.
    float emissiveScalar{};          // Offset=116, size= 4 bytes.
    uint32_t padding[2]{};           // Offset=120, size= 8 bytes. Padding to make total size 128 (divisible by 16)
}; // Make sure to pad so size is divisible by 16 because you have a vec4.
#pragma pack(pop)

// Where a mesh is, and nothing else. Materials are named by whatever draws the
// geometry -- a vertex attribute for meshes, the instance buffer for instanced
// geometry -- because a texture unit means something only within one renderer's
// pass, while this record is shared by all of them.
static_assert(sizeof(MeshData) == 128,
    "MeshData must match the 128-byte std430 layout in mesh_transform.glsl");

// The transform arguments of the last updateMeshTransform for an index, kept at
// the precision they were given at. MeshData itself is lossy -- position becomes
// a Dekker float pair and everything else a plain float -- so a consumer that
// has to reproduce the shader's transform on the CPU (LOD selection against the
// same interpolated pose the vertex stage builds) reads it back from here.
struct MeshTransform {
    glm::dvec3 position{};
    glm::dvec3 velocity{};
    glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 angVelAxis{0.0, 1.0, 0.0};
    double angVel{};
    glm::dvec3 centerOfRotation{};
    glm::dvec3 scale{1.0};
    uint64_t time{};
};

/**
 * @brief Manages a shared SSBO for mesh and instance transformation data
 * 
 * This class handles the OpenGL SSBO lifecycle and index allocation for both
 * MeshHandler and future InstanceHandler to share transformation data.
 */
class SSBOManager {
public:
    /**
     * @brief Constructor - creates and initializes the SSBO
     * @param maxEntries Maximum number of MeshData entries the SSBO can hold
     */
    explicit SSBOManager(size_t maxEntries);
    
    /**
     * @brief Destructor - cleans up OpenGL resources
     */
    ~SSBOManager();

    // Owns the SSBO handle; copying would double-delete it.
    SSBOManager(const SSBOManager&) = delete;
    SSBOManager& operator=(const SSBOManager&) = delete;
    
    /**
     * @brief Allocate a new index for storing MeshData
     * @return Index to use for updateData(), or throws if SSBO is full
     */
    int allocateIndex();
    
    /**
     * @brief Deallocate an index for reuse
     * @param index Index to deallocate
     */
    void deallocateIndex(int index);
    
    /**
     * @brief Update MeshData at specified index
     * @param index Index to update (must be allocated)
     * @param data New MeshData to store
     */
    void updateData(int index, const MeshData& data);

    /**
     * @brief Update mesh transform data at specified index using transform parameters
     */
    void updateMeshTransform(
        int index,
        const glm::dvec3& position,
        const glm::dvec3& velocity,
        const glm::dquat& orientation,
        const glm::dvec3& angVelAxis,
        double angVel,
        const glm::dvec3& centerOfRotation,
        const glm::dvec3& scale,
        uint64_t time,
        double emissiveScalar = 1.0);

    /**
     * @brief Transform last written to an index by updateMeshTransform
     *
     * Never reads back from the GPU; this is the value the caller supplied,
     * remembered at full precision. An index that has not been written yet
     * reads as the identity transform.
     */
    const MeshTransform& getMeshTransform(int index) const;

private:
    GLuint m_ssbo;
    size_t m_maxEntries;
    size_t m_nextNewIndex;
    std::vector<int> m_availableIndices;
    // Parallel to the SSBO contents, one entry per index. Only
    // updateMeshTransform maintains it; updateData writes the raw struct and is
    // not a transform, so it leaves this untouched.
    std::vector<MeshTransform> m_meshTransforms;
};