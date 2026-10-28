// Texture2DArray.cpp
#include "Texture2DArray.h"
#include <stdexcept>

Texture2DArray::Texture2DArray(const TextureArraySpec& spec) : Texture{GL_TEXTURE_2D_ARRAY} {
    if (spec.m_width <= 0 || spec.m_height <= 0) {
        throw std::runtime_error("Texture2DArray: width and height must be positive");
    }
    if (spec.m_layers.empty()) {
        throw std::runtime_error("Texture2DArray: no layers to upload");
    }
    for (const void* layer : spec.m_layers) {
        if (!layer) throw std::runtime_error("Texture2DArray: a layer has no pixels to upload");
    }

    const TextureLayout layout{textureLayout(spec.m_format)};
    const GLint filter{spec.m_filter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST};
    const GLsizei layerCount{static_cast<GLsizei>(spec.m_layers.size())};

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_id);

    // Rows are packed with no padding, which the default alignment of 4 would
    // mis-read for any format whose row length is not a multiple of it.
    GLint previousAlignment{4};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, static_cast<GLint>(layout.m_internalFormat),
                 spec.m_width, spec.m_height, layerCount, 0, layout.m_pixelFormat,
                 layout.m_pixelType, nullptr);
    for (GLsizei layer{0}; layer < layerCount; ++layer) {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, spec.m_width, spec.m_height, 1,
                        layout.m_pixelFormat, layout.m_pixelType, spec.m_layers[layer]);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, filter);

    if (spec.m_generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER,
                        spec.m_filter == TextureFilter::Linear
                            ? GL_LINEAR_MIPMAP_LINEAR
                            : GL_NEAREST_MIPMAP_NEAREST);
    } else {
        // Without this the default minification filter expects mip levels that
        // were never uploaded, and the texture samples as black.
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, filter);
    }

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}
