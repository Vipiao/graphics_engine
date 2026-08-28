// Texture.cpp
#include "Texture.h"
#include <stdexcept>

TextureLayout textureLayout(TextureFormat format) {
    switch (format) {
        case TextureFormat::R16:   return {GL_R16, GL_RED, GL_UNSIGNED_SHORT};
        // Uploaded as full float and stored as half: the narrowing is GL's, so
        // the caller never handles a 16-bit float itself.
        case TextureFormat::RG16F: return {GL_RG16F, GL_RG, GL_FLOAT};
        case TextureFormat::RGB8:  return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
        case TextureFormat::RGB16F: return {GL_RGB16F, GL_RGB, GL_FLOAT};
        case TextureFormat::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
    throw std::runtime_error("Texture: unknown format");
}

Texture::~Texture() {
    if (m_id != 0) glDeleteTextures(1, &m_id);
}
