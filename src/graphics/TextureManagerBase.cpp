// TextureManagerBase.cpp
#include "TextureManagerBase.h"
#include "STBImageLoader.h"
#include "ShaderProgram.h"
#include <cassert>
#include <iostream>
#include <stdexcept>

TextureManagerBase::TextureManagerBase() : m_nextTextureUnit(0) {
}

TextureManagerBase::~TextureManagerBase() {
    // Cleanup textures
    for (const auto& texture : m_textures) {
        glDeleteTextures(1, &texture.textureId);
    }
}

int TextureManagerBase::createTexture(const std::string& path) {
    // Check if texture already exists
    for (const auto& texture : m_textures) {
        if (texture.path == path) {
            return texture.textureUnit;
        }
    }
    
    // Fail early: all texture units must fit in the shaders' u_textures array.
    if (m_nextTextureUnit >= ShaderProgram::s_maxTextureUnits) {
        throw std::runtime_error(
            "TextureManagerBase: cannot create texture \"" + path +
            "\": maximum number of textures (" +
            std::to_string(ShaderProgram::s_maxTextureUnits) + ") reached");
    }

    // Load new texture; loadTextureFromFile throws if the file cannot be read.
    GLuint textureId = loadTextureFromFile(path);

    // Store texture info
    Texture texture;
    texture.textureId = textureId;
    texture.textureUnit = m_nextTextureUnit++;
    texture.path = path;
    
    m_textures.push_back(texture);
    
    return texture.textureUnit;
}

GLuint TextureManagerBase::loadTextureFromFile(const std::string& path) {
    // Load image data first; STBImageLoader::load throws if the file cannot be
    // read, so no GL texture is created for a failed load.
    int width{ 0 };
    int height{ 0 };
    int nrChannels{ 0 };
    unsigned char* data = STBImageLoader::load(true, path, &width, &height, &nrChannels);
    assert((nrChannels == 3 || nrChannels == 4) && "texture must be RGB or RGBA");

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLenum format = (nrChannels == 3) ? GL_RGB : GL_RGBA;
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    STBImageLoader::free(data);

    return textureId;
}