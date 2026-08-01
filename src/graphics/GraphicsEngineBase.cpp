#include "GraphicsEngineBase.h"
#include "utils/TimeHandler.h"
#include <cassert>
#include "math/PaniniProjection.h"

#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <filesystem>
#include <algorithm>

static void glfwErrorCallback(int error, const char* description) {
   std::cout << "GLFW error " << error << ": " << description << std::endl;
}

// Request discrete GPU on Windows (NVIDIA Optimus / AMD PowerXpress)
// Theoretically windows is supposed to detect this itself, but experiments
// shows it often fails at this, especially for openGL applications.
// This is just a hint, so does not overwrite user specification.
// For linux I do this in the constructor.
#ifdef _WIN32
extern "C" {
   __declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
   __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

void GraphicsEngineBase::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
   GraphicsEngineBase* graphicsEngine{ static_cast<GraphicsEngineBase*>(glfwGetWindowUserPointer(window)) };
   glfwMakeContextCurrent(graphicsEngine->m_window);
   glViewport(0, 0, width, height);
   graphicsEngine->m_screen_width = width;
   graphicsEngine->m_screen_height = height;
   graphicsEngine->m_monitorRefreshRate = graphicsEngine->queryMonitorRefreshRate();
   if (graphicsEngine->m_monitorRefreshRate == 0) {
      std::cout << "Warning: Failed to get framerate from monitor. Guessing it is 60 fps." << std::endl;
      graphicsEngine->m_monitorRefreshRate = 60;
   }
   for (auto& callback : graphicsEngine->m_framebufferCallbacks) {
      callback(width, height);
   }
}

void GraphicsEngineBase::windowPosCallback(GLFWwindow* window, int xpos, int ypos) {
   GraphicsEngineBase* graphicsEngine{ static_cast<GraphicsEngineBase*>(glfwGetWindowUserPointer(window)) };
   glfwMakeContextCurrent(graphicsEngine->m_window);
   graphicsEngine->m_monitorRefreshRate = graphicsEngine->queryMonitorRefreshRate();
   if (graphicsEngine->m_monitorRefreshRate == 0) {
      std::cout << "Warning: Failed to get framerate from monitor. Guessing it is 60 fps." << std::endl;
      graphicsEngine->m_monitorRefreshRate = 60;
   }
   for (auto& callback : graphicsEngine->m_windowPosCallbacks) {
      callback(xpos, ypos);
   }
}

GraphicsEngineBase::GraphicsEngineBase(TimeHandler* timeHandler, Mode mode,
   const std::filesystem::path& filepath)
   : m_timeHandler{ timeHandler } {
   assert(m_timeHandler != nullptr);
   // Force discrete GPU on Linux systems (works for NVIDIA, AMD, Intel Arc)
   #ifdef __linux__
   // Try to use gpu instead of integrated gpu. 0 means do not overwrite user settings.
   // For windws see top of file.
   setenv("DRI_PRIME", "1", 0);
   setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
   setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
   #endif

   // macOS: No action needed - automatically handled
   #ifdef __APPLE__
   // macOS does this automatically.
   #endif
   
   // FreeBSD/OpenBSD: Same as Linux. I am not sure if my application supports FreeBSD though.
   // Maybe will try to compile it for it in the future for fun, but keep this just in case.
   #if defined(__FreeBSD__) || defined(__OpenBSD__)
   setenv("DRI_PRIME", "1", 0);
   setenv("__NV_PRIME_RENDER_OFFLOAD", "1", 0);
   setenv("__GLX_VENDOR_LIBRARY_NAME", "nvidia", 0);
   #endif

   // glfw: initialize and configure
   // ------------------------------
   glfwSetErrorCallback(glfwErrorCallback);
   glfwInit();

   const char* platformName{ "unknown" };
   switch (glfwGetPlatform()) {
      case GLFW_PLATFORM_WAYLAND: platformName = "Wayland"; break;
      case GLFW_PLATFORM_X11:     platformName = "X11";     break;
      case GLFW_PLATFORM_WIN32:   platformName = "Win32";   break;
      case GLFW_PLATFORM_COCOA:   platformName = "Cocoa";   break;
   }
   std::cout << "GLFW platform: " << platformName << std::endl;
   glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
   glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
   glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef FLOATING_WINDOW
   glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
#endif

#ifdef __APPLE__
   glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

   glfwWindowHint(GLFW_STENCIL_BITS, 8); // Request 8 stencil bits

   // Try to bypass compositor on Linux
   //glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
   //glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

   // glfw window creation
   // --------------------
   m_window = glfwCreateWindow(m_screen_width, m_screen_height, "LearnOpenGL", NULL, NULL);
   if (m_window == NULL) {
      glfwTerminate();
      std::cout << "Failed to create GLFW window" << std::endl;
      throw "Failed to create GLFW window";
   }
   glfwMakeContextCurrent(m_window);
   glfwSetWindowUserPointer(m_window, this);
   glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
   glfwSetWindowPosCallback(m_window, windowPosCallback);

   // glad: load all OpenGL function pointers
   // ---------------------------------------
   if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
      glfwTerminate();
      std::cout << "Failed to initialize GLAD" << std::endl;
      throw "Failed to initialize GLAD";
   }

   // Clobal configuration.
   setSwapInterval(m_swapInterval);
   glEnable(GL_DEPTH_TEST);
   glEnable(GL_CULL_FACE);
   //glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
   //glEnable(GL_BLEND);
   //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   // Mouse.
   MouseHandler::Mode modeMouseHandler{ MouseHandler::Mode::NONE };
   KeyboardHandler::Mode modeKeyboardHandler{ KeyboardHandler::Mode::NONE };
   if (mode == Mode::RECORD) {
      modeMouseHandler = MouseHandler::Mode::RECORD;
      modeKeyboardHandler = KeyboardHandler::Mode::RECORD;
   } else if (mode == Mode::PLAY) {
      modeMouseHandler = MouseHandler::Mode::PLAY;
      modeKeyboardHandler = KeyboardHandler::Mode::PLAY;
   }
   m_mouseHandler = new MouseHandler{ m_window, modeMouseHandler, filepath / "mouse_data.bin" };
   m_keyboardHandler = new KeyboardHandler{ m_window, modeKeyboardHandler, filepath / "keyboard_data.bin" };

   //
