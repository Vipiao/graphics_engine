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
 * FrameRenderParams frameParams{
 *     camera.getViewMatrix(), camera.getProjectionMatrix(),
 *     frameCount, timeMs, timeFraction, lightDir, camera.getPosition()
 * };
 *
 * // Single pass rendering
 * meshHandler.render(frameParams);
 * @endcode
 */

#include "../ShaderProgram.h"
#include "../SSBOManager.h"
#include "../FrameRenderParams.h"
#include "../Texture2D.h"

#include <memory>

class TextureStore;

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
   // Colour, normal, material and mask units, one byte each; see
   // MeshHandler::packTextureUnits. Per vertex rather than per mesh so one mesh
   // can wear more than one material -- a grid is a single mesh, and its blocks
   // are not obliged to share an atlas.
   uint32_t textureUnits;
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

   explicit MeshHandler(size_t maxTriangles, SSBOManager* ssboManager,
                        TextureStore* textureStore);
   ~MeshHandler();

   // Owns GL buffer/VAO/texture handles; copying would double-delete them.
   MeshHandler(const MeshHandler&) = delete;
   MeshHandler& operator=(const MeshHandler&) = delete;

   // Packs four texture units into the single value a Vertex carries. -1, or any
   // unit the shaders cannot bind, means "no texture" for that slot.
   static uint32_t packTextureUnits(int colorTextureUnit, int normalTextureUnit,
                                    int materialTextureUnit, int maskTextureUnit);
   // What packTextureUnits gives for four absent textures.
   static uint32_t noTextureUnits() { return packTextureUnits(-1, -1, -1, -1); }

   void addMesh(int ssboIndex);
   // textureUnits is per vertex and packed by packTextureUnits; null leaves the
   // triangles untextured.
   std::vector<uint32_t> appendTrianglesToMesh(
      int meshIndex, const std::vector<glm::dvec3>* vertices,
      const std::vector<glm::dvec3>* normals,
      const std::vector<glm::dvec3>* tangents,
      const std::vector<glm::dvec2>* uvs,
      const std::vector<double>* occlusionFactors = nullptr,
      const std::vector<glm::dvec4>* colors = nullptr,
      const std::vector<uint32_t>* textureUnits = nullptr
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
   void render(const FrameRenderParams& params);
   void renderGeometry(const FrameRenderParams& params);
   void renderDepth(
      const FrameRenderParams& params,
      bool renderOpaque = true, bool renderTransparent = false);

   Texture createTexture(std::string texturePath);
   void unitTest();

   // Shader reloading
   std::pair<bool, std::string> reloadShaders();

   ShaderProgram m_shaderProgram{};
   ShaderProgram m_gbufferShaderProgram{};
   ShaderProgram m_depthShaderProgram{};

protected:

   // Which of the store's textures this renderer binds; the index is the unit
   // the shaders reach it through, so entries are appended and never moved.
   std::vector<std::weak_ptr<Texture2D>> m_textureUnits;
   TextureStore* m_textureStore{ nullptr };

   unsigned int m_vertexBuffer{};
   unsigned int m_vao{};
   int m_totalTriangles{ 0 };
   size_t m_maxTriangles{};
   std::vector<Vertex> m_vertexData;
   std::map<int64_t, MeshInfo> m_meshIndexToMeshInfo;
   SSBOManager* m_ssboManager;

private:
   // Helper function for common rendering logic
   void renderGeometryHelper(ShaderProgram& program, const FrameRenderParams& params);

};