#include "MouseHandler.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <numeric>

std::unordered_map<std::uintptr_t, MouseHandler*> MouseHandler::m_windowsToMouseHandlers{};

void MouseHandler::cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
   size_t key{ reinterpret_cast<std::uintptr_t>(window) };
   if (MouseHandler::m_windowsToMouseHandlers.find(key) !=
      MouseHandler::m_windowsToMouseHandlers.end()) {
      MouseHandler* mouseHandler{ MouseHandler::m_windowsToMouseHandlers[key] };
      mouseHandler->m_mousePos.x = xpos;
      mouseHandler->m_mousePos.y = ypos;
      //std::cout << "xpos: " << xpos << std::endl << "ypos: " << ypos << std::endl << std::endl;
   }
}

void MouseHandler::mouseButtonCallback(GLFWwindow* window, int button, int action, int /*mods*/) {
   size_t key{ reinterpret_cast<std::uintptr_t>(window) };
   if (MouseHandler::m_windowsToMouseHandlers.find(key) !=
      MouseHandler::m_windowsToMouseHandlers.end()) {
      MouseHandler* mouseHandler{ MouseHandler::m_windowsToMouseHandlers[key] };
      if (button == GLFW_MOUSE_BUTTON_LEFT) {
         mouseHandler->m_leftMouseDown = action == GLFW_PRESS;
      } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
         mouseHandler->m_rightMouseDown = action == GLFW_PRESS;
      }
   }
}

void MouseHandler::scrollCallback(GLFWwindow* window, double /*xoffset*/, double yoffset) {
   size_t key{ reinterpret_cast<std::uintptr_t>(window) };
   if (MouseHandler::m_windowsToMouseHandlers.find(key) !=
      MouseHandler::m_windowsToMouseHandlers.end()) {
      MouseHandler* mouseHandler{ MouseHandler::m_windowsToMouseHandlers[key] };
      mouseHandler->m_scrollPosition += yoffset;
   }
}

MouseHandler::MouseHandler(GLFWwindow* window, Mode mode, const std::filesystem::path& filepath)
   : m_mode(mode) {
   if (mode != Mode::NONE) {
      // Ensure the directory exists.
      if (!std::filesystem::exists(filepath.parent_path())) {
         std::filesystem::create_directories(filepath.parent_path());
      }

      // In RECORD mode, delete the file if it exists.
      if (mode == Mode::RECORD && std::filesystem::exists(filepath)) {
         std::filesystem::remove(filepath);
      }

      // Open the file in the appropriate mode.
      m_file.open(filepath, (mode == Mode::RECORD ? std::ios::out : std::ios::in) | std::ios::binary);
      if (!m_file.is_open()) {
         throw std::runtime_error("Failed to open file: " + filepath.string());
      }
   }
   //
   m_window = window;
   size_t key{ reinterpret_cast<std::uintptr_t>(window) };
   if (MouseHandler::m_windowsToMouseHandlers.find(key) !=
      MouseHandler::m_windowsToMouseHandlers.end()) {
      std::cout << "Error: Cannot assign window to two mouse handlers." << std::endl;
      throw "Error: Cannot assign window to two mouse handlers.";
   }
   MouseHandler::m_windowsToMouseHandlers[key] = this;
   //
   glfwSetCursorPosCallback(m_window, cursorPositionCallback);
   glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
   glfwSetScrollCallback(m_window, scrollCallback);
   glfwSetCursorPos(m_window, 0, 0); // Prevents jumping of mouse when first entering the window.

   //
}

MouseHandler::~MouseHandler() {
   size_t key{ reinterpret_cast<std::uintptr_t>(m_window) };
   MouseHandler::m_windowsToMouseHandlers.erase(key);
   if (m_mode != Mode::NONE) {
      m_file.close();
   }
}

void MouseHandler::update() {

   // Movement.
   if (m_frameNum == 0) {

   } else {
      m_mouseMovement = m_mousePos - m_mousePosPrev;
      m_scrollMovement = m_scrollPosition - m_scrollPositionPrev;
   }
   m_mousePosPrev = m_mousePos;
   m_scrollPositionPrev = m_scrollPosition;

   // Mouse click.
   if (m_prevLeftMouseDown) {
      if (m_leftMouseDown) {
         m_timeLeftDown++;
      } else {
         m_timeLeftUp = 0;
      }
   } else {
      if (m_leftMouseDown) {
         m_timeLeftDown = 0;
      } else {
         m_timeLeftUp++;
      }
   }
   m_prevLeftMouseDown = m_leftMouseDown;

   if (m_prevRightMouseDown) {
      if (m_rightMouseDown) {
         m_timeRightDown++;
      } else {
         m_timeRightUp = 0;
      }
   } else {
      if (m_rightMouseDown) {
         m_timeRightDown = 0;
      } else {
         m_timeRightUp++;
      }
   }
   m_prevRightMouseDown = m_rightMouseDown;

   m_frameNum++;
}

void MouseHandler::setMouseLock(bool lockMouse) {
   if (lockMouse) {
      glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
   } else {
      glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
   }
   m_mouseIsLocked = lockMouse;
}

bool MouseHandler::getMouseLock() {
   if (m_mode == Mode::RECORD) {
      record(m_mouseIsLocked);
   } else if (m_mode == Mode::PLAY) {
      return playback<bool>();
   }
   return m_mouseIsLocked;
}

bool MouseHandler::leftClick() {
   if (m_mode == Mode::RECORD) {
      record(m_leftMouseDown);
      record(m_timeLeftDown);
      record(m_timeLeftUp);
   } else if (m_mode == Mode::PLAY) {
      m_leftMouseDown = playback<bool>();
      m_timeLeftDown = playback<uint64_t>();
      m_timeLeftUp = playback<uint64_t>();
   }
   return m_leftMouseDown && m_timeLeftDown == 0;
}

bool MouseHandler::rightClick() {
   if (m_mode == Mode::RECORD) {
      record(m_rightMouseDown);
      record(m_timeRightDown);
      record(m_timeRightUp);
   } else if (m_mode == Mode::PLAY) {
      m_rightMouseDown = playback<bool>();
      m_timeRightDown = playback<uint64_t>();
      m_timeRightUp = playback<uint64_t>();
   }
   return m_rightMouseDown && m_timeRightDown == 0;
}

glm::dvec2 MouseHandler::getMousePos() {
   if (m_mode == Mode::RECORD) {
      record(m_mousePos);
   } else if (m_mode == Mode::PLAY) {
      m_mousePos = playback<glm::dvec2>();
   }
   return m_mousePos;
}

glm::dvec2 MouseHandler::getMouseMovement() {
   if (m_mode == Mode::RECORD) {
      record(m_mouseMovement);
   } else if (m_mode == Mode::PLAY) {
      m_mouseMovement = playback<glm::dvec2>();
   }
   return m_mouseMovement;
}

bool MouseHandler::getLeftDown() {
   if (m_mode == Mode::RECORD) {
      record(m_leftMouseDown);
   } else if (m_mode == Mode::PLAY) {
      m_leftMouseDown = playback<bool>();
   }
   return m_leftMouseDown;
}

bool MouseHandler::getRightDown() {
   if (m_mode == Mode::RECORD) {
      record(m_rightMouseDown);
   } else if (m_mode == Mode::PLAY) {
      m_rightMouseDown = playback<bool>();
   }
   return m_rightMouseDown;
}

uint64_t MouseHandler::getTimeLeftDown() {
   if (m_mode == Mode::RECORD) {
      record(m_timeLeftDown);
   } else if (m_mode == Mode::PLAY) {
      m_timeLeftDown = playback<uint64_t>();
   }
   return m_timeLeftDown;
}

uint64_t MouseHandler::getTimeLeftUp() {
   if (m_mode == Mode::RECORD) {
      record(m_timeLeftUp);
   } else if (m_mode == Mode::PLAY) {
      m_timeLeftUp = playback<uint64_t>();
   }
   return m_timeLeftUp;
}

uint64_t MouseHandler::getTimeRightDown() {
   if (m_mode == Mode::RECORD) {
      record(m_timeRightDown);
   } else if (m_mode == Mode::PLAY) {
      m_timeRightDown = playback<uint64_t>();
   }
   return m_timeRightDown;
}

uint64_t MouseHandler::getTimeRightUp() {
   if (m_mode == Mode::RECORD) {
      record(m_timeRightUp);
   } else if (m_mode == Mode::PLAY) {
      m_timeRightUp = playback<uint64_t>();
   }
   return m_timeRightUp;
}

double MouseHandler::getScrollPosition() {
   if (m_mode == Mode::RECORD) {
      record(m_scrollPosition);
   } else if (m_mode == Mode::PLAY) {
      m_scrollPosition = playback<double>();
   }
   return m_scrollPosition;
}

double MouseHandler::getScrollMovement() {
   if (m_mode == Mode::RECORD) {
      record(m_scrollMovement);
   } else if (m_mode == Mode::PLAY) {
      m_scrollMovement = playback<double>();
   }
   return m_scrollMovement;
}