#ifdef FULLSCREEN
   glfwMaximizeWindow(m_window);
#endif //FULLSCREEN

   m_monitorRefreshRate = queryMonitorRefreshRate();
   if (m_monitorRefreshRate == 0) {
      std::cout << "Warning: Failed to get framerate from monitor. Guessing it is 60 fps." << std::endl;
      m_monitorRefreshRate = 60;
   }
   // Seeded from the mode until updateFrameTiming measures a real frame.
   m_measuredFrameRate = static_cast<double>(m_monitorRefreshRate);
}

GraphicsEngineBase::~GraphicsEngineBase() {
   glfwTerminate();
   delete m_mouseHandler;
   delete m_keyboardHandler;
}

void GraphicsEngineBase::applyInitialFramebufferSize() {
   int fbWidth = 0;
   int fbHeight = 0;
   glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
   glViewport(0, 0, fbWidth, fbHeight);
   m_screen_width = static_cast<unsigned int>(fbWidth);
   m_screen_height = static_cast<unsigned int>(fbHeight);
   for (auto& callback : m_framebufferCallbacks) {
      callback(fbWidth, fbHeight);
   }
}

void GraphicsEngineBase::setWindowPos(int xPos, int yPos) {
   // Wayland does not let clients position windows; ignore the request.
   if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
      return;
   }
   glfwSetWindowPos(m_window, xPos, yPos);
}

void GraphicsEngineBase::setSwapInterval(int swapInterval) {
   m_swapInterval = swapInterval;
   glfwSwapInterval(swapInterval);
}

void GraphicsEngineBase::updateFrameTiming() {
   std::chrono::time_point<std::chrono::high_resolution_clock> previousFrameStart{
      m_frameStartTime };
   m_frameStartTime = m_timeHandler->now();

   // The first frame has no predecessor to measure against; the mode's rate is
   // the only estimate available until the second frame supplies a real one.
   if (m_frameNum == 0) {
      m_measuredFrameRate = static_cast<double>(m_monitorRefreshRate);
      return;
   }
   double frameDelta{ std::clamp(
      std::chrono::duration<double>(m_frameStartTime - previousFrameStart).count(),
      k_minFrameDelta, k_maxFrameDelta) };
   m_measuredFrameRate = 1.0 / frameDelta;
}

void GraphicsEngineBase::clearScreen() {
   glClearColor(0.6f, 0.7f, 0.8f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GraphicsEngineBase::updateInput() {
   // Must run glfwPollEvents after clearScreen() so the vsync throttle is behind us;
   // polling later in the frame samples the cursor at a drifting point.
   glfwPollEvents();
   m_mouseHandler->update();
   m_keyboardHandler->update();
}

void GraphicsEngineBase::calculateCameraVelocity() {
   if (m_frameNum == 0) {
      m_camVel = { 0,0,0 };
   } else {
      m_camVel = m_camPos - m_camPosPrev;
   }
   m_camPosPrev = m_camPos;
}

glm::dmat4 GraphicsEngineBase::getViewMatrix() const {
   glm::dmat4 viewMatrix{ 1 };
   double ss{ glm::sqrt(2.) / 2. };
   viewMatrix = glm::toMat4(glm::dquat{ ss, -ss, 0., 0. } * glm::conjugate(m_camOri));
   return viewMatrix;
}

glm::dmat4 GraphicsEngineBase::getProjectionMatrix() const {
   double aspectRatio = getAspectRatio();

   // Set m_fieldOfView as horizontal field of view.
   double fieldOfViewVertical = 2.0 * atan(tan(m_fieldOfView / 2.0) / aspectRatio);
   return glm::perspective(fieldOfViewVertical, aspectRatio, 0.1, 10000.);
}

double GraphicsEngineBase::getAspectRatio() const {
   if (m_screen_width == 0 || m_screen_height == 0) {
      return 1.;
   }
   return (double)m_screen_width / (double)m_screen_height;
}

double GraphicsEngineBase::getPaniniFitScale() const {
   double tanHalfFov = tan(m_fieldOfView / 2.0);
   glm::dvec2 tanEdge{ tanHalfFov, tanHalfFov / getAspectRatio() };
   return PaniniProjection::fitScale(m_paniniHorizontal, m_paniniVertical, tanEdge);
}

void GraphicsEngineBase::checkGLErrors() {
   GLenum error = glGetError();
   while (error != GL_NO_ERROR) {
      std::cerr << "OpenGL error: " << error << std::endl;
      error = glGetError();
   }
}

void GraphicsEngineBase::swapBuffers() {
   glfwSwapBuffers(m_window);
}

void GraphicsEngineBase::toggleFullscreen() {
   if (glfwGetWindowMonitor(m_window)) {
      // Currently fullscreen: restore the windowed position and size.
      glfwSetWindowMonitor(m_window, nullptr, m_windowedPosX, m_windowedPosY,
         m_windowedWidth, m_windowedHeight, 0);
   } else {
      m_windowedPosX = 0;
      m_windowedPosY = 0;
      if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) { // Wayland has no window positions.
         glfwGetWindowPos(m_window, &m_windowedPosX, &m_windowedPosY);
      }
      glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);
      GLFWmonitor* monitor{ getCurrentMonitor() };
      const GLFWvidmode* mode{ glfwGetVideoMode(monitor) };
      if (!mode) {
         std::cout << "Warning: Failed to get video mode for fullscreen toggle." << std::endl;
         return;
      }
      glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
   }
   // The swap interval is not preserved across monitor changes on all platforms.
   setSwapInterval(m_swapInterval);
}

void GraphicsEngineBase::setTriangleRenderMode(bool useTriangles) {
   m_renderTriangleMode = useTriangles;
   if (m_renderTriangleMode) {
      glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
   } else {
      glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
   }
}

bool GraphicsEngineBase::getTriangleRenderMode() {
   return m_renderTriangleMode;
}

int GraphicsEngineBase::queryMonitorRefreshRate() {
   if (!m_window) {
      return 0; // Return 0 if there is no window.
   }

   const GLFWvidmode* mode = glfwGetVideoMode(getCurrentMonitor());
   return mode ? mode->refreshRate : 0;
}

GLFWmonitor* GraphicsEngineBase::getCurrentMonitor() {
   // When fullscreen, the window's monitor is authoritative.
   if (GLFWmonitor* fullscreenMonitor{ glfwGetWindowMonitor(m_window) }) {
      return fullscreenMonitor;
   }

   // Wayland does not expose window positions, so the center-based pick
   // below is impossible; fall back to the primary monitor.
   if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
      return glfwGetPrimaryMonitor();
   }

   int windowX, windowY, windowWidth, windowHeight;
   glfwGetWindowPos(m_window, &windowX, &windowY);
   glfwGetWindowSize(m_window, &windowWidth, &windowHeight);

   int monitorCount;
   GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
   GLFWmonitor* windowMonitor = glfwGetPrimaryMonitor(); // Default to primary monitor

   int windowCenterX = windowX + windowWidth / 2;
   int windowCenterY = windowY + windowHeight / 2;

   for (int i = 0; i < monitorCount; ++i) {
      int monitorX, monitorY, monitorWidth, monitorHeight;
      glfwGetMonitorWorkarea(monitors[i], &monitorX, &monitorY, &monitorWidth, &monitorHeight);

      if (windowCenterX >= monitorX && windowCenterX < (monitorX + monitorWidth) &&
         windowCenterY >= monitorY && windowCenterY < (monitorY + monitorHeight)) {
         windowMonitor = monitors[i];
         break;
      }
   }

   return windowMonitor;
}

