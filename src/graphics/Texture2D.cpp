// Texture2D.cpp
#include "Texture2D.h"
#include <stdexcept>

namespace {

// The three things GL wants to describe one texel: how it is stored, how the
// upload is laid out, and what type the upload holds.
struct FormatLayout {
    GLenum m_internalFormat;
    GLenum m_pixelFormat;
    GLenum m_pixelType;
};

FormatLayout formatLayout(TextureSpec::Format format) {
    switch (format) {
        case TextureSpec::Format::R8:    return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case TextureSpec::Format::RG8:   return {GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
        case TextureSpec::Format::R16:   return {GL_R16, GL_RED, GL_UNSIGNED_SHORT};
        case TextureSpec::Format::RG16:  return {GL_RG16, GL_RG, GL_UNSIGNED_SHORT};
        case TextureSpec::Format::RGB8:  return {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
        case TextureSpec::Format::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
    throw std::runtime_error("Texture2D: unknown format");
}

GLint wrapMode(TextureSpec::Wrap wrap) {
    return wrap == TextureSpec::Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

}  // namespace

Texture2D::Texture2D(const TextureSpec& spec) {
    if (spec.m_width <= 0 || spec.m_height <= 0) {
        throw std::runtime_error("Texture2D: width and height must be positive");
    }
    if (!spec.m_pixels) {
        throw std::runtime_error("Texture2D: no pixels to upload");
    }

    const FormatLayout layout{formatLayout(spec.m_format)};
    const GLint filter{spec.m_filter == TextureSpec::Filter::Linear ? GL_LINEAR
                                                                    : GL_NEAREST};

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    // Rows are packed with no padding, which the default alignment of 4 would
    // mis-read for any format whose row length is not a multiple of it.
    GLint previousAlignment{4};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(layout.m_internalFormat), spec.m_width,
                 spec.m_height, 0, layout.m_pixelFormat, layout.m_pixelType, spec.m_pixels);

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode(spec.m_wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode(spec.m_wrap));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

    if (spec.m_generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        spec.m_filter == TextureSpec::Filter::Linear
                            ? GL_LINEAR_MIPMAP_LINEAR
                            : GL_NEAREST_MIPMAP_NEAREST);
    } else {
        // Without this the default minification filter expects mip levels that
        // were never uploaded, and the texture samples as black.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

Texture2D::~Texture2D() {
    if (m_id != 0) glDeleteTextures(1, &m_id);
}
