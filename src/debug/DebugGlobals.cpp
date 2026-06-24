// DebugGlobals.cpp
#include "DebugGlobals.h"
#include <utility>

DebugRendererGuard::DebugRendererGuard() 
    : m_isActive(false) {
    // Default constructor - does nothing, doesn't set global pointer
}

DebugRendererGuard::DebugRendererGuard(DebugRenderer* renderer) 
    : m_isActive(true) {
    DebugGlobals::setDebugRendererInternal(renderer);
}

DebugRendererGuard::~DebugRendererGuard() {
    if (m_isActive) {
        DebugGlobals::setDebugRendererInternal(nullptr);
    }
}

DebugRendererGuard::DebugRendererGuard(DebugRendererGuard&& other) noexcept 
    : m_isActive(other.m_isActive) {
    other.m_isActive = false; // Transfer ownership
}

DebugRendererGuard& DebugRendererGuard::operator=(DebugRendererGuard&& other) noexcept {
    if (this != &other) {
        if (m_isActive) {
            DebugGlobals::setDebugRendererInternal(nullptr);
        }
        m_isActive = other.m_isActive;
        other.m_isActive = false;
    }
    return *this;
}