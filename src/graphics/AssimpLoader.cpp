#include "AssimpLoader.h"

#include <stdexcept>
#include <iostream>

AssetMeshData AssimpLoader::processMesh(
    aiMesh* mesh, 
    const aiScene* scene,
    bool ignoreTextureCoordinates
) {
    AssetMeshData meshData;

    // Reserve space to avoid reallocations
    meshData.positionsData.reserve(mesh->mNumVertices);
    meshData.normalsData.reserve(mesh->mNumVertices);
    meshData.tangentsData.reserve(mesh->mNumVertices);
    meshData.uvsData.reserve(mesh->mNumVertices);

    // Process vertex positions, normals, tangents, and texture coordinates
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        // Process positions
        std::vector<double> position = {
            static_cast<double>(mesh->mVertices[i].x),
            static_cast<double>(mesh->mVertices[i].y),
            static_cast<double>(mesh->mVertices[i].z)
        };
        meshData.positionsData.push_back(position);

        // Process normals
        if (mesh->HasNormals()) {
            std::vector<double> normal = {
                static_cast<double>(mesh->mNormals[i].x),
                static_cast<double>(mesh->mNormals[i].y),
                static_cast<double>(mesh->mNormals[i].z)
            };
            meshData.normalsData.push_back(normal);
        } else {
            // Default normal pointing upward if none exists
            meshData.normalsData.push_back({0.0, 1.0, 0.0});
        }

        // Process tangents
        if (mesh->HasTangentsAndBitangents()) {
            std::vector<double> tangent = {
                static_cast<double>(mesh->mTangents[i].x),
                static_cast<double>(mesh->mTangents[i].y),
                static_cast<double>(mesh->mTangents[i].z)
            };
            meshData.tangentsData.push_back(tangent);
        } else {
            // Generate a tangent perpendicular to the normal
            const std::vector<double>& normal = meshData.normalsData.back();
            std::vector<double> tangent(3);
            
            if (std::abs(normal[1]) < 0.99) {
                // Cross with up vector if normal is not pointing mostly up
                // Up vector (0,1,0) cross normal
                tangent[0] = static_cast<double>(normal[2]);  // normal.z
                tangent[1] = 0.0;
                tangent[2] = -static_cast<double>(normal[0]); // -normal.x
            } else {
                // Cross with right vector if normal is pointing mostly up
                // Right vector (1,0,0) cross normal
                tangent[0] = 0.0;
                tangent[1] = -static_cast<double>(normal[2]); // -normal.z
                tangent[2] = static_cast<double>(normal[1]);  // normal.y
            }
            
            // Normalize the tangent
            double length = sqrt(tangent[0]*tangent[0] + tangent[1]*tangent[1] + tangent[2]*tangent[2]);
            if (length > 0.000001) {
                tangent[0] /= length;
                tangent[1] /= length;
                tangent[2] /= length;
            } else {
                tangent[0] = 1.0;
                tangent[1] = 0.0;
                tangent[2] = 0.0;
            }
            
            meshData.tangentsData.push_back(tangent);
        }

        // Process texture coordinates
        if (mesh->HasTextureCoords(0) && !ignoreTextureCoordinates) {
            std::vector<double> uv = {
                static_cast<double>(mesh->mTextureCoords[0][i].x),
                1.0 - static_cast<double>(mesh->mTextureCoords[0][i].y) // Flip Y for OpenGL convention
            };
            meshData.uvsData.push_back(uv);
        } else {
            // Default texture coordinates if none exists
            meshData.uvsData.push_back({0.0, 0.0});
        }
    }

    // Process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            meshData.indices.push_back(face.mIndices[j]);
        }
    }

    // Process materials if needed (currently not implemented)
    // if (mesh->mMaterialIndex >= 0) { ... }

    return meshData;
}

void AssimpLoader::processNode(
    aiNode* node, 
    const aiScene* scene,
    std::vector<AssetMeshData>* meshes,
    bool ignoreTextureCoordinates
) {
    // Process all meshes in this node
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes->push_back(processMesh(mesh, scene, ignoreTextureCoordinates));
    }

    // Process child nodes recursively
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        processNode(node->mChildren[i], scene, meshes, ignoreTextureCoordinates);
    }
}

void AssimpLoader::load(
    const std::string& path,
    std::vector<AssetMeshData>* meshes,
    bool ignoreTextureCoordinates
) {
    // Clear the output container if it's not empty
    meshes->clear();

    // Create Assimp importer
    Assimp::Importer importer;

    // Set up post-processing flags
    unsigned int flags = aiProcess_Triangulate | aiProcess_FlipUVs;
    
    // Add additional processing flags
    flags |= aiProcess_CalcTangentSpace;          // Calculate tangents and bitangents
    //flags |= aiProcess_GenSmoothNormals;          // Generate smooth normals if not present
    //flags |= aiProcess_JoinIdenticalVertices;     // Optimize mesh by joining identical vertices
    //flags |= aiProcess_ImproveCacheLocality;      // Improve performance through better cache usage
    //flags |= aiProcess_FindInvalidData;           // Remove or fix invalid data

    // Load the scene
    const aiScene* scene = importer.ReadFile(path, flags);

    // Check for errors
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "ERROR: Loading file at: \"" + path + "\" failed." << std::endl;
        std::cout << "Assimp error: " << importer.GetErrorString() << std::endl;
        throw std::runtime_error("ERROR: Loading file at: \"" + path + "\" failed.");
    }

    // Process the scene
    processNode(scene->mRootNode, scene, meshes, ignoreTextureCoordinates);

    // Print loading information
    std::cout << "Model loaded successfully from: " << path << std::endl;
    std::cout << "Number of meshes: " << meshes->size() << std::endl;

    // Print mesh information
    for (size_t i = 0; i < meshes->size(); i++) {
        const AssetMeshData& mesh = (*meshes)[i];
        std::cout << "  Mesh " << i << ": " 
                  << mesh.positionsData.size() << " vertices, "
                  << mesh.indices.size() / 3 << " triangles, "
                  << (mesh.uvsData.size() > 0 ? "has texture coordinates" : "no texture coordinates") << ", "
                  << (mesh.tangentsData.size() > 0 ? "has tangents" : "no tangents") << std::endl;
    }
}