// TextureCube.h
#pragma once

#include <array>
#include "Texture.h"

/**
 * @brief What a TextureCube is made of. Pixels are read at construction only.
 *
 * Six square faces of one size, in GL's own order: +X, -X, +Y, -Y, +Z, -Z. No
 * wrap mode, a cube having no edge to run off: a lookup is a direction, and
 * every direction lands on exactly one face.
 */
struct CubeTextureSpec {
    static constexpr int s_faceCount{6};

    int m_size{0};
    TextureFormat m_format{TextureFormat::RGBA8};
    TextureFilter m_filter{TextureFilter::Linear};
    bool m_generateMipmaps{false};
    // One face apiece, tightly packed and row major. Borrowed for the length of
    // the constructor and not retained.
    std::array<const void*, s_faceCount> m_faces{};
};

/**
 * @brief One GL cube texture, sampled by direction rather than by coordinate.
 *
 * Owned by TextureStore, which is the only thing that makes them.
 *
 * Filtering runs across face boundaries, so a field that is continuous in
 * direction is continuous once sampled: without that, the half texel between
 * one face's last texel centre and the next face's would flatten, leaving a
 * ridge along all twelve of the cube's edges.
 */
class TextureCube : public Texture {
public:
    explicit TextureCube(const CubeTextureSpec& spec);
};
