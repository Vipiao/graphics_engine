#pragma once

/**
 * @file MeshHandler.h
 * 
 * @brief A mesh management system for OpenGL rendering.
 * 
 * The MeshHandler class provides a robust system for managing 3D mesh data in an OpenGL context.
 * It handles the creation, modification, and rendering of triangle-based meshes with support
 * for positions, normals, texture coordinates, and occlusion factors.
 * 
 * Key features:
 * - Efficient mesh management with unique IDs for both meshes and triangles
 * - Dynamic addition and removal of triangles/meshes
 * - Support for texture mapping
 * - Single-pass rendering with occlusion control
 * - GPU memory management with vertex buffer objects (VBOs) and vertex array objects (VAOs)
 * 
 * Usage example:
 * @code
 * // Create SSBO manager and mesh handler
 * auto ssboManager = std::make_unique<SSBOManager>(100);  // 100 mesh capacity
 * MeshHandler meshHandler(1000, ssboManager.get());  // 1000 triangle capacity
 * 
 * // Create a new mesh
 * int meshId = meshHandler.addMesh();
 * 
 * // Define a simple triangle
 * std::vector<glm::dvec3> vertices = {
 *     glm::dvec3(-1.0, -1.0, 0.0),
 *     glm::dvec3( 1.0, -1.0, 0.0),
 *     glm::dvec3( 0.0,  1.0, 0.0)
 * };
 * std::vector<glm::dvec3> normals = {
 *     glm::dvec3(0.0, 0.0, 1.0),
 *     glm::dvec3(0.0, 0.0, 1.0),
 *     glm::dvec3(0.0, 0.0, 1.0)
 * };
 * std::vector<glm::dvec3> tangents = {
 *     glm::dvec3(1.0, 0.0, 0.0),
 *     glm::dvec3(1.0, 0.0, 0.0),
 *     glm::dvec3(1.0, 0.0, 0.0)
 * };
 * std::vector<glm::dvec2> uvs = {
 *     glm::dvec2(0.0, 0.0),
 *     glm::dvec2(1.0, 0.0),
 *     glm::dvec2(0.5, 1.0)
 * };
 * 
 * // Optional occlusion factors (0.0 = fully occluded, 1.0 = no occlusion)
 * std::vector<double> occlusionFactors = {
 *     1.0, 1.0, 1.0  // No occlusion for any vertex
 * };
 * 
 * // Add triangle to mesh and get its ID
 * std::vector<uint32_t> triangleIds = meshHandler.appendTrianglesToMesh(
 *     meshId, &vertices, &normals, &tangents, &uvs, &occlusionFactors);
 * 
 * // Update mesh transform using SSBOManager
 * glm::dvec3 position(0.0, 0.0, 0.0);
 * glm::dvec3 velocity(0.0, 0.0, 0.0);
 * glm::dquat orientation(1.0, 0.0, 0.0, 0.0);  // Identity quaternion
 * glm::dvec3 angVelAxis(0.0, 1.0, 0.0);
 * double angVel = 0.0;
 * glm::dvec3 centerOfRotation(0.0, 0.0, 0.0);
 * glm::dvec3 scale(1.0, 1.0, 1.0);
 * int32_t colorTextureUnit = -1;  // No texture
 * int32_t normalTextureUnit = -1; // No texture
 * uint64_t interpolationTimeStep = 0;
 * 
 * ssboManager->updateMeshTransform(
 *     meshId, position, velocity, orientation, 
 *     angVelAxis, angVel, centerOfRotation, scale,
 *     colorTextureUnit, normalTextureUnit, interpolationTimeStep);
 * 
 * // In render loop:
 * glm::mat4 view = camera.getViewMatrix();
 * glm::mat4 projection = camera.getProjectionMatrix();
 * glm::dvec3 lightPos(5.0, 5.0, 5.0);
 * glm::dvec3 camPos = camera.getPosition();
 * 
 * // Single pass rendering
 * meshHandler.render(view, projection, frameCount, timeMs, 
 *                   timeFraction, lightPos, camPos);
 * @endcode
 */

