// TextureStore.cpp
#include "TextureStore.h"
#include "STBImageLoader.h"
#include <cassert>
#include <stdexcept>

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

std::weak_ptr<Texture2DArray> TextureStore::createArrayFromFiles(
    const std::vector<std::string>& paths, const std::array<uint8_t, 4>& fill) {
    int width{0};
    int height{0};
    // Expanded to RGBA, so layers from RGB and RGBA files can share the array
    std::vector<std::vector<uint8_t>> loaded(paths.size());
    for (size_t layer{0}; layer < paths.size(); ++layer) {
        if (paths[layer].empty()) continue;

        int layerWidth{0};
        int layerHeight{0};
        int channelCount{0};
        unsigned char* pixels{
            STBImageLoader::load(true, paths[layer], &layerWidth, &layerHeight, &channelCount)};
        const bool sameSize{width == 0 || (layerWidth == width && layerHeight == height)};
        if (!sameSize || (channelCount != 3 && channelCount != 4)) {
            STBImageLoader::free(pixels);
            throw std::runtime_error("TextureStore::createArrayFromFiles: " + paths[layer] +
                                     " must be RGB or RGBA and the size of the other layers");
        }
        width = layerWidth;
        height = layerHeight;

        const size_t texelCount{static_cast<size_t>(width) * height};
        loaded[layer].resize(texelCount * 4);
        for (size_t texel{0}; texel < texelCount; ++texel) {
            for (int channel{0}; channel < 4; ++channel) {
                loaded[layer][texel * 4 + channel] =
                    channel < channelCount ? pixels[texel * channelCount + channel] : 255;
            }
        }
        STBImageLoader::free(pixels);
    }
    if (width == 0) {
        throw std::runtime_error("TextureStore::createArrayFromFiles: no file to size it by");
    }

    TextureArraySpec spec{};
    spec.m_width = width;
    spec.m_height = height;
    spec.m_format = TextureFormat::RGBA8;
    spec.m_generateMipmaps = true;
    for (std::vector<uint8_t>& layer : loaded) {
        if (layer.empty()) {
            layer.resize(static_cast<size_t>(width) * height * 4);
            for (size_t byte{0}; byte < layer.size(); ++byte) {
                layer[byte] = fill[byte % 4];
            }
        }
        spec.m_layers.push_back(layer.data());
    }

    const std::shared_ptr<Texture2DArray> texture{std::make_shared<Texture2DArray>(spec)};
    m_textures.push_back(texture);
    invalidateBindings();
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
