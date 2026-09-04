// A spinning cube, and the smallest complete use of the engine.
//
// The mesh transform is uploaded once, before the loop. Nothing in the loop
// touches it. Velocity and angular velocity are per physics step, and the
// vertex stage extrapolates from the step stamped into the transform, so
// advancing setRenderParameters is what makes the cube turn.

#include "graphics/GraphicsEngine.h"
#include "graphics/SSBOManager.h"
#include "utils/TimeHandler.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace {

struct Cube {
    std::vector<glm::dvec3> vertices, normals, tangents;
    std::vector<glm::dvec2> uvs;
    std::vector<glm::dvec4> colors;

    // Geometry is a triangle soup: three vertices per triangle, no indices.
    void quad(glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::dvec3 d,
              glm::dvec3 n, glm::dvec3 t, glm::dvec4 col) {
        const glm::dvec3 p[6] = {a, b, c, a, c, d};
        const glm::dvec2 u[6] = {{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
        for (int i = 0; i < 6; ++i) {
            vertices.push_back(p[i]);
            normals.push_back(n);
            tangents.push_back(t);
            uvs.push_back(u[i]);
            colors.push_back(col);
        }
    }

    Cube() {
        const double s = 1.0;
        quad({ s,-s,-s},{ s, s,-s},{ s, s, s},{ s,-s, s}, { 1, 0, 0},{0,1,0}, {0.9,0.1,0.1,1});
        quad({-s, s,-s},{-s,-s,-s},{-s,-s, s},{-s, s, s}, {-1, 0, 0},{0,1,0}, {0.1,0.9,0.1,1});
        quad({ s, s,-s},{-s, s,-s},{-s, s, s},{ s, s, s}, { 0, 1, 0},{1,0,0}, {0.1,0.1,0.9,1});
        quad({-s,-s,-s},{ s,-s,-s},{ s,-s, s},{-s,-s, s}, { 0,-1, 0},{1,0,0}, {0.9,0.9,0.1,1});
        quad({-s,-s, s},{ s,-s, s},{ s, s, s},{-s, s, s}, { 0, 0, 1},{1,0,0}, {0.9,0.1,0.9,1});
        quad({-s, s,-s},{ s, s,-s},{ s,-s,-s},{-s,-s,-s}, { 0, 0,-1},{1,0,0}, {0.1,0.9,0.9,1});
    }
};

} // namespace

int main() {
    TimeHandler timeHandler;

    GraphicsEngine engine(&timeHandler, 800, 600, "Spinning cube",
                          10'000, 100, GraphicsEngineBase::Mode::NONE);

    // A mesh owns one transform slot in the shared SSBO, and its index is the
    // mesh id everything else is addressed by.
    int meshId = engine.m_ssboManager->allocateIndex();
    engine.createMesh(meshId);

    Cube cube;
    engine.appendTrianglesToMesh(meshId, &cube.vertices, &cube.normals,
                                 &cube.tangents, &cube.uvs, &cube.colors);

    // An identity camera orientation looks along +Y with +Z up.
    engine.getCamPos() = glm::dvec3(0.0, -5.0, 1.5);
    engine.getCamOri() = glm::dquat(1.0, 0.0, 0.0, 0.0);

    engine.updateMeshTransform(
        meshId,
        glm::dvec3(0.0),                            // position
        glm::dvec3(0.0),                            // velocity, per step
        glm::dquat(1.0, 0.0, 0.0, 0.0),             // orientation
        glm::normalize(glm::dvec3(0.3, 0.2, 1.0)),  // spin axis
        0.05,                                       // radians per step
        glm::dvec3(0.0),                            // centre of rotation
        glm::dvec3(1.0),                            // scale
        0,                                          // step this pose belongs to
        0.0);                                       // emissive; 0 to be lit

    uint64_t step = 0;
    while (!glfwWindowShouldClose(engine.getWindow())) {
        engine.beginFrame();
        engine.setRenderParameters(step++, 0.0);
        engine.render();
        engine.endFrame();
    }
}
