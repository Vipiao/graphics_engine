// Texture2D.h
#pragma once

#include <glad/glad.h>

/**
 * @brief What a Texture2D is made of. Pixels are read at construction only.
 *
 * Sized formats rather than a channel count, because the width of a texel is
 * part of what the caller decided: a height field quantized to 16 bits is not
 * the same texture as one quantized to 8, and the difference is not the
 * renderer's to guess at.
 */
struct TextureSpec {
    // Fixed point spends its bits uniformly across a bounded range, which is
    // what generated data with known extremes wants. The float format is for
    // data whose extremes are not known in advance, which fixed point could
    // only take by carrying a scale alongside it to map onto [0, 1].
    enum class Format { R8, RG8, R16, RG16, RG16F, RGB8, RGBA8 };
    enum class Wrap { Repeat, ClampToEdge };
    enum class Filter { Nearest, Linear };

    int m_width{0};
    int m_height{0};
    Format m_format{Format::RGBA8};
    Wrap m_wrap{Wrap::Repeat};
    Filter m_filter{Filter::Linear};
    bool m_generateMipmaps{false};
    // Tightly packed, row major, bottom row first. Borrowed for the length of
    // the constructor and not retained.
    const void* m_pixels{nullptr};
};

/**
 * @brief One GL texture object, built from memory rather than from a file.
 *
 * Owned by TextureStore, which is the only thing that makes them. Owns its
 * handle and deletes it; non-copyable, so the handle has exactly one owner and
 * cannot be freed twice.
 */
class Texture2D {
public:
    explicit Texture2D(const TextureSpec& spec);
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    GLuint getID() const { return m_id; }

private:
    GLuint m_id{0};
};
