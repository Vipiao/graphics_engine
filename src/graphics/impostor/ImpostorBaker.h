// ImpostorBaker.h
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include "ImpostorConfig.h"

class ShaderProgram;
class Texture2D;
class Texture2DArray;
class TextureStore;

// A mesh already uploaded: GeometryVertex at locations 0 to 3, the texture layer
// (a float) at 4, indices in the VAO.
struct ImpostorMesh {
    GLuint m_vao{0};
    uint32_t m_firstIndex{0};
    uint32_t m_indexCount{0};
    int32_t m_baseVertex{0};
    // A sphere containing the mesh, which every view frames
    glm::vec3 m_centre{0.0f};
    float m_radius{0.0f};
};

// As material_decode.glsl reads it.
struct ImpostorMaterial {
    glm::vec4 m_color{1.0f};
    // Colour, normal, material, mask, indexed by the vertex's layer; empty for none
    std::array<std::weak_ptr<Texture2DArray>, 4> m_textures;
    // Fragments with alpha below it are cut out
    float m_alphaCutoff{0.5f};
};

// The textures are the store's; whoever asked for the bake removes them.
struct ImpostorAtlas {
    // RGB: albedo. A: coverage.
    std::weak_ptr<Texture2D> m_color;
    // RGB: normal in the mesh's frame, packed to [0, 1]. A: the surface's depth
    // towards the viewer from the sphere's centre, -radius to radius packed to [0, 1].
    std::weak_ptr<Texture2D> m_normal;
    ImpostorBakeSpec m_spec;
    glm::vec3 m_centre{0.0f};
    float m_radius{0.0f};
};

/**
 * @brief Bakes a mesh into colour and normal-with-depth atlases, one cell per view.
 *
 * Renders into its own framebuffer and restores the GL state it changes. On the
 * CPU, see-through texels take the nearest covered texel of their cell, so mips
 * do not fringe dark at the outline.
 */
class ImpostorBaker {
public:
    explicit ImpostorBaker(TextureStore* textureStore);
    ~ImpostorBaker();

    ImpostorBaker(const ImpostorBaker&) = delete;
    ImpostorBaker& operator=(const ImpostorBaker&) = delete;

    // Throws on an invalid spec or mesh.
    ImpostorAtlas bake(const ImpostorMesh& mesh, const ImpostorMaterial& material,
                       const ImpostorBakeSpec& spec);

    // As <name>_color.png, _normal.png, _coverage.png and _depth.png. False if a
    // file could not be written or a texture has expired.
    static bool writeImages(const ImpostorAtlas& atlas, const std::filesystem::path& directory,
                            const std::string& name);

private:
    TextureStore* m_textureStore;
    std::unique_ptr<ShaderProgram> m_program;
};
