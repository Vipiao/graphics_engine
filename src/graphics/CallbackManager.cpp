// CallbackManager.cpp
#include "CallbackManager.h"

void CallbackManager::addCallback(IGraphicsCallbacks* callback) {
    m_callbacks.push_back(callback);
}

void CallbackManager::removeCallback(IGraphicsCallbacks* callback) {
    auto it = std::remove(m_callbacks.begin(), m_callbacks.end(), callback);
    m_callbacks.erase(it, m_callbacks.end());
}

void CallbackManager::callPreRenderCallbacks(uint64_t frameNum) {
    for (auto& callback : m_callbacks) {
        callback->preRenderCallback(frameNum);
    }
}

void CallbackManager::callRenderCallbacks(glm::dmat4 viewMatrix, glm::dmat4 projectionMatrix) {
    for (auto& callback : m_callbacks) {
        callback->renderCallback(viewMatrix, projectionMatrix);
    }
}

void CallbackManager::callPostRenderCallbacks(uint64_t frameNum) {
    for (auto& callback : m_callbacks) {
        callback->postRenderCallback(frameNum);
    }
}

void CallbackManager::callFramebufferSizeCallbacks(int width, int height) {
    for (auto& callback : m_callbacks) {
        callback->framebufferSizeCallback(width, height);
    }
}

void CallbackManager::callWindowPosCallbacks(int xpos, int ypos) {
    for (auto& callback : m_callbacks) {
        callback->windowPosCallback(xpos, ypos);
    }
}