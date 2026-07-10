#include "MeshHandler.h"

#include "../STBImageLoader.h"
#include "math/DekkerArithmetic.h"
#include "utils/HashFunctions.h"

#include <iterator>
#include <iostream>
#include <unordered_set>
#include <algorithm>
#include <set>
#include <limits>
#include <cassert>
#include <cstdlib>
#include <ctime>

#define CHECK_GL_ERROR(operation) \
   do { \
      GLenum error = glGetError(); \
      if (error != GL_NO_ERROR) { \
         std::cerr << "OpenGL error " << error << " after: " << operation << " at line " << __LINE__ << std::endl; \
      } \
   } while(0)

MeshHandler::MeshHandler(size_t maxTriangles, SSBOManager* ssboManager)
   : m_maxTriangles(maxTriangles), m_ssboManager(ssboManager) {

   m_shaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/meshHandler/mesh_vertex_shader.vert");
   m_shaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/fragment_shader.frag");
   m_shaderProgram.linkShaders();

   // Load G-buffer shaders
   m_gbufferShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/meshHandler/mesh_vertex_shader.vert");
   m_gbufferShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/gbuffer_fragment_shader.frag");
   m_gbufferShaderProgram.linkShaders();

   // Load depth-only shaders
   m_depthShaderProgram.loadVertexShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/meshHandler/mesh_depth_vertex_shader.vert");
   m_depthShaderProgram.loadFragmentShaderFromPath(ENGINE_ASSET_DIR "/src/graphics/shared_shaders/depth_fragment_shader.frag");
   m_depthShaderProgram.linkShaders();

   // Vertex buffer object.
   glGenBuffers(1, &m_vertexBuffer);
   glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
   glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 3 * m_maxTriangles, nullptr, GL_DYNAMIC_DRAW); // Reserve space

   // ShaderAttribute.
   struct ShaderAttribute {
      const char* name; GLint size;
      GLenum type; bool isIntegerType; size_t offset;
   };

   // VAO setup.
   glGenVertexArrays(1, &m_vao); 
   glBindVertexArray(m_vao);
   glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);

   ShaderAttribute attributes[] = {
       {"position", 3, GL_FLOAT, false, offsetof(Vertex, position)},
       {"normal", 3, GL_FLOAT, false, offsetof(Vertex, normal)},
       {"tangent", 3, GL_FLOAT, false, offsetof(Vertex, tangent)},
       {"uv", 2, GL_FLOAT, false, offsetof(Vertex, uv)},
       {"occlusionFactor", 1, GL_FLOAT, false, offsetof(Vertex, occlusionFactor)},
       {"color", 4, GL_FLOAT, false, offsetof(Vertex, color)},
       {"meshIndex", 1, GL_UNSIGNED_INT, true, offsetof(Vertex, meshIndex)},
       {"triangleIndex", 1, GL_UNSIGNED_INT, true, offsetof(Vertex, triangleId)}
   };
   for (const auto& attr : attributes) {
      GLint attribLocation = glGetAttribLocation(m_shaderProgram.getID(), attr.name);
      if (attribLocation != -1) {
         glEnableVertexAttribArray(attribLocation);
         if (attr.isIntegerType) {
            glVertexAttribIPointer(attribLocation, attr.size, attr.type, sizeof(Vertex), (void*)attr.offset);
         } else {
            glVertexAttribPointer(attribLocation, attr.size, attr.type, GL_FALSE, sizeof(Vertex), (void*)attr.offset);
         }
      } else {
         std::cout << "Warning: \"" << attr.name << "\" not found as an attribute in shader program." << std::endl;
      }
   }

   glBindBuffer(GL_ARRAY_BUFFER, 0);
   glBindVertexArray(0);
}

MeshHandler::~MeshHandler() {
   glDeleteBuffers(1, &m_vertexBuffer);
   glDeleteVertexArrays(1, &m_vao);
   
   for (size_t ii = 0; ii < m_textures.size(); ii++) {
      glDeleteTextures(1, &m_textures[ii].m_texture);
   }
}

void MeshHandler::addMesh(int ssboIndex) {
   // Initialize MeshInfo for this mesh.
   MeshInfo info;
   info.numTriangles = 0;
   info.nextTriangleId = 0;

   // Store MeshInfo keyed on the externally-allocated SSBO index.
   m_meshIndexToMeshInfo[ssboIndex] = info;
}

std::vector<uint32_t> MeshHandler::appendTrianglesToMesh(
   int meshIndex, const std::vector<glm::dvec3>* vertices,
   const std::vector<glm::dvec3>* normals,
   const std::vector<glm::dvec3>* tangents,
   const std::vector<glm::dvec2>* uvs,
   const std::vector<double>* occlusionFactors,
   const std::vector<glm::dvec4>* colors
) {
   // Verify the input: Ensure vertices is not a null pointer and the size is a multiple of 3
   if (vertices == nullptr || vertices->size() % 3 != 0) {
      throw std::invalid_argument("Invalid vertices argument: either null or not a multiple of 3.");
   }

   // Verify the input: Ensure normals is not a null pointer and the size is a multiple of 3
   if (normals == nullptr || normals->size() % 3 != 0) {
      throw std::invalid_argument("Invalid normals argument: either null or not a multiple of 3.");
   }

   // Verify the input: Ensure tangents is not a null pointer and the size is a multiple of 3
   if (tangents == nullptr || tangents->size() % 3 != 0) {
      throw std::invalid_argument("Invalid tangents argument: either null or not a multiple of 3.");
   }

   // Verify the input: Ensure uvs is not a null pointer and the size is a multiple of 3
   if (uvs == nullptr || uvs->size() % 3 != 0) {
      throw std::invalid_argument("Invalid uvs argument: either null or not a multiple of 3.");
   }

   // Verify the input: Ensure occlusionFactors is not a null pointer and the size is a multiple of 3
   if (occlusionFactors != nullptr && occlusionFactors->size() % 3 != 0) {
      throw std::invalid_argument("Invalid occlusionFactors argument: If not null, should be a multiple of 3.");
   }

   // Verify the input: Ensure colors is not a null pointer and the size is a multiple of 3
   if (colors != nullptr && colors->size() % 3 != 0) {
      throw std::invalid_argument("Invalid colors argument: If not null, should be a multiple of 3.");
   }

   // Check if the lengths are the same.
   if (
      vertices->size() != normals->size() ||
      vertices->size() != tangents->size() ||
      vertices->size() != uvs->size() ||
      (occlusionFactors != nullptr && vertices->size() != occlusionFactors->size()) ||
      (colors != nullptr && vertices->size() != colors->size())
   ) {
      throw std::invalid_argument("The size of vertices, normals, tangents, and uvs are not the same.");
   }

   if (vertices->size() == 0) {
      return {};
   }

   // Check capacity: See if adding the new triangles will exceed the maximum limit
   const int newTriangles = static_cast<int>(vertices->size() / 3);
   if (static_cast<size_t>(m_totalTriangles) + static_cast<size_t>(newTriangles) > m_maxTriangles) {
      throw std::runtime_error("Exceeded the maximum number of triangles.");
   }

   // Verify mesh index: Ensure the mesh index exists in our map
   auto it = m_meshIndexToMeshInfo.find(meshIndex);
   if (it == m_meshIndexToMeshInfo.end()) {
      throw std::invalid_argument("The mesh index does not exist.");
   }

   MeshInfo& meshInfo = it->second;  // Reference to the mesh's MeshInfo

   // Reserve memory for the new vertices
   std::vector<Vertex> newVertexData;
   newVertexData.reserve(vertices->size());

   // Create the new vertex data based on the new triangles
   std::vector<uint32_t> newIds;
   newIds.reserve(vertices->size() / 3);
   for (size_t ii = 0; ii < vertices->size(); ++ii) {
      // Update the triangle index every three vertices (start of each triangle)
      if (ii % 3 == 0) {
         newIds.push_back(meshInfo.nextTriangleId);
         meshInfo.triangleIndices[meshInfo.nextTriangleId] =
            static_cast<int>(m_totalTriangles * 3 + ii);
         meshInfo.nextTriangleId++;
      }

      newVertexData.push_back({
         glm::vec3((*vertices)[ii]),
         glm::vec3((*normals)[ii]),
         glm::vec3((*tangents)[ii]),
         glm::vec2((*uvs)[ii]),
         occlusionFactors != nullptr ? static_cast<float>((*occlusionFactors)[ii]) : 1.0f,
         colors != nullptr ? glm::vec4((*colors)[ii]) : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
         static_cast<uint32_t>(meshIndex),
         newIds.back()  // Use the last index in newIndices
      });
   }

   // Update the CPU-side copy of the vertex data
   m_vertexData.insert(m_vertexData.end(), newVertexData.begin(), newVertexData.end());

   // Upload only the new vertex data to the GPU from the m_vertexData buffer
   size_t offset = sizeof(Vertex) * m_totalTriangles * 3;
   glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
   glBufferSubData(GL_ARRAY_BUFFER, offset, sizeof(Vertex) * newVertexData.size(), &m_vertexData[offset / sizeof(Vertex)]);

   // Update the total triangle count
   m_totalTriangles += newTriangles;

   // Update the number of triangles in the mesh info
   meshInfo.numTriangles += newTriangles;

   return newIds;
}

void MeshHandler::removeTrianglesFromMesh(int meshIndex, const std::vector<uint32_t>* triangleIds) {
   // Check if the mesh index exists.
   auto it = m_meshIndexToMeshInfo.find(meshIndex);
   if (it == m_meshIndexToMeshInfo.end()) {
      throw std::invalid_argument("Invalid meshIndex.");
   }

   //
   if (triangleIds->size() == 0) {
      return;
   }

   // Get the mesh info.
   MeshInfo& meshInfo = it->second;

   // Convert triangleIndices to set for faster lookup.
   std::set<uint64_t> idsToRemove(triangleIds->begin(), triangleIds->end());
   size_t numVerticesToRemove = idsToRemove.size() * 3;

   // Sort all the triangle indices.
   std::vector<int64_t> sortedIndices;
   sortedIndices.reserve(idsToRemove.size());
   for (const auto& id : idsToRemove) {
      sortedIndices.push_back(meshInfo.triangleIndices[id]);
   }
   std::sort(sortedIndices.begin(), sortedIndices.end());

   // Check that all indices to remove are present in the sorted indices list.
//#ifndef NDEBUG
//   for (const auto& idToRemove : idsToRemove) {
//      bool found = false;
//      for (const auto& sortedIndex : sortedIndices) {
//         if (m_vertexData[sortedIndex].triangleId == idToRemove) {
//            found = true;
//            break;
//         }
//      }
//      if (!found) {
//         throw std::invalid_argument("Triangle index to remove not found in mesh: " + std::to_string(idToRemove));
//      }
//   }
//#endif // !NDEBUG

   // Initialize indices.
   int64_t destStart = sortedIndices[0];
   int64_t destEnd = destStart;
   int64_t srcStart = m_totalTriangles * 3 - static_cast<int>(numVerticesToRemove);
   int64_t srcEnd = srcStart;
   size_t sortedIndex = 0;  // Track the current position in sortedIndices

   // Outer loop.
   while (true) {
      while (
         srcStart < m_totalTriangles * 3 && m_vertexData[srcStart].meshIndex ==
            static_cast<uint32_t>(meshIndex) &&
         idsToRemove.find(m_vertexData[srcStart].triangleId) != idsToRemove.end()
      ) {
         srcStart += 3;
      }
      if (srcStart == m_totalTriangles * 3) {
         break;
      }
      srcEnd = srcStart;

      // Update destStart to the next element in sortedIndices that is greater or equal to destEnd
      // or the triangle index at sortedIndices[sortedIndex] should be deleted.
      while (sortedIndex < sortedIndices.size() && destEnd > sortedIndices[sortedIndex]) {
         sortedIndex++;
      }
      if (sortedIndex < sortedIndices.size()) {
         destStart = sortedIndices[sortedIndex];
      } else {
         // No more indices to move
         break;
      }

      destEnd = destStart;

      // Expand the area to copy as large as possible from src to dst where src are triangles to
      // not delete and dst are triangles to delete/overwrite.
      while (srcEnd < m_totalTriangles * 3 &&
         m_vertexData[destEnd].meshIndex == static_cast<uint32_t>(meshIndex) &&
         idsToRemove.find(m_vertexData[destEnd].triangleId) != idsToRemove.end() &&
         (m_vertexData[srcEnd].meshIndex != static_cast<uint32_t>(meshIndex) ||
            idsToRemove.find(m_vertexData[srcEnd].triangleId) == idsToRemove.end())
      ) {
         destEnd += 3;
         srcEnd += 3;
      }

      // Copy buffer from src to dest.
      size_t copySize = srcEnd - srcStart;
      if (copySize > 0) {
         std::copy(m_vertexData.begin() + srcStart, m_vertexData.begin() + srcEnd,
            m_vertexData.begin() + destStart);

         glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
         glBufferSubData(GL_ARRAY_BUFFER, sizeof(Vertex) * destStart, sizeof(Vertex) * copySize, &m_vertexData[destStart]);

         // Correct the indices in the MeshInfo objects.
         for (int64_t i = destStart; i < destEnd; i += 3) {
            int64_t meshIdx = m_vertexData[i].meshIndex;
            int64_t triangleIdx = m_vertexData[i].triangleId;

            auto meshInfoIt = m_meshIndexToMeshInfo.find(meshIdx);
            if (meshInfoIt == m_meshIndexToMeshInfo.end()) {
               throw std::runtime_error("Inconsistency found: meshIdx does not exist in m_meshIndexToMeshInfo");
            }

            meshInfoIt->second.triangleIndices[triangleIdx] = i;
         }
      }
      srcStart = srcEnd;
   }

   // Update the class members after removal.
   m_vertexData.resize(m_vertexData.size() - numVerticesToRemove);
   m_totalTriangles -= static_cast<int>(idsToRemove.size());
   // Update MeshInfo for the mesh
   for (auto index : idsToRemove) {
      meshInfo.triangleIndices.erase(index);
   }
   meshInfo.numTriangles -= static_cast<int>(idsToRemove.size());
}

void MeshHandler::updateTrianglesInformation(
   int meshIndex,
   const std::vector<uint32_t>* triangleIds,
   const std::vector<glm::dvec3>* normals,
   const std::vector<glm::dvec3>* tangents,
   const std::vector<glm::dvec2>* uvs,
   const std::vector<double>* occlusionFactors,
   const std::vector<glm::dvec4>* colors
) {
   // Check if the mesh index exists.
   auto meshIt = m_meshIndexToMeshInfo.find(meshIndex);
   if (meshIt == m_meshIndexToMeshInfo.end()) {
      throw std::invalid_argument("Invalid meshIndex.");
   }

   // Validate triangleIds vector
   if (triangleIds == nullptr || triangleIds->empty()) {
      throw std::invalid_argument("triangleIds is null or empty.");
   }

   // Validate the lengths of the vectors
   size_t numVertices = triangleIds->size() * 3;
   if ((normals != nullptr && normals->size() != numVertices) ||
      (tangents != nullptr && tangents->size() != numVertices) ||
      (uvs != nullptr && uvs->size() != numVertices) ||
      (occlusionFactors != nullptr && occlusionFactors->size() != numVertices) ||
      (colors != nullptr && colors->size() != numVertices)) {
      throw std::invalid_argument("One or more attribute vectors do not match three times the size of triangleIds.");
   }

   MeshInfo& meshInfo = meshIt->second;

   // Iterate through each triangle ID and update its information
   for (size_t i = 0; i < triangleIds->size(); ++i) {
      uint32_t triangleId = (*triangleIds)[i];
      int64_t vertexDataIndex = meshInfo.triangleIndices[triangleId];

      // Update normals, if provided
      if (normals != nullptr) {
         for (int j = 0; j < 3; ++j) {
            glm::dvec3 normal = (*normals)[i * 3 + j];
            m_vertexData[vertexDataIndex + j].normal = glm::vec3(normal);
         }
      }
      
      // Update tangents, if provided
      if (tangents != nullptr) {
         for (int j = 0; j < 3; ++j) {
            glm::dvec3 tangent = (*tangents)[i * 3 + j];
            m_vertexData[vertexDataIndex + j].tangent = glm::vec3(tangent);
         }
      }

      // Update UVs, if provided
      if (uvs != nullptr) {
         for (int j = 0; j < 3; ++j) {
            glm::dvec2 uv = (*uvs)[i * 3 + j];
            m_vertexData[vertexDataIndex + j].uv = glm::vec2(uv);
         }
      }

      // Update occlusion factors, if provided
      if (occlusionFactors != nullptr) {
         for (int j = 0; j < 3; ++j) {
            double occlusionFactor = (*occlusionFactors)[i * 3 + j];
            m_vertexData[vertexDataIndex + j].occlusionFactor = static_cast<float>(occlusionFactor);
         }
      }
      
      // Update colors, if provided
      if (colors != nullptr) {
         for (int j = 0; j < 3; ++j) {
            glm::dvec4 color = (*colors)[i * 3 + j];
            m_vertexData[vertexDataIndex + j].color = glm::vec4(color);
         }
      }
   }

   // Collect and sort the indices of the vertex data to be updated
   std::vector<int64_t> vertexDataIndices;
   vertexDataIndices.reserve(triangleIds->size());
   for (const auto& triangleId : *triangleIds) {
      int64_t vertexDataIndex = meshInfo.triangleIndices[triangleId];
      vertexDataIndices.push_back(vertexDataIndex);
   }
   std::sort(vertexDataIndices.begin(), vertexDataIndices.end());

   // Update the vertex buffer with the new data
   glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
   int64_t currentStart = vertexDataIndices[0];
   int64_t currentEnd = currentStart;

   for (size_t i = 1; i < vertexDataIndices.size(); ++i) {
      // Check if the current index is contiguous with the previous
      if (vertexDataIndices[i] == currentEnd + 3) {
         currentEnd = vertexDataIndices[i];
      } else {
         // Update the contiguous block of data
         glBufferSubData(GL_ARRAY_BUFFER, sizeof(Vertex) * currentStart,
            sizeof(Vertex) * (currentEnd - currentStart + 3),
            &m_vertexData[currentStart]);

         // Start a new block
         currentStart = vertexDataIndices[i];
         currentEnd = currentStart;
      }
   }

   // Update the last block of data
   if (currentEnd >= currentStart) {
      glBufferSubData(GL_ARRAY_BUFFER, sizeof(Vertex) * currentStart,
         sizeof(Vertex) * (currentEnd - currentStart + 3),
         &m_vertexData[currentStart]);
   }
}

void MeshHandler::removeMesh(int meshIndex) {
   // Check if the mesh index exists.
   auto it = m_meshIndexToMeshInfo.find(meshIndex);
   if (it == m_meshIndexToMeshInfo.end()) {
      throw std::invalid_argument("Invalid meshIndex.");
   }

   // Get the mesh info.
   MeshInfo& info = it->second;

   if (info.numTriangles == 0) {
      m_meshIndexToMeshInfo.erase(meshIndex);
      return;
   }

   // Sort all the triangle indices.
   std::vector<int64_t> sortedIndices;
   sortedIndices.reserve(info.triangleIndices.size());
   for (const auto& pair : info.triangleIndices) {
      sortedIndices.push_back(pair.second);
   }
   std::sort(sortedIndices.begin(), sortedIndices.end());

   // Initialize indices.
   int64_t destStart = sortedIndices[0];
   int64_t destEnd = destStart;
   int64_t srcStart = m_totalTriangles * 3 - info.numTriangles * 3;
   int64_t srcEnd = srcStart;
   size_t sortedIndex = 0;  // Track the current position in sortedIndices

   // Outer loop.
   while (true) {
      while (srcStart < m_totalTriangles * 3 && m_vertexData[srcStart].meshIndex == static_cast<uint32_t>(meshIndex)) {
         srcStart += 3;
      }
      if (srcStart == m_totalTriangles * 3) {
         break;
      }
      srcEnd = srcStart;

      // Update destStart to the next element in sortedIndices that is greater or equal to destEnd
      while (sortedIndex < sortedIndices.size() && destEnd > sortedIndices[sortedIndex]) {
         sortedIndex++;
      }
      if (sortedIndex < sortedIndices.size()) {
         destStart = sortedIndices[sortedIndex];
      } else {
         // No more indices to move
         break;
      }

      destEnd = destStart;
      srcStart = srcEnd;

      // Inner loop
      while (srcEnd < m_totalTriangles * 3 && m_vertexData[destEnd].meshIndex == static_cast<uint32_t>(meshIndex) &&
         m_vertexData[srcEnd].meshIndex != static_cast<uint32_t>(meshIndex)) {
         destEnd += 3;
         srcEnd += 3;
      }

      // Copy buffer from src to dest.
      size_t copySize = srcEnd - srcStart;
      if (copySize > 0) {
         std::copy(m_vertexData.begin() + srcStart, m_vertexData.begin() + srcEnd,
            m_vertexData.begin() + destStart);

         glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
         glBufferSubData(GL_ARRAY_BUFFER, sizeof(Vertex) * destStart, sizeof(Vertex) * copySize, &m_vertexData[destStart]);

         // Correct the indices in the MeshInfo objects.
         for (int64_t i = destStart; i < destEnd; i += 3) {
            int meshIdx = m_vertexData[i].meshIndex;
            int triangleId = m_vertexData[i].triangleId;

            auto meshInfoIt = m_meshIndexToMeshInfo.find(meshIdx);
            if (meshInfoIt == m_meshIndexToMeshInfo.end()) {
               throw std::runtime_error("Inconsistency found: meshIdx does not exist in m_meshIndexToMeshInfo");
            }

            meshInfoIt->second.triangleIndices[triangleId] = i;
         }
      }
      srcStart = srcEnd;
   }

   // Update the class members after removal.
   size_t numVerticesToRemove = info.numTriangles * 3;
   m_vertexData.resize(m_vertexData.size() - numVerticesToRemove);
   m_totalTriangles -= info.numTriangles;
   m_meshIndexToMeshInfo.erase(meshIndex);
   // SSBO deallocation is the caller's responsibility — MeshHandler does not own SSBO indices.
}

void MeshHandler::render(const FrameRenderParams& params) {
   // Use forward rendering shader program
   m_shaderProgram.use();

   // Set light direction (unique to forward rendering)
   GLint lightDirLoc = glGetUniformLocation(m_shaderProgram.getID(), "u_lightDir");
   if (lightDirLoc != -1) {
      // Transform light direction to view space
      glm::vec4 lightDirView = glm::mat4(params.view) * glm::vec4(glm::vec3(params.lightDir), 0.0f);
      glm::vec3 lightDirFloat(lightDirView.x, lightDirView.y, lightDirView.z);
      glUniform3fv(lightDirLoc, 1, glm::value_ptr(lightDirFloat));
   }

   // Use helper for common rendering logic
   renderGeometryHelper(params);
}

void MeshHandler::renderGeometry(const FrameRenderParams& params) {
   // Use G-buffer shader program
   m_gbufferShaderProgram.use();

   // Use helper for common rendering logic
   renderGeometryHelper(params);
}

void MeshHandler::renderDepth(
   const FrameRenderParams& params,
   bool /* renderOpaque */, bool /* renderTransparent */
) {
   // Use depth-only shader program
   m_depthShaderProgram.use();

   // Use helper for common rendering logic
   renderGeometryHelper(params);
}

void MeshHandler::renderGeometryHelper(const FrameRenderParams& params) {
   // Get currently active shader program
   GLint currentProgram;
   glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
   if (currentProgram == 0) {
      throw std::runtime_error("No shader program is currently active");
   }
   unsigned int programID = static_cast<unsigned int>(currentProgram);

   // Set uniforms
   GLint viewLoc = glGetUniformLocation(programID, "view");
   GLint projectionLoc = glGetUniformLocation(programID, "projection");
   GLint frameLoc = glGetUniformLocation(programID, "u_frame");
   GLint timeLoc = glGetUniformLocation(programID, "u_time");
   GLint timeRemainderLoc = glGetUniformLocation(programID, "u_timeRemainder");
   GLint cameraPosHighLoc = glGetUniformLocation(programID, "u_cameraPositionHigh");
   GLint cameraPosLowLoc = glGetUniformLocation(programID, "u_cameraPositionLow");

   // Convert double precision parameters to float precision for OpenGL
   glm::mat4 viewFloat{ params.view };
   glm::mat4 projectionFloat{ params.projection };

   if (viewLoc != -1)
       glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(viewFloat));
   if (projectionLoc != -1)
       glUniformMatrix4fv(projectionLoc, 1, GL_FALSE, glm::value_ptr(projectionFloat));
   if (frameLoc != -1)
       glUniform1ui(frameLoc, params.frame);
   if (timeLoc != -1)
       glUniform1ui(timeLoc, params.time);
   if (timeRemainderLoc != -1)
       glUniform1f(timeRemainderLoc, (float)params.timeRemainder);

   // Set camera position as Dekker number
   if (cameraPosHighLoc == -1 || cameraPosLowLoc == -1) {
      throw std::runtime_error("Camera position Dekker uniforms not found in shader");
   }
   {
      typedef DekkerArithmetic<float> DekkerFloat;
      DekkerFloat::DekkerNumber camX(params.camPos.x);
      DekkerFloat::DekkerNumber camY(params.camPos.y);
      DekkerFloat::DekkerNumber camZ(params.camPos.z);
      glm::vec3 camPosHigh(camX.main, camY.main, camZ.main);
      glm::vec3 camPosLow(camX.error, camY.error, camZ.error);
      glUniform3fv(cameraPosHighLoc, 1, glm::value_ptr(camPosHigh));
      glUniform3fv(cameraPosLowLoc, 1, glm::value_ptr(camPosLow));
   }
   
   // Bind textures
   for (size_t ii = 0; ii < m_textures.size(); ii++) {
      const Texture* texture = &m_textures[ii];

       // Debug check: ensure texture unit is within shader array bounds
       if (texture->m_textureUnit >= 32) {
           throw std::runtime_error("MeshHandler texture unit " + std::to_string(texture->m_textureUnit) +
                                  " exceeds shader array size (32)");
       }

      glActiveTexture(GL_TEXTURE0 + texture->m_textureUnit);
      glBindTexture(GL_TEXTURE_2D, texture->m_texture);
      
      std::string textureName = "u_textures[" + std::to_string(texture->m_textureUnit) + "]";
      GLint textureLoc = glGetUniformLocation(programID, textureName.c_str());
      if (textureLoc != -1)
          glUniform1i(textureLoc, texture->m_textureUnit);
   }
   
   // Set depth testing and render geometry
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   glBindVertexArray(m_vao);
   glDrawArrays(GL_TRIANGLES, 0, m_totalTriangles * 3);
   glBindVertexArray(0);
}

MeshHandler::Texture MeshHandler::createTexture(std::string texturePath) {
   if (m_textures.size() >= m_maxTextures) {
      throw std::runtime_error(
         "MeshHandler: cannot create texture \"" + texturePath +
         "\": maximum number of textures (" + std::to_string(m_maxTextures) + ") reached");
   }
   // Load image data first; STBImageLoader::load throws if the file cannot be
   // read, so no GL texture is created for a failed load.
   int width{ 0 };
   int height{ 0 };
   int nrChannels{ 0 };
   unsigned char* data = STBImageLoader::load(true, texturePath, &width, &height, &nrChannels);
   assert((nrChannels == 3 || nrChannels == 4) && "texture must be RGB or RGBA");

   unsigned int texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   // set the texture wrapping parameters
   // Set setting to clamp to edge to prevent artifacts.
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
   // set texture filtering parameters
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   // create texture and generate mipmaps
   if (nrChannels == 3) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
   } else { // nrChannels == 4
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
   }
   glGenerateMipmap(GL_TEXTURE_2D);
   STBImageLoader::free(data);

   //
   m_textures.emplace_back(texture, (unsigned int)m_textures.size());

   return m_textures.back();
}

std::pair<bool, std::string> MeshHandler::reloadShaders() {
   std::string allErrors;
   bool allSuccess = true;
   
   auto [success1, error1] = m_shaderProgram.reloadShaders();
   auto [success2, error2] = m_gbufferShaderProgram.reloadShaders();
   auto [success3, error3] = m_depthShaderProgram.reloadShaders();
   
   if (!success1) { allSuccess = false; allErrors += "Main shader: " + error1 + "\n"; }
   if (!success2) { allSuccess = false; allErrors += "GBuffer shader: " + error2 + "\n"; }
   if (!success3) { allSuccess = false; allErrors += "Depth shader: " + error3 + "\n"; }
   
   if (allSuccess) {
      return {true, "All MeshHandler shaders reloaded successfully"};
   } else {
      return {false, allErrors};
   }
}

struct TestMeshData {
   int meshId;
   std::vector<uint32_t> triangleIndices;
};

