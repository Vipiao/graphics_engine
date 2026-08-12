// TextureStore.h
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Texture2D.h"
#include "ShaderProgram.h"

/**
 * @brief The one owner of every content texture, however it was produced.
 *
 * Takes pixels, not sources: a file is loaded into pixels before it gets here,
 * so a height field generated at startup and an artist's PNG are the same kind
 * of thing by the time the store sees them. Nothing here knows what a texture
 * depicts or which pass will sample it.
 *
 * Handles are weak, as elsewhere in the engine: the store is the only owner, a
 * handle to a removed texture expires visibly instead of dangling, and two
 * subsystems can name the same texture without either having to free it.
 *
 * Render targets are deliberately not here. They are framebuffer plumbing sized
 * to the window rather than content, and nothing ever shares one.
 */
class TextureStore {
public:
    std::weak_ptr<Texture2D> create(const TextureSpec& spec);

    // The same, with the pixels read off disk first. Asking twice for one path
    // returns what was already loaded rather than a second copy of it -- the
    // only reason the store knows paths exist at all.
    std::weak_ptr<Texture2D> createFromFile(const std::string& path);

    void remove(std::weak_ptr<Texture2D> texture);

    /**
     * @brief Binds a texture to a unit, skipping the call if it is already there.
     *
     * Consecutive draws overwhelmingly want the same textures -- every part of a
     * character wears the same atlas -- so most binds a renderer issues would
     * change nothing. GL cannot be asked what is bound without stalling, so this
     * mirrors the binding state instead and compares against the mirror.
     *
     * There is one GL binding state, so there is one mirror, here rather than
     * per renderer.
     */
    void bindTexture(int unit, GLuint textureId);

    /**
     * @brief Forgets what is bound, so the next bind of each unit is issued.
     *
     * The mirror is only true while every bind goes through it, and the
     * G-buffer and shadow passes bind their own targets directly. A renderer
     * calls this as it starts binding, which costs one redundant bind per unit
     * and cannot silently sample whatever the previous pass left behind.
     */
    void invalidateBindings();

private:
    std::vector<std::shared_ptr<Texture2D>> m_textures;
    // What each unit currently holds; 0 means "unknown or nothing".
    std::array<GLuint, ShaderProgram::s_maxTextureUnits> m_boundUnits{};
    // Weakly held, so a removed texture drops out of the cache on its own
    // rather than being handed back expired.
    std::unordered_map<std::string, std::weak_ptr<Texture2D>> m_fileCache;
};
