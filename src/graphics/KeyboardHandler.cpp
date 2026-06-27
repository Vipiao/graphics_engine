





#include "KeyboardHandler.h"
#include <iostream>

KeyboardHandler::KeyboardHandler(GLFWwindow* window, Mode mode, const std::filesystem::path& filepath)
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
   m_buttons.push_back(&m_q);
   m_buttons.push_back(&m_w);
   m_buttons.push_back(&m_e);
   m_buttons.push_back(&m_r);
   m_buttons.push_back(&m_t);
   m_buttons.push_back(&m_y);
   m_buttons.push_back(&m_u);
   m_buttons.push_back(&m_i);
   m_buttons.push_back(&m_o);
   m_buttons.push_back(&m_p);
   m_buttons.push_back(&m_a);
   m_buttons.push_back(&m_s);
   m_buttons.push_back(&m_d);
   m_buttons.push_back(&m_f);
   m_buttons.push_back(&m_g);
   m_buttons.push_back(&m_h);
   m_buttons.push_back(&m_j);
   m_buttons.push_back(&m_k);
   m_buttons.push_back(&m_l);
   m_buttons.push_back(&m_z);
   m_buttons.push_back(&m_x);
   m_buttons.push_back(&m_c);
   m_buttons.push_back(&m_v);
   m_buttons.push_back(&m_b);
   m_buttons.push_back(&m_n);
   m_buttons.push_back(&m_m);

   m_buttons.push_back(&m_lShift);
   m_buttons.push_back(&m_rShift);
   m_buttons.push_back(&m_lCtrl);
   m_buttons.push_back(&m_rCtrl);
   m_buttons.push_back(&m_space);

   m_buttons.push_back(&m_right);
   m_buttons.push_back(&m_left);
   m_buttons.push_back(&m_up);
   m_buttons.push_back(&m_down);

   m_buttons.push_back(&m_esc);
   m_buttons.push_back(&m_capsLock);

   m_buttons.push_back(&m_insert);
   m_buttons.push_back(&m_delete);
   m_buttons.push_back(&m_home);
   m_buttons.push_back(&m_end);
   m_buttons.push_back(&m_pageUp);
   m_buttons.push_back(&m_pageDown);
}

KeyboardHandler::~KeyboardHandler() {
   if (m_mode != Mode::NONE) {
      m_file.close();
   }
}

void KeyboardHandler::update() {
   for (size_t ii = 0; ii < m_buttons.size(); ii++) {
      Button* bb{ m_buttons[ii] };
      bb->m_isDown = glfwGetKey(m_window, bb->m_keyCode) == GLFW_PRESS;
      if (bb->m_isDownPrevious) {
         if (bb->m_isDown) {
            bb->m_timeDown++;
         } else {
            bb->m_timeUp = 0;
         }
      } else {
         if (bb->m_isDown) {
            bb->m_timeDown = 0;
         } else {
            bb->m_timeUp++;
         }
      }
      bb->m_isDownPrevious = bb->m_isDown;
   }
}
