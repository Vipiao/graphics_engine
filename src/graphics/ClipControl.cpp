#include "ClipControl.h"

#include <stdexcept>

namespace {

using ClipControlProc = void(APIENTRYP)(GLenum origin, GLenum depth);
ClipControlProc s_clipControl{nullptr};

}  // namespace

namespace clipControl {

void load(LoadProc getProcAddress) {
    s_clipControl = reinterpret_cast<ClipControlProc>(getProcAddress("glClipControl"));
    if (s_clipControl == nullptr) {
        throw std::runtime_error(
            "glClipControl is missing; reverse-Z shadows need a GL 4.5 or later context");
    }
}

void setDepthMode(GLenum depthMode) {
    if (s_clipControl == nullptr) {
        throw std::runtime_error("clipControl::load must run before the depth mode is set");
    }
    // The window origin is the half nothing here wants moved, so it is pinned to
    // the GL default while the depth range changes underneath it.
    s_clipControl(GL_LOWER_LEFT, depthMode);
}

}  // namespace clipControl
