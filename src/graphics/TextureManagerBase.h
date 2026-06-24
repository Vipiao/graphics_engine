// TextureManagerBase.h
#pragma once

#include <vector>
#include <string>
#include <glad/glad.h>

/**
 * @brief Base class for texture management
 */
class TextureManagerBase {
public:
    struct Texture {
        GLuint textureId;
        int textureUnit;
        std::string path;
    };

    TextureManagerBase();
    virtual ~TextureManagerBase();
    
    // Texture management
    int createTexture(const std::string& path);
    
    // Public access to textures for composition users
    std::vector<Texture> m_textures;

protected:
    int m_nextTextureUnit;
    
private:
    // Internal texture loading
    GLuint loadTextureFromFile(const std::string& path);
    
    // Prevent copying
    TextureManagerBase(const TextureManagerBase&) = delete;
    TextureManagerBase& operator=(const TextureManagerBase&) = delete;
};