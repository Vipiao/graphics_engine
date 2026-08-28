// TextureStore.cpp
#include "TextureStore.h"
#include "STBImageLoader.h"
#include <cassert>

std::weak_ptr<Texture2D> TextureStore::create(const TextureSpec& spec) {
    const std::shared_ptr<Texture2D> texture{std::make_shared<Texture2D>(spec)};
    m_textures.push_back(texture);
    // Uploading binds the new texture to whichever unit is active and leaves it
    // unbound, so what the mirror believes about that unit no longer holds.
    invalidateBindings();
    return texture;
}

std::weak_ptr<TextureCube> TextureStore::createCube(const CubeTextureSpec& spec) {
    const std::shared_ptr<TextureCube> texture{std::make_shared<TextureCube>(spec)};
    m_textures.push_back(texture);
    invalidateBindings();
    return texture;
}

std::weak_ptr<Texture2D> TextureStore::createFromFile(const std::string& path) {
    const std::unordered_map<std::string, std::weak_ptr<Texture2D>>::iterator cached{
        m_fileCache.find(path)};
    if (cached != m_fileCache.end() && !cached->second.expired()) {
        return cached->second;
    }

    // Throws if the file cannot be read, so no texture is created for a failed
    // load.
    int width{0};
    int height{0};
    int channelCount{0};
    unsigned char* pixels{STBImageLoader::load(true, path, &width, &height, &channelCount)};
    assert((channelCount == 3 || channelCount == 4) && "texture must be RGB or RGBA");

    TextureSpec spec{};
    spec.m_width = width;
    spec.m_height = height;
    spec.m_format =
        channelCount == 3 ? TextureFormat::RGB8 : TextureFormat::RGBA8;
    spec.m_generateMipmaps = true;
    spec.m_pixels = pixels;

    std::weak_ptr<Texture2D> texture{};
    try {
        texture = create(spec);
    } catch (...) {
        STBImageLoader::free(pixels);
        throw;
    }
    STBImageLoader::free(pixels);

    m_fileCache[path] = texture;
    return texture;
}

void TextureStore::remove(std::weak_ptr<Texture> textureWeak) {
    const std::shared_ptr<Texture> texture{textureWeak.lock()};
    if (!texture) return;

    for (std::vector<std::shared_ptr<Texture>>::iterator it{m_textures.begin()};
         it != m_textures.end(); ++it) {
        if (*it == texture) {
            m_textures.erase(it);
            return;
        }
    }
}

void TextureStore::bindTexture(int unit, const Texture& texture) {
    assert(unit >= 0 && unit < ShaderProgram::s_maxTextureUnits &&
           "Binding outside the units the shaders declare");

    if (m_boundUnits[unit] == texture.getID()) return;

    // The texture rather than its handle, a bind needing the target and nothing
    // being able to recover that from the handle alone.
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(texture.getTarget(), texture.getID());
    m_boundUnits[unit] = texture.getID();
}

void TextureStore::invalidateBindings() {
    m_boundUnits.fill(0);
}
