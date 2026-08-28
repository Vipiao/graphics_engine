// Texture.h
#pragma once

#include <glad/glad.h>

/**
 * @brief How wide a texel is, and how its bits are spent.
 *
 * Sized formats rather than a channel count, because the width of a texel is
 * part of what the caller decided: a height field quantized to 16 bits is not
 * the same texture as one quantized to 8, and the difference is not the
 * renderer's to guess at.
 *
 * Fixed point spends its bits uniformly across a bounded range, which is what
 * generated data with known extremes wants. The float formats are for data whose
 * extremes are not known in advance, which fixed point could only take by
 * carrying a scale alongside it to map onto [0, 1].
 */
enum class TextureFormat { R16, RG16F, RGB8, RGB16F, RGBA8 };

enum class TextureFilter { Nearest, Linear };

// The three things GL wants to describe one texel: how it is stored, how the
// upload is laid out, and what type the upload holds.
struct TextureLayout {
    GLenum m_internalFormat;
    GLenum m_pixelFormat;
    GLenum m_pixelType;
};

TextureLayout textureLayout(TextureFormat format);

/**
 * @brief One GL texture object, whatever shape it holds.
 *
 * Owns its handle and deletes it; non-copyable, so the handle has exactly one
 * owner and cannot be freed twice. Carries its target alongside because nothing
 * can recover that from the handle, and a bind needs it.
 *
 * The shape is the derived class' business: this exists so the store can own one
 * kind of thing and a shader input can name one, neither having to care which
 * shape arrived.
 */
class Texture {
public:
    virtual ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    GLuint getID() const { return m_id; }
    GLenum getTarget() const { return m_target; }

protected:
    explicit Texture(GLenum target) : m_target{target} {}

    GLuint m_id{0};

private:
    GLenum m_target{GL_TEXTURE_2D};
};
