// TextureCube.cpp
#include "TextureCube.h"
#include <stdexcept>

TextureCube::TextureCube(const CubeTextureSpec& spec) : Texture{GL_TEXTURE_CUBE_MAP} {
    if (spec.m_size <= 0) {
        throw std::runtime_error("TextureCube: face size must be positive");
    }
    for (const void* face : spec.m_faces) {
        if (!face) throw std::runtime_error("TextureCube: a face has no pixels to upload");
    }

    const TextureLayout layout{textureLayout(spec.m_format)};
    const GLint filter{spec.m_filter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST};

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);

    // Rows are packed with no padding, which the default alignment of 4 would
    // mis-read for any format whose row length is not a multiple of it.
    GLint previousAlignment{4};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for (int face{0}; face < CubeTextureSpec::s_faceCount; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                     static_cast<GLint>(layout.m_internalFormat), spec.m_size, spec.m_size,
                     0, layout.m_pixelFormat, layout.m_pixelType, spec.m_faces[face]);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

    // Clamped on all three axes so the filter has somewhere to fall back to at a
    // face's own rim; what it actually reads there is the neighbouring face,
    // seamless filtering being enabled for the context.
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, filter);

    if (spec.m_generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                        spec.m_filter == TextureFilter::Linear
                            ? GL_LINEAR_MIPMAP_LINEAR
                            : GL_NEAREST_MIPMAP_NEAREST);
    } else {
        // Without this the default minification filter expects mip levels that
        // were never uploaded, and the texture samples as black.
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, filter);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}
