#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <vector>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <fstream>
#include <filesystem>

class MouseHandler {
public:
   enum class Mode { NONE, RECORD, PLAY };

   MouseHandler(GLFWwindow* window, Mode mode = Mode::NONE, const std::filesystem::path& filepath = "mouse_data.bin");
   ~MouseHandler();

   void update();
   template <typename T>
   void record(const T& data) {
      m_file.write(reinterpret_cast<const char*>(&data), sizeof(T));
      m_file.flush();
   }

   template <typename T>
   T playback() {
      T data{};
      m_file.read(reinterpret_cast<char*>(&data), sizeof(T));
      if (m_file.eof() || !m_file.good()) {
         m_file.close();
         m_mode = Mode::NONE;
      }
      return data;
   }
   void setMouseLock(bool lockMouse);
   bool getMouseLock();
   bool leftClick();
   bool rightClick();
   glm::dvec2 getMousePos();
   glm::dvec2 getMouseMovement();
   bool getLeftDown();
   bool getRightDown();
   uint64_t getTimeLeftDown();
   uint64_t getTimeLeftUp();
   uint64_t getTimeRightDown();
   uint64_t getTimeRightUp();
   double getScrollPosition();
   double getScrollMovement();

   GLFWwindow* m_window{ nullptr };

   std::vector<std::string> m_recordings{};
protected:
   static void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);
   static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
   static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

   Mode m_mode{};
   std::fstream m_file{};

   static std::unordered_map<std::uintptr_t, MouseHandler*> m_windowsToMouseHandlers;

   uint64_t m_frameNum{ 0 };
   bool m_mouseIsLocked{ false };

   glm::dvec2 m_mousePosPrev{};
   bool m_prevLeftMouseDown{ false };
   bool m_prevRightMouseDown{ false };
   double m_scrollPositionPrev{ 0. };
   // Mouse.
   glm::dvec2 m_mousePos{};
   glm::dvec2 m_mouseMovement{};
   bool m_leftMouseDown{ false };
   bool m_rightMouseDown{ false };
   uint64_t m_timeLeftDown{ 0 };
   uint64_t m_timeLeftUp{ 0 };
   uint64_t m_timeRightDown{ 0 };
   uint64_t m_timeRightUp{ 0 };
   double m_scrollPosition{ 0. };
   double m_scrollMovement{};
};
