// Texture2D.h
#pragma once

#include "Texture.h"

/**
 * @brief What a Texture2D is made of. Pixels are read at construction only.
 */
struct TextureSpec {
    enum class Wrap { Repeat, ClampToEdge };

    int m_width{0};
    int m_height{0};
    TextureFormat m_format{TextureFormat::RGBA8};
    Wrap m_wrap{Wrap::Repeat};
    TextureFilter m_filter{TextureFilter::Linear};
    bool m_generateMipmaps{false};
    // Tightly packed, row major, bottom row first. Borrowed for the length of
    // the constructor and not retained.
    const void* m_pixels{nullptr};
};

/**
 * @brief One flat GL texture, built from memory rather than from a file.
 *
 * Owned by TextureStore, which is the only thing that makes them.
 */
class Texture2D : public Texture {
public:
    explicit Texture2D(const TextureSpec& spec);
};
