// TextureManagerBase.cpp
#include "TextureManagerBase.h"
#include "STBImageLoader.h"
#include <iostream>

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
    
    // Load new texture
    GLuint textureId = loadTextureFromFile(path);
    if (textureId == 0) {
        std::cerr << "TextureManagerBase: Failed to load texture " << path << std::endl;
        return -1;
    }
    
    // Store texture info
    Texture texture;
    texture.textureId = textureId;
    texture.textureUnit = m_nextTextureUnit++;
    texture.path = path;
    
    m_textures.push_back(texture);
    
    return texture.textureUnit;
}

GLuint TextureManagerBase::loadTextureFromFile(const std::string& path) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Load image data
    int width, height, nrChannels;
    unsigned char* data = STBImageLoader::load(true, path, &width, &height, &nrChannels);
    
    if (data) {
        GLenum format = (nrChannels == 3) ? GL_RGB : GL_RGBA;

        //// Set pixel alignment to 1 to handle RGB textures correctly
        //glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        //
        //// For RGB textures, force alpha channel to 1.0 using swizzle mask
        //if (nrChannels == 3) {
        //    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        //}

        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        STBImageLoader::free(data);
        
        return textureId;
    } else {
        glDeleteTextures(1, &textureId);
        return 0; // Failed to load
    }
}