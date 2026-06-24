// CallbackManager.h
#pragma once

#include "GraphicsCallbacks.h"
#include <vector>
#include <algorithm>
#include <glm/glm.hpp>

/**
 * @brief Manages graphics callbacks
 */
class CallbackManager {
public:
    virtual ~CallbackManager() = default;
    
    virtual void addCallback(IGraphicsCallbacks* callback);
    virtual void removeCallback(IGraphicsCallbacks* callback);

protected:
    void callPreRenderCallbacks(uint64_t frameNum);
    void callRenderCallbacks(glm::dmat4 viewMatrix, glm::dmat4 projectionMatrix);
    void callPostRenderCallbacks(uint64_t frameNum);
    void callFramebufferSizeCallbacks(int width, int height);
    void callWindowPosCallbacks(int xpos, int ypos);

private:
    std::vector<IGraphicsCallbacks*> m_callbacks;
};