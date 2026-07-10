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
    int32_t colorTextureUnit{};      // Offset=116, size= 4 bytes. (-1 means no textures)
    int32_t normalTextureUnit{};     // Offset=120, size= 4 bytes. (-1 means no textures)
    int32_t materialTextureUnit{};   // Offset=124, size= 4 bytes. (-1 means no textures)
    float emissiveScalar{};          // Offset=128, size= 4 bytes.
    int32_t maskTextureUnit{-1};     // Offset=132, size= 4 bytes. (-1 means no mask texture)
    uint32_t padding[2]{};           // Offset=136, size= 8 bytes. Padding to make total size 144 (divisible by 16)
}; // Make sure to pad so size is divisible by 16 because you have a vec4.
#pragma pack(pop)

static_assert(sizeof(MeshData) == 144,
    "MeshData must match the 144-byte std430 layout in mesh_transform.glsl");

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
        int32_t colorTextureUnit,
        int32_t normalTextureUnit,
        int32_t materialTextureUnit,
        uint64_t time,
        double emissiveScalar = 1.0,
        int32_t maskTextureUnit = -1);
    
private:
    GLuint m_ssbo;
    size_t m_maxEntries;
    size_t m_nextNewIndex;
    std::vector<int> m_availableIndices;
};