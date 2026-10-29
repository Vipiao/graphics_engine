// ImpostorBaker.cpp
#include "ImpostorBaker.h"

#include <cassert>
#include <cstddef>
#include <deque>
#include <stdexcept>
#include <vector>
#include "../ShaderProgram.h"
#include "../Texture2D.h"
#include "../Texture2DArray.h"
#include "../TextureStore.h"
#include "utils/PngWriter.h"

namespace {

constexpr const char* s_vertexPath =
    ENGINE_ASSET_DIR "/src/graphics/impostor/impostor_bake.vert";
constexpr const char* s_fragmentPath =
    ENGINE_ASSET_DIR "/src/graphics/impostor/impostor_bake.frag";

constexpr int k_channels{4};
// On units 0 to 3, in ImpostorMaterial's order
constexpr int k_materialTextures{4};

// The GL state a bake changes.
struct SavedState {
    GLint m_drawFramebuffer{0};
    GLint m_readFramebuffer{0};
    GLint m_viewport[4]{};
    GLfloat m_clearColor[4]{};
    GLboolean m_cullFace{GL_FALSE};
    GLboolean m_blend{GL_FALSE};
    GLboolean m_scissor{GL_FALSE};
    GLboolean m_depthTest{GL_FALSE};
    GLboolean m_depthMask{GL_TRUE};
    GLint m_depthFunc{GL_LESS};
    GLfloat m_clearDepth{1.0f};

    static SavedState capture() {
        SavedState state{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.m_drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state.m_readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, state.m_viewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, state.m_clearColor);
        state.m_cullFace = glIsEnabled(GL_CULL_FACE);
        state.m_blend = glIsEnabled(GL_BLEND);
        state.m_scissor = glIsEnabled(GL_SCISSOR_TEST);
        state.m_depthTest = glIsEnabled(GL_DEPTH_TEST);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &state.m_depthMask);
        glGetIntegerv(GL_DEPTH_FUNC, &state.m_depthFunc);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &state.m_clearDepth);
        return state;
    }

    void restore() const {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(m_drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(m_readFramebuffer));
        glViewport(m_viewport[0], m_viewport[1], m_viewport[2], m_viewport[3]);
        glClearColor(m_clearColor[0], m_clearColor[1], m_clearColor[2], m_clearColor[3]);
        setEnabled(GL_CULL_FACE, m_cullFace);
        setEnabled(GL_BLEND, m_blend);
        setEnabled(GL_SCISSOR_TEST, m_scissor);
        setEnabled(GL_DEPTH_TEST, m_depthTest);
        glDepthMask(m_depthMask);
        glDepthFunc(static_cast<GLenum>(m_depthFunc));
        glClearDepth(m_clearDepth);
    }

    static void setEnabled(GLenum capability, GLboolean enabled) {
        if (enabled) {
            glEnable(capability);
        } else {
            glDisable(capability);
        }
    }
};

GLuint createColorTarget(int side, GLint internalFormat) {
    GLuint texture{0};
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// k_channels channels, or one for GL_RED.
std::vector<uint8_t> readTarget(GLenum attachment, int side, GLenum format = GL_RGBA) {
    const size_t channels{format == GL_RED ? size_t{1} : size_t{k_channels}};
    std::vector<uint8_t> pixels(static_cast<size_t>(side) * side * channels);
    GLint previousAlignment{4};
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(attachment);
    glReadPixels(0, 0, side, side, format, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    return pixels;
}

// Breadth-first flood from the covered texels: every see-through texel takes the
// colour, normal, roughness and depth of the nearest covered one in its cell.
// Coverage is kept.
void fillSeeThrough(std::vector<uint8_t>& color, std::vector<uint8_t>& normal,
                    std::vector<uint8_t>& depth, int side, int cellPixels) {
    const size_t texels{static_cast<size_t>(side) * side};
    std::vector<bool> reached(texels, false);
    std::deque<size_t> frontier{};

    for (size_t texel{0}; texel < texels; ++texel) {
        if (color[texel * k_channels + 3] > 0) {
            reached[texel] = true;
            frontier.push_back(texel);
        }
    }

    const int steps[4][2]{{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    while (!frontier.empty()) {
        const size_t texel{frontier.front()};
        frontier.pop_front();
        const int x{static_cast<int>(texel % side)};
        const int y{static_cast<int>(texel / side)};

        for (const auto& step : steps) {
            const int nextX{x + step[0]};
            const int nextY{y + step[1]};
            if (nextX < 0 || nextY < 0 || nextX >= side || nextY >= side ||
                nextX / cellPixels != x / cellPixels || nextY / cellPixels != y / cellPixels) {
                continue;
            }
            const size_t next{static_cast<size_t>(nextY) * side + nextX};
            if (reached[next]) continue;

            reached[next] = true;
            // Colour alpha is coverage and stays; normal alpha is roughness
            for (int channel{0}; channel < 3; ++channel) {
                color[next * k_channels + channel] = color[texel * k_channels + channel];
            }
            for (int channel{0}; channel < k_channels; ++channel) {
                normal[next * k_channels + channel] = normal[texel * k_channels + channel];
            }
            depth[next] = depth[texel];
            frontier.push_back(next);
        }
    }
}

std::weak_ptr<Texture2D> upload(TextureStore& store, const std::vector<uint8_t>& pixels,
                                int side, TextureFormat format = TextureFormat::RGBA8) {
    TextureSpec spec{};
    spec.m_width = side;
    spec.m_height = side;
    spec.m_format = format;
    spec.m_wrap = TextureSpec::Wrap::ClampToEdge;
    spec.m_filter = TextureFilter::Linear;
    spec.m_generateMipmaps = true;
    spec.m_pixels = pixels.data();
    return store.create(spec);
}

// Level 0, bottom row first.
std::vector<uint8_t> readTexture(const Texture2D& texture, int& side) {
    glBindTexture(GL_TEXTURE_2D, texture.getID());
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &side);
    std::vector<uint8_t> pixels(static_cast<size_t>(side) * side * k_channels);
    GLint previousAlignment{4};
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    glBindTexture(GL_TEXTURE_2D, 0);
    return pixels;
}

// 16-bit samples, top row first.
std::vector<uint16_t> pngSamples(const std::vector<uint8_t>& pixels, int side, int firstChannel,
                                 int channelCount) {
    std::vector<uint16_t> samples{};
    samples.reserve(static_cast<size_t>(side) * side * channelCount);
    for (int row{side - 1}; row >= 0; --row) {
        for (int column{0}; column < side; ++column) {
            const size_t texel{static_cast<size_t>(row) * side + column};
            for (int channel{firstChannel}; channel < firstChannel + channelCount; ++channel) {
                // 255 * 257 = 65535
                const uint8_t sample{pixels[texel * k_channels + channel]};
                samples.push_back(static_cast<uint16_t>(sample * 257));
            }
        }
    }
    return samples;
}

}  // namespace

ImpostorBaker::ImpostorBaker(TextureStore* textureStore) : m_textureStore{textureStore} {
    if (!m_textureStore) {
        throw std::runtime_error("ImpostorBaker: TextureStore cannot be null");
    }
    m_program = std::make_unique<ShaderProgram>();
    m_program->addDefine("MATERIAL_TEXTURE_ARRAYS", "");
    m_program->loadVertexShaderFromPath(s_vertexPath);
    m_program->loadFragmentShaderFromPath(s_fragmentPath);
    m_program->linkShaders();
}

// Here, where ShaderProgram is a complete type, so the unique_ptr can delete it.
ImpostorBaker::~ImpostorBaker() = default;

ImpostorAtlas ImpostorBaker::bake(const ImpostorMesh& mesh, const ImpostorMaterial& material,
                                  const ImpostorBakeSpec& spec) {
    if (spec.m_viewsPerSide < 2 || spec.m_viewsPerSide % 2 != 0) {
        throw std::invalid_argument("ImpostorBaker::bake: views per side must be even");
    }
    if (spec.m_viewPixels <= 2 * spec.m_borderPixels || spec.m_borderPixels < 0) {
        throw std::invalid_argument("ImpostorBaker::bake: the border leaves no picture");
    }
    if (mesh.m_radius <= 0.0f) {
        throw std::invalid_argument("ImpostorBaker::bake: the mesh needs a bounding sphere");
    }
    if (mesh.m_vao == 0 || mesh.m_indexCount == 0) {
        throw std::invalid_argument("ImpostorBaker::bake: the mesh has nothing to draw");
    }

    assert(material.m_alphaCutoff > 0.0f && material.m_alphaCutoff <= 1.0f &&
           "a cutoff of 0 keeps see-through texels, above 1 cuts everything");

    const int side{spec.m_viewsPerSide * spec.m_viewPixels};
    const SavedState saved{SavedState::capture()};

    GLuint framebuffer{0};
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    const GLuint colorTarget{createColorTarget(side, GL_RGBA8)};
    const GLuint normalTarget{createColorTarget(side, GL_RGBA8)};
    const GLuint surfaceDepthTarget{createColorTarget(side, GL_R8)};
    GLuint depthTarget{0};
    glGenRenderbuffers(1, &depthTarget);
    glBindRenderbuffer(GL_RENDERBUFFER, depthTarget);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, side, side);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normalTarget, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D,
                           surfaceDepthTarget, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthTarget);
    const GLenum drawBuffers[3]{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
                                GL_COLOR_ATTACHMENT2};
    glDrawBuffers(3, drawBuffers);
    const auto release{[&] {
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &colorTarget);
        glDeleteTextures(1, &normalTarget);
        glDeleteTextures(1, &surfaceDepthTarget);
        glDeleteRenderbuffers(1, &depthTarget);
        saved.restore();
        m_textureStore->invalidateBindings();
    }};
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        release();
        throw std::runtime_error("ImpostorBaker::bake: the bake framebuffer is incomplete");
    }

    // Reverse-Z. Seen from both sides, so nothing is culled.
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GEQUAL);
    glClearDepth(0.0);
    glViewport(0, 0, side, side);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    m_program->use();
    m_program->setUniformInt("u_viewsPerSide", spec.m_viewsPerSide);
    m_program->setUniformVec3("u_centre", glm::dvec3{mesh.m_centre});
    m_program->setUniformFloat("u_radius", mesh.m_radius);
    m_program->setUniformVec4("u_color", glm::dvec4{material.m_color});
    m_program->setUniformFloat("u_alphaCutoff", material.m_alphaCutoff);

    // Other passes bind textures directly
    m_textureStore->invalidateBindings();
    glm::ivec4 units{-1};
    for (int unit{0}; unit < k_materialTextures; ++unit) {
        const std::shared_ptr<Texture2DArray> texture{material.m_textures[unit].lock()};
        if (!texture) continue;
        m_textureStore->bindTexture(unit, *texture);
        const GLint location{m_program->getTextureUnitLocation(unit)};
        if (location != -1) {
            glUniform1i(location, unit);
        }
        units[unit] = unit;
    }
    glUniform4i(glGetUniformLocation(m_program->getID(), "u_textureUnits"), units.x, units.y,
                units.z, units.w);

    glBindVertexArray(mesh.m_vao);
    const int picturePixels{spec.m_viewPixels - 2 * spec.m_borderPixels};
    for (int row{0}; row < spec.m_viewsPerSide; ++row) {
        for (int column{0}; column < spec.m_viewsPerSide; ++column) {
            glViewport(column * spec.m_viewPixels + spec.m_borderPixels,
                       row * spec.m_viewPixels + spec.m_borderPixels, picturePixels,
                       picturePixels);
            glUniform2i(glGetUniformLocation(m_program->getID(), "u_cell"), column, row);
            glDrawElementsBaseVertex(
                GL_TRIANGLES, static_cast<GLsizei>(mesh.m_indexCount), GL_UNSIGNED_INT,
                reinterpret_cast<void*>(static_cast<uintptr_t>(mesh.m_firstIndex) *
                                        sizeof(uint32_t)),
                mesh.m_baseVertex);
        }
    }
    glBindVertexArray(0);

    std::vector<uint8_t> color{readTarget(GL_COLOR_ATTACHMENT0, side)};
    std::vector<uint8_t> normal{readTarget(GL_COLOR_ATTACHMENT1, side)};
    std::vector<uint8_t> depth{readTarget(GL_COLOR_ATTACHMENT2, side, GL_RED)};

    release();

    fillSeeThrough(color, normal, depth, side, spec.m_viewPixels);

    ImpostorAtlas atlas{};
    atlas.m_color = upload(*m_textureStore, color, side);
    atlas.m_normal = upload(*m_textureStore, normal, side);
    atlas.m_depth = upload(*m_textureStore, depth, side, TextureFormat::R8);
    atlas.m_spec = spec;
    atlas.m_centre = mesh.m_centre;
    atlas.m_radius = mesh.m_radius;
    return atlas;
}

bool ImpostorBaker::writeImages(const ImpostorAtlas& atlas,
                                const std::filesystem::path& directory,
                                const std::string& name) {
    const std::shared_ptr<Texture2D> colorTexture{atlas.m_color.lock()};
    const std::shared_ptr<Texture2D> normalTexture{atlas.m_normal.lock()};
    const std::shared_ptr<Texture2D> depthTexture{atlas.m_depth.lock()};
    if (!colorTexture || !normalTexture || !depthTexture) return false;

    int side{0};
    int normalSide{0};
    int depthSide{0};
    const std::vector<uint8_t> color{readTexture(*colorTexture, side)};
    const std::vector<uint8_t> normal{readTexture(*normalTexture, normalSide)};
    const std::vector<uint8_t> depth{readTexture(*depthTexture, depthSide)};
    assert(side == normalSide && side == depthSide && "the atlases are baked at one size");

    bool written{PngWriter::writeRgb16(directory / (name + "_color.png"), side, side,
                                       pngSamples(color, side, 0, 3))};
    written = PngWriter::writeRgb16(directory / (name + "_normal.png"), side, side,
                                    pngSamples(normal, side, 0, 3)) && written;
    written = PngWriter::writeGrey16(directory / (name + "_coverage.png"), side, side,
                                     pngSamples(color, side, 3, 1)) && written;
    written = PngWriter::writeGrey16(directory / (name + "_depth.png"), side, side,
                                     pngSamples(depth, side, 0, 1)) && written;
    return written;
}
