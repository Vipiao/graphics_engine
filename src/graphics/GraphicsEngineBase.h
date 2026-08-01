#pragma once

#include <chrono>
#include <memory>
#include <functional>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "MouseHandler.h"
#include "KeyboardHandler.h"

class TimeHandler;

class GraphicsEngineBase {
public:
   enum class Mode { NONE, RECORD, PLAY };
   // The TimeHandler is the engine's only clock. Frame timing is recorded and
   // replayed through it, exactly as mouse and keyboard state already are, so a
   // replayed session reproduces the frame pacing the recorded one ran at.
   GraphicsEngineBase(TimeHandler* timeHandler, Mode mode = Mode::NONE,
      const std::filesystem::path& filepath = "recording_mouse_keyboard");
   ~GraphicsEngineBase();
   GraphicsEngineBase(const GraphicsEngineBase&) = delete;
   GraphicsEngineBase& operator= (const GraphicsEngineBase&) = delete;

   void setWindowPos(int xPos, int yPos);
   void setSwapInterval(int swapInterval);
   
   // Frame control methods
   bool shouldClose() const { return glfwWindowShouldClose(m_window); }
   void clearScreen();
   // Opens a frame: samples its start and derives the rate the previous one
   // achieved. Call once per frame, after clearScreen has cleared the vsync
   // throttle, so the sample lands at the frame's true start.
   void updateFrameTiming();
   void updateInput();
   void calculateCameraVelocity();
   glm::dmat4 getViewMatrix() const;
   glm::dmat4 getProjectionMatrix() const;
   double getAspectRatio() const;
   // Single owner of the Panini fit scale, derived from the same members as
   // the projection matrix. Consumers (post pass, screen anchors) must use
   // this value rather than re-deriving it.
   double getPaniniFitScale() const;
   void checkGLErrors();
   void swapBuffers();
   void toggleFullscreen();
   void incrementFrame() { m_frameNum++; }
   void setTriangleRenderMode(bool useTriangles);
   bool getTriangleRenderMode();

   // Apply the actual framebuffer size to the viewport, screen size members,.
   // Call this after all framebufferSize callbacks are setup.
   void applyInitialFramebufferSize();

   GLFWwindow* m_window{ nullptr };
   unsigned int m_screen_width{ 800 };
   unsigned int m_screen_height{ 600 };
   glm::dvec3 m_camPos{ 0,0,0 };
   glm::dvec3 m_camVel{};
   // The monitor mode's refresh rate. This is what the display *can* do, not
   // what this window achieves: a compositor throttles a windowed surface well
   // below it. Use it only to pace against the display; anything converting
   // between seconds and frames wants m_measuredFrameRate.
   int m_monitorRefreshRate{ 0 };
   // Frames per second actually achieved, measured in updateFrameTiming.
   // Instantaneous, never smoothed, so a value advanced by 1/m_measuredFrameRate
   // each frame tracks elapsed time exactly.
   double m_measuredFrameRate{ 0.0 };
   // Start of the current frame; the single frame-start timestamp, so
   // everything pacing off the frame agrees on when it began.
   std::chrono::time_point<std::chrono::high_resolution_clock> m_frameStartTime{};
   //glm::dquat m_camOri{ glm::sqrt(2.) / 2., -glm::sqrt(2.) / 2.,0,0 }; // 90% rotation around negative x axis.
   glm::dquat m_camOri{ 1,0,0,0 }; // Unit orientation.
   uint64_t m_frameNum{ 0 };
   int m_swapInterval{ 1 }; // 0 = vsync off, 1 = vsync on.
   double m_fieldOfView{ glm::radians(120.0) }; // Horizontal field of view.
   // Panini projection strengths. 0 = standard rectilinear (off), 1 = max distortion.
   double m_paniniHorizontal{ 0.0 };
   double m_paniniVertical{ 0.0 };
   // Blue-noise dither amplitude for the post-processing pass, in color
   // units. 0 = off; the default of 1.0 / 255.0 covers exactly one 8-bit
   // quantization step, dissolving banding without visible grain.
   double m_ditherStrength{ 1.0 / 255.0 };
   // Mouse.
   MouseHandler* m_mouseHandler{ nullptr };
   KeyboardHandler* m_keyboardHandler{ nullptr };

   // Framebuffer and window callbacks remain for window system events
   void registerFramebufferCallback(std::function<void(int, int)> callback) { m_framebufferCallbacks.push_back(callback); }
   void registerWindowPosCallback(std::function<void(int, int)> callback) { m_windowPosCallbacks.push_back(callback); }

protected:
   static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
   static void windowPosCallback(GLFWwindow* window, int xpos, int ypos);
   int queryMonitorRefreshRate();
   GLFWmonitor* getCurrentMonitor();

   // Bounds on the measured frame delta. A window the compositor has stopped
   // sending frame callbacks to (occluded, another workspace) resumes with an
   // arbitrarily long gap, which must not become an arbitrarily long time step.
   static constexpr double k_minFrameDelta{ 0.001 };  // 1000 fps
   static constexpr double k_maxFrameDelta{ 0.1 };    // 10 fps

   TimeHandler* m_timeHandler{ nullptr };

   bool m_renderTriangleMode{ false };
   bool m_windowOnTop{ false };

   // Windowed geometry captured by toggleFullscreen() when entering
   // fullscreen; only valid while fullscreen.
   int m_windowedPosX{};
   int m_windowedPosY{};
   int m_windowedWidth{};
   int m_windowedHeight{};

   glm::dvec3 m_camPosPrev{};

   std::vector<std::function<void(int, int)>> m_framebufferCallbacks;
   std::vector<std::function<void(int, int)>> m_windowPosCallbacks;
};

