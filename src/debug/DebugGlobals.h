// DebugGlobals.h
#pragma once

class DebugRenderer;
class IHashable;
class GameBase;

/**
 * @brief RAII guard for managing DebugRenderer lifetime in global state
 * Automatically clears the global pointer when destroyed
 */
class DebugRendererGuard {
public:
    DebugRendererGuard(); // Default constructor (no-op)
    explicit DebugRendererGuard(DebugRenderer* renderer);
    ~DebugRendererGuard();
    
    // Move-only semantics to prevent double-cleanup
    DebugRendererGuard(DebugRendererGuard&& other) noexcept;
    DebugRendererGuard& operator=(DebugRendererGuard&& other) noexcept;
    
    // Delete copy operations
    DebugRendererGuard(const DebugRendererGuard&) = delete;
    DebugRendererGuard& operator=(const DebugRendererGuard&) = delete;
    
private:
    bool m_isActive;
};

/**
 * @brief Global access point for debug renderer
 * Use setDebugRenderer() to get an RAII guard that manages cleanup
 */
class DebugGlobals {
public:
    /**
     * @brief Set debug renderer and return RAII guard
     * @param debugRenderer Pointer to debug renderer (can be nullptr)
     * @return Guard object that will clear the global pointer when destroyed
     */
    static DebugRendererGuard setDebugRenderer(DebugRenderer* debugRenderer) { 
        return DebugRendererGuard(debugRenderer); 
    }
    
    /**
     * @brief Get current debug renderer (may be nullptr)
     * @return Current debug renderer pointer
     */
    static DebugRenderer* getDebugRenderer() { return g_debugRenderer; }

    // Global IHashable pointer for debugging (temporary)
    static IHashable* g_gameBase;
    
private:
    friend class DebugRendererGuard;
    
    // Internal method only accessible by guard
    static void setDebugRendererInternal(DebugRenderer* debugRenderer) { 
        g_debugRenderer = debugRenderer; 
    }

    // Global debug renderer pointer
    static DebugRenderer* g_debugRenderer;
};