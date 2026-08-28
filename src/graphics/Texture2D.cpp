// Texture2D.cpp
#include "Texture2D.h"
#include <stdexcept>

namespace {

GLint wrapMode(TextureSpec::Wrap wrap) {
    return wrap == TextureSpec::Wrap::Repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
}

}  // namespace

Texture2D::Texture2D(const TextureSpec& spec) : Texture{GL_TEXTURE_2D} {
    if (spec.m_width <= 0 || spec.m_height <= 0) {
        throw std::runtime_error("Texture2D: width and height must be positive");
    }
    if (!spec.m_pixels) {
        throw std::runtime_error("Texture2D: no pixels to upload");
    }

    const TextureLayout layout{textureLayout(spec.m_format)};
    const GLint filter{spec.m_filter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST};

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
                        spec.m_filter == TextureFilter::Linear
                            ? GL_LINEAR_MIPMAP_LINEAR
                            : GL_NEAREST_MIPMAP_NEAREST);
    } else {
        // Without this the default minification filter expects mip levels that
        // were never uploaded, and the texture samples as black.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