#include "../ShaderProgram.h"
#include "../SSBOManager.h"

#include <glm/glm.hpp>
#include <vector>
#include <map>
#include <glad/glad.h>
#include <stdexcept>
#include <glm/gtc/quaternion.hpp>

#pragma pack(push, 1)
static_assert(sizeof(float)* std::size_t(8) == std::size_t(32), "float is not 32 bits.");
struct Vertex {
   glm::vec3 position;
   glm::vec3 normal;
   glm::vec3 tangent;
   glm::vec2 uv;
   float occlusionFactor;
   glm::vec4 color;
   uint32_t meshIndex;
   uint32_t triangleId;
};
#pragma pack(pop)

struct MeshInfo {
   int numTriangles{ 0 };
   std::map<uint64_t, uint64_t> triangleIndices{}; // Triangle id to index in vertex data.
   int nextTriangleId{ 0 };
};

class MeshHandler {
public:
   class Texture {
   protected:
   public:
      Texture() : m_texture(0), m_textureUnit(0) {}
      Texture(unsigned int texture, unsigned int textureUnit) {
         m_texture = texture;
         m_textureUnit = textureUnit;
      }
      unsigned int m_texture{};
      unsigned int m_textureUnit{};
   };

   explicit MeshHandler(size_t maxTriangles, SSBOManager* ssboManager);
   ~MeshHandler();

   void addMesh(int ssboIndex);
   std::vector<uint32_t> appendTrianglesToMesh(
      int meshIndex, const std::vector<glm::dvec3>* vertices,
      const std::vector<glm::dvec3>* normals,
      const std::vector<glm::dvec3>* tangents,
      const std::vector<glm::dvec2>* uvs,
      const std::vector<double>* occlusionFactors = nullptr,
      const std::vector<glm::dvec4>* colors = nullptr
   );
   // removeTrianglesFromMesh: "triangleIndices" are the indices of the triangles you want to delete.
   // Does not need to be ordered
   void removeTrianglesFromMesh(int meshIndex, const std::vector<uint32_t>* triangleIds);
   void updateTrianglesInformation(
      int meshIndex,
      const std::vector<uint32_t>* triangleIds = nullptr,
      const std::vector<glm::dvec3>* normals = nullptr,
      const std::vector<glm::dvec3>* tangents = nullptr,
      const std::vector<glm::dvec2>* uvs = nullptr,
      const std::vector<double>* occlusionFactors = nullptr,
      const std::vector<glm::dvec4>* colors = nullptr
   );
   void removeMesh(int meshIndex);
   void render(
      const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
      double timeRemainder, const glm::dvec3& lightDir, glm::dvec3 camPos);
   void renderGeometry(
      const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
      double timeRemainder, const glm::dvec3& lightDir, glm::dvec3 camPos);
   void renderDepth(
      const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
      double timeRemainder, const glm::dvec3& camPos,
      bool renderOpaque = true, bool renderTransparent = false);

   Texture createTexture(std::string texturePath);
   void unitTest();

   // Shader reloading
   std::pair<bool, std::string> reloadShaders();

   ShaderProgram m_shaderProgram{};
   ShaderProgram m_gbufferShaderProgram{};
   ShaderProgram m_depthShaderProgram{};

protected:

   std::vector<Texture> m_textures{};

   unsigned int m_vertexBuffer{};
   unsigned int m_vao{};
   int m_totalTriangles{ 0 };
   size_t m_maxTriangles{};
   size_t m_maxTextures{ 16 };
   std::vector<Vertex> m_vertexData;
   std::map<int64_t, MeshInfo> m_meshIndexToMeshInfo;
   SSBOManager* m_ssboManager;

private:
   // Helper function for common rendering logic
   void renderGeometryHelper(
       const glm::mat4& view, const glm::mat4& projection, uint64_t frame, uint64_t time,
       double timeRemainder, const glm::dvec3& camPos);

};