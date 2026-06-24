// GraphicsCallbacks.h
#pragma once

#include <glm/glm.hpp>
#include <cstdint>

/**
 * @brief Interface for graphics engine callbacks
 */
class IGraphicsCallbacks {
public:
    virtual ~IGraphicsCallbacks() = default;
    
    virtual void preRenderCallback(uint64_t frameNum) = 0;
    virtual void renderCallback(glm::dmat4 viewMatrix, glm::dmat4 projectionMatrix) = 0;
    virtual void postRenderCallback(uint64_t frameNum) = 0;
    virtual void framebufferSizeCallback(int width, int height) = 0;
    virtual void windowPosCallback(int xpos, int ypos) = 0;
};