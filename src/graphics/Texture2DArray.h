// Texture2DArray.h
#pragma once

#include <vector>
#include "Texture.h"

/**
 * @brief What a Texture2DArray is made of. Pixels are read at construction only.
 *
 * Layers of one size and format. Each layer repeats on its own, so a shader can
 * pick the layer per vertex and keep its UVs as they were.
 */
struct TextureArraySpec {
    int m_width{0};
    int m_height{0};
    TextureFormat m_format{TextureFormat::RGBA8};
    TextureFilter m_filter{TextureFilter::Linear};
    bool m_generateMipmaps{false};
    // One per layer, tightly packed, row major, bottom row first. Borrowed for
    // the length of the constructor and not retained.
    std::vector<const void*> m_layers;
};

/**
 * @brief One GL 2D array texture, sampled with the layer as the third coordinate.
 *
 * Owned by TextureStore, which is the only thing that makes them.
 */
class Texture2DArray : public Texture {
public:
    explicit Texture2DArray(const TextureArraySpec& spec);
};
