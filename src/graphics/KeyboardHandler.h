#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <filesystem>
#include <fstream>

class KeyboardHandler {
public:
   class Button {
      friend class KeyboardHandler;
   protected:
      bool m_isDownPrevious{ false };
      int m_keyCode{ -1 };

      bool m_isDown{ false };
      uint64_t m_timeDown{ 0 };
      uint64_t m_timeUp{ 0 };
      KeyboardHandler* m_keyboardHandler{ nullptr };
   public:
      Button(int keyCode, KeyboardHandler* keyboardHandler) : m_keyboardHandler(keyboardHandler) {
         m_keyCode = keyCode;
      }
      bool justPressed() {
         if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::RECORD) {
            m_keyboardHandler->record(m_isDown);
            m_keyboardHandler->record(m_timeDown);
         } else if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::PLAY) {
            m_isDown = m_keyboardHandler->playback<bool>();
            m_timeDown = m_keyboardHandler->playback<uint64_t>();
         }
         return m_isDown && m_timeDown == 0;
      }
      bool justReleased() {
         if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::RECORD) {
            m_keyboardHandler->record(m_isDown);
            m_keyboardHandler->record(m_timeUp);
         } else if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::PLAY) {
            m_isDown = m_keyboardHandler->playback<bool>();
            m_timeUp = m_keyboardHandler->playback<uint64_t>();
         }
         return !m_isDown && m_timeUp == 0;
      }
      bool isDown() {
         if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::RECORD) {
            m_keyboardHandler->record(m_isDown);
         } else if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::PLAY) {
            m_isDown = m_keyboardHandler->playback<bool>();
         }
         return m_isDown;
      }
      uint64_t timeDown() {
         if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::RECORD) {
            m_keyboardHandler->record(m_timeDown);
         } else if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::PLAY) {
            m_timeDown = m_keyboardHandler->playback<uint64_t>();
         }
         return m_timeDown;
      }
      uint64_t timeUp() {
         if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::RECORD) {
            m_keyboardHandler->record(m_timeUp);
         } else if (m_keyboardHandler->m_mode == KeyboardHandler::Mode::PLAY) {
            m_timeUp = m_keyboardHandler->playback<uint64_t>();
         }
         return m_timeUp;
      }
   };
   enum class Mode { NONE, RECORD, PLAY };
   KeyboardHandler(GLFWwindow* window, Mode mode = Mode::NONE, const std::filesystem::path& filepath = "keyboard_data.bin");
   ~KeyboardHandler();

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

   GLFWwindow* m_window{ nullptr };
   bool m_doRecord{ false };
   std::vector<Button*> m_buttons{};

   Button m_q{ GLFW_KEY_Q, this };
   Button m_w{ GLFW_KEY_W, this };
   Button m_e{ GLFW_KEY_E, this };
   Button m_r{ GLFW_KEY_R, this };
   Button m_t{ GLFW_KEY_T, this };
   Button m_y{ GLFW_KEY_Y, this };
   Button m_u{ GLFW_KEY_U, this };
   Button m_i{ GLFW_KEY_I, this };
   Button m_o{ GLFW_KEY_O, this };
   Button m_p{ GLFW_KEY_P, this };
   Button m_a{ GLFW_KEY_A, this };
   Button m_s{ GLFW_KEY_S, this };
   Button m_d{ GLFW_KEY_D, this };
   Button m_f{ GLFW_KEY_F, this };
   Button m_g{ GLFW_KEY_G, this };
   Button m_h{ GLFW_KEY_H, this };
   Button m_j{ GLFW_KEY_J, this };
   Button m_k{ GLFW_KEY_K, this };
   Button m_l{ GLFW_KEY_L, this };
   Button m_z{ GLFW_KEY_Z, this };
   Button m_x{ GLFW_KEY_X, this };
   Button m_c{ GLFW_KEY_C, this };
   Button m_v{ GLFW_KEY_V, this };
   Button m_b{ GLFW_KEY_B, this };
   Button m_n{ GLFW_KEY_N, this };
   Button m_m{ GLFW_KEY_M, this };

   Button m_lShift{ GLFW_KEY_LEFT_SHIFT, this };
   Button m_rShift{ GLFW_KEY_RIGHT_SHIFT, this };
   Button m_lCtrl{ GLFW_KEY_LEFT_CONTROL, this };
   Button m_rCtrl{ GLFW_KEY_RIGHT_CONTROL, this };
   Button m_space{ GLFW_KEY_SPACE, this };

   Button m_right{ GLFW_KEY_RIGHT, this };
   Button m_left{ GLFW_KEY_LEFT, this };
   Button m_up{ GLFW_KEY_UP, this };
   Button m_down{ GLFW_KEY_DOWN, this };

   Button m_esc{ GLFW_KEY_ESCAPE, this };
   Button m_capsLock{ GLFW_KEY_CAPS_LOCK, this };
protected:
   Mode m_mode{};
   std::fstream m_file{};

};

