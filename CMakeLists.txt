cmake_minimum_required(VERSION 3.16)
project(GraphicsEngine VERSION 0.1.0 LANGUAGES CXX C)   # C is needed for glad.c

# Are we being built on our own, or pulled into a game via add_subdirectory?
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(GRAPHICS_ENGINE_STANDALONE TRUE)
else()
    set(GRAPHICS_ENGINE_STANDALONE FALSE)
endif()

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default build type and the message only matter when building standalone.
# Never force these on a parent project that consumes us.
if(GRAPHICS_ENGINE_STANDALONE)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    message(STATUS "GraphicsEngine build type: ${CMAKE_BUILD_TYPE}")
endif()
# NOTE: optimization (-O2/-O3, /O2) is left to CMake's build-type defaults,
# and the /MD vs /MDd runtime is left to CMake's default so it always matches
# whatever the consuming game uses. -march=native was removed: it bakes in
# CPU instructions that crash on other machines and you don't want it forced
# on a redistributable engine.

# Platform-specific configuration
if(WIN32)
    message(STATUS "Configuring for Windows")
elseif(UNIX)
    message(STATUS "Configuring for Linux")
endif()

# Shared utils library (utils/math/debug + vendored glm). Searched for next to
# this repo first, so cloning the two side by side just works; override
# UTILS_DIR to point elsewhere. When a game pulls in both, the guard keeps
# utils from being added twice.
set(UTILS_DIR "" CACHE PATH "Path to the shared utils library repo")
if(NOT UTILS_DIR)
    foreach(candidate
            "${CMAKE_CURRENT_SOURCE_DIR}/../utils"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../utils"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../03_utils/utils")
        if(EXISTS "${candidate}/CMakeLists.txt")
            set(UTILS_DIR "${candidate}")
            break()
        endif()
    endforeach()
endif()
if(NOT EXISTS "${UTILS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "The utils library was not found. Clone https://github.com/Vipiao/utils "
        "next to this repository, or configure with -DUTILS_DIR=/path/to/utils")
endif()
message(STATUS "GraphicsEngine utils: ${UTILS_DIR}")
if(NOT TARGET utils)
    add_subdirectory(${UTILS_DIR} ${CMAKE_BINARY_DIR}/utils-build)
endif()

# Find packages using CMake's package finder
find_package(assimp REQUIRED)
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

if(NOT TARGET assimp::assimp)
    include_directories(${ASSIMP_INCLUDE_DIRS})
endif()

# Engine sources only — no main.cpp, no physics. CONFIGURE_DEPENDS re-globs
# when files are added so you don't have to re-run cmake by hand.
file(GLOB_RECURSE ENGINE_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/src/glad.c"
)

# The engine is a library, not an executable.
add_library(graphics STATIC ${ENGINE_SRC})
# Namespaced alias — this is the name the game links against.
add_library(GraphicsEngine::graphics ALIAS graphics)

# PUBLIC include dirs propagate to anything that links the engine, so the
# game never has to spell out the engine's folder layout.
# glm comes from the utils library (single vendored copy for the whole stack).
target_include_directories(graphics PUBLIC
    #"${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/glad/include"
)
# stb_image is only used inside STBImageLoader.cpp, so it stays PRIVATE.
target_include_directories(graphics PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/external/stb"
)
if(NOT TARGET assimp::assimp)
    target_include_directories(graphics PRIVATE ${ASSIMP_INCLUDE_DIRS})
endif()

# PUBLIC so consumers compiling engine headers also get C++20.
target_compile_features(graphics PUBLIC cxx_std_20)

# Warnings stay PRIVATE — your strictness shouldn't be forced on the game.
if(MSVC)
    target_compile_options(graphics PRIVATE /W4)
else()
    target_compile_options(graphics PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas)
endif()

# Link deps. glfw/OpenGL are PUBLIC because the engine's public API likely
# exposes them; utils is PUBLIC because its headers (and glm) are part of the
# engine's interface; assimp is PRIVATE since it's only used inside the loaders.
target_link_libraries(graphics
    PUBLIC
        Utils::utils
        glfw
        ${OPENGL_LIBRARIES}
    PRIVATE
        ${ASSIMP_LIBRARIES}
)
# System libs needed when the final game exe links GL/glfw on Windows.
# PUBLIC so they propagate to the consumer's link line.
if(WIN32)
    target_link_libraries(graphics PUBLIC gdi32 user32 shell32)
endif()

target_compile_definitions(graphics PUBLIC
    ENGINE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# The example is only useful when this repo is the top-level project.
if(GRAPHICS_ENGINE_STANDALONE)
    add_subdirectory(example)
endif()