void MeshHandler::unitTest() {
   // Set seed for reproducibility
   srand(0);

   // Vector to hold test meshes
   std::vector<TestMeshData> testMeshes;

   // Loop 1000 times to simulate adding and removing meshes
   for (int i = 0; i < 10000; ++i) {
      int action = rand() % 4; // Randomly choose an action

      if ((action == 0 || testMeshes.empty()) && testMeshes.size() <= 100) {
         // Add a mesh
         int n = rand() % 11;
         int numVertices = 3 * n;

         // Create a vector of vertices
         std::vector<glm::dvec3> vertices(numVertices, glm::dvec3(0.0, 0.0, 0.0));
         std::vector<glm::dvec3> normals(numVertices, glm::dvec3(0.0, 0.0, 0.0));
         std::vector<glm::dvec3> tangents(numVertices, glm::dvec3(1.0, 0.0, 0.0));
         std::vector<glm::dvec2> uvs(numVertices, glm::dvec2(0.0, 0.0));

         // Allocate SSBO index externally
         int ssboIndex = m_ssboManager->allocateIndex();
         addMesh(ssboIndex);
         appendTrianglesToMesh(ssboIndex, &vertices, &normals, &tangents, &uvs);

         // Create and add TestMeshData instance
         TestMeshData testMesh;
         testMesh.meshId = ssboIndex;
         for (size_t j = 0; j < vertices.size() / 3; ++j) {
            testMesh.triangleIndices.push_back(static_cast<uint32_t>(j));
         }
         testMeshes.push_back(testMesh);
      } else if (action == 1 && !testMeshes.empty()) {
         // Remove a mesh
         // Randomly choose a mesh to remove
         int indexToRemove = rand() % testMeshes.size();
         int ssboIndexToRemove = testMeshes[indexToRemove].meshId;

         // Call removeMesh, then release the SSBO index (owned externally in new design)
         removeMesh(ssboIndexToRemove);
         m_ssboManager->deallocateIndex(ssboIndexToRemove);

         // Remove the mesh from testMeshes
         testMeshes.erase(testMeshes.begin() + indexToRemove);
      } else if (action == 2 && !testMeshes.empty()) {
         // Append triangles to a mesh
         int indexToAppend = rand() % testMeshes.size();
         int meshIdToAppend = testMeshes[indexToAppend].meshId;

         int n = rand() % 11;
         int numVertices = 3 * n;

         std::vector<glm::dvec3> vertices(numVertices, glm::dvec3(0.0, 0.0, 0.0));
         std::vector<glm::dvec3> normals(numVertices, glm::dvec3(0.0, 0.0, 0.0));
         std::vector<glm::dvec3> tangents(numVertices, glm::dvec3(1.0, 0.0, 0.0));
         std::vector<glm::dvec2> uvs(numVertices, glm::dvec2(0.0, 0.0));

         std::vector<uint32_t> appendedTriangles = 
            appendTrianglesToMesh(meshIdToAppend, &vertices, &normals, &tangents, &uvs);
            
         for (auto triangleIndex : appendedTriangles) {
            testMeshes[indexToAppend].triangleIndices.push_back(triangleIndex);
         }
      } else if (action == 3 && !testMeshes.empty()) {
         // Remove triangles from a mesh
         int indexToRemove = rand() % testMeshes.size();
         int meshIdToRemove = testMeshes[indexToRemove].meshId;

         int numTrianglesToRemove = rand() % (testMeshes[indexToRemove].triangleIndices.size() + 1);
         std::vector<uint32_t> indicesToRemove;
         indicesToRemove.reserve(numTrianglesToRemove);

         for (int j = 0; j < numTrianglesToRemove; ++j) {
            if (testMeshes[indexToRemove].triangleIndices.empty()) break;
            int triangleIndexToRemove = rand() % testMeshes[indexToRemove].triangleIndices.size();
            indicesToRemove.push_back(testMeshes[indexToRemove].triangleIndices[triangleIndexToRemove]);
            testMeshes[indexToRemove].triangleIndices.erase(testMeshes[indexToRemove].triangleIndices.begin() + triangleIndexToRemove);
         }

         removeTrianglesFromMesh(meshIdToRemove, &indicesToRemove);
      }

      // Verification code
      for (const auto& testMesh : testMeshes) {
         // Retrieve MeshInfo
         auto it = m_meshIndexToMeshInfo.find(testMesh.meshId);
         if (it == m_meshIndexToMeshInfo.end()) {
            throw std::runtime_error("MeshInfo not found for mesh ID " + std::to_string(testMesh.meshId));
         }
         const MeshInfo& meshInfo = it->second;

         // Verify numTriangles
         int vertexCount = 0;
         for (const auto& vertex : m_vertexData) {
            if (vertex.meshIndex == static_cast<uint32_t>(testMesh.meshId)) {
               ++vertexCount;
            }
         }
         if (vertexCount / 3 != meshInfo.numTriangles) {
            throw std::runtime_error("Triangle count mismatch for mesh ID " + std::to_string(testMesh.meshId));
         }

         // Verify triangleIndices
         for (const auto& triangleIndex : testMesh.triangleIndices) {
            auto triangleIt = meshInfo.triangleIndices.find(triangleIndex);
            if (triangleIt == meshInfo.triangleIndices.end()) {
               throw std::runtime_error("Triangle index not found in MeshInfo for mesh ID " + std::to_string(testMesh.meshId));
            }

            // Verify the location in m_vertexData
            int64_t expectedVertexIndex = triangleIt->second;
            if (m_vertexData[expectedVertexIndex].meshIndex != static_cast<uint32_t>(testMesh.meshId) ||
               m_vertexData[expectedVertexIndex].triangleId != triangleIndex) {
               throw std::runtime_error("Triangle index does not point to correct location in m_vertexData for mesh ID " + std::to_string(testMesh.meshId));
            }
         }
      }
   }
}