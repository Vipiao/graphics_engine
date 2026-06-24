#pragma once

#include <string>
#include <vector>

// No forward declarations for GLM types - we don't need them in the header anymore

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// Rename to AssetMeshData to avoid conflict with MeshHandler's MeshData
struct AssetMeshData {
    std::vector<std::vector<double>> positionsData;  // Each position is 3 doubles (x,y,z)
    std::vector<std::vector<double>> normalsData;    // Each normal is 3 doubles (x,y,z)
    std::vector<std::vector<double>> tangentsData;   // Each tangent is 3 doubles (x,y,z)
    std::vector<std::vector<double>> uvsData;        // Each UV is 2 doubles (u,v)
    std::vector<int> indices;
};

class AssimpLoader {
private:
    static void processNode(
        aiNode* node, 
        const aiScene* scene,
        std::vector<AssetMeshData>* meshes,
        bool ignoreTextureCoordinates
    );

    static AssetMeshData processMesh(
        aiMesh* mesh, 
        const aiScene* scene,
        bool ignoreTextureCoordinates
    );

public:
    // Load model and return a vector of AssetMeshData structures
    static void load(
        const std::string& path,
        std::vector<AssetMeshData>* meshes,
        bool ignoreTextureCoordinates = false
    );
};