# Graphics Engine

The graphics engine is used across other programs. It is not runnable by itself (there is no
main.cpp). A program that needs graphics links to it, owns its own loop and runs it one frame at a
time.

The graphics are 3D. It includes instanced rendering, custom mesh rendering, ray marched volumes,
LOD surface with mapping, and is using Dekker arithmetic for beyond 32 bit floating point precision.

## Requirements

- C++20 compiler
- CMake 3.16 or newer
- OpenGL
- [GLFW 3](https://www.glfw.org/), found with `find_package(glfw3)`
- [Assimp](https://github.com/assimp/assimp), found with `find_package(assimp)`
- [utils](https://github.com/Vipiao/utils), the shared library repo

`utils` is a dependency for this library. It also supplies glm and TimeHandler.

glad and stb_image are vendored in `external/` and need no installation.

## Building

Clone `utils` next to this repository and it is found automatically. If it lives elsewhere, pass
`-DUTILS_DIR=/path/to/utils`.

```sh
git clone https://github.com/Vipiao/utils.git
cmake -B build
cmake --build build
```

## Using it from a game

Add the engine as a subdirectory and link the alias target. Include directories, C++20 and `utils`
all propagate, so a consuming project does not name `utils` itself.

```cmake
add_subdirectory(/path/to/graphics_engine graphics-engine-build)
target_link_libraries(my_game PRIVATE GraphicsEngine::graphics)
```

Then construct the engine and drive it. `example/main.cpp` is a complete spinning cube, built
automatically whenever this repo is configured on its own. In outline:

```cpp
GraphicsEngine engine(&timeHandler, 800, 600, "Spinning cube",
                      10'000, 100, GraphicsEngineBase::Mode::NONE);

// A mesh owns one transform slot in the shared SSBO, and that index is its id.
int meshId = engine.m_ssboManager->allocateIndex();
engine.createMesh(meshId);
engine.appendTrianglesToMesh(meshId, &vertices, &normals, &tangents, &uvs, &colors);

// An identity camera orientation looks along +Y with +Z up.
engine.getCamPos() = glm::dvec3(0.0, -5.0, 1.5);

// Uploaded once. Velocity and angular velocity are per physics step, and the
// last argument is the step this pose belongs to.
engine.updateMeshTransform(
    meshId, position, velocity, orientation,
    spinAxis, 0.05, centreOfRotation, scale, 0, 0.0);

uint64_t step = 0;
while (!glfwWindowShouldClose(engine.getWindow())) {
    engine.beginFrame();
    engine.setRenderParameters(step++, 0.0);   // only this advances
    engine.render();
    engine.endFrame();
}
```

Nothing in the loop touches the transform. Advancing the step is what turns the cube, because the
vertex stage extrapolates the pose from the step stamped into it.

## The frame loop

`beginFrame` opens the frame, polls input and stamps the frame's start time. `render` draws the
scene. `endFrame` presents it and swaps.

Between them, `setRenderParameters(interpolationTimeStep, timeRemainder)` is the seam between a
fixed-rate physics tick and the display's variable rate. The game says which physics step it is on
and how far past it the frame falls; the engine interpolates as it draws. Physics at a fixed 64 Hz
renders at whatever the monitor does, without the game rebuilding its transforms every frame.

## What it does

**Rendering**

- Deferred shading through a G-buffer, Blinn-Phong with a Schlick Fresnel term over roughness and
  metallic
- Screen-space ambient occlusion
- Cascaded shadow maps, with CDLOD patches grouped into tiers so a small cascade does not re-render
  the whole surface
- Weighted blended order-independent transparency
- Panini projection and blue-noise dithering in post

**Geometry**

- CDLOD terrain at planetary scale. The shape is a GLSL snippet supplied by the caller and injected
  into the shader at load time, so the engine does not need to know what the surface is.
- Instanced geometry, per-instance data uploaded through SSBOs
- Ray-marched volumes, also driven by a caller-supplied snippet
- A 2D mesh manager for interface elements
- Model and texture loading through Assimp and stb_image, with a shared texture store

**Precision**

The camera is double precision throughout, `dvec3` and `dquat`. Shaders place geometry far from the
origin with `dekker_arithmetic.glsl`, which carries about 48 bits of mantissa against a float's 24.
It mirrors `DekkerArithmetic.h` in `utils` and is kept in step with it.

**Development**

- `reloadShaders()` recompiles shaders at runtime and reports what failed, so a shader edit does not
  cost a restart
- Input and frame pacing run through a single `TimeHandler` and can be recorded and replayed
- Debug visualisation for geometry that is not part of the scene

## Layout

    example/                       a complete spinning cube
    src/graphics/                  engine sources, one folder per subsystem
    src/graphics/shared_shaders/   GLSL shared between subsystems
    src/debug/                     debug visualisation
    external/glad                  OpenGL loader, generated
    external/stb                   stb_image
    dependency_graph_generator/    module dependency analysis

`dependency_analyzer.py` builds a dependency graph of the source tree. It reads every `.cpp` and
`.h`, takes an edge from each `#include` and from each forward declaration of a class or struct, and
reports any group of files that depend on each other in a circle. Keeping the module graph acyclic
is a design rule here and that script is how it is checked.

It writes GraphML. Drop `cpp_dependencies.graphml` into
[yEd Live](https://www.yworks.com/products/yed-live) to view it in a browser; it runs locally and
does not upload the file.

## Licence

See `COPYRIGHT`. Third-party components keep their own terms.
