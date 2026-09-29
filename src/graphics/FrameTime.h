// FrameTime.h
#pragma once

#include <cstdint>

// The window the shaders' physics clock wraps over, in ticks. u_time counts whole
// physics ticks and is only ever read modulo this, which keeps float sub-tick
// resolution however long a session runs. A tick handed to a shader to compare
// with u_time is taken modulo this too, so a float carries it exactly.
//
// Mirrored by k_timeWrapTicks in shared_shaders/frame_time.glsl.
inline constexpr uint32_t k_timeWrapTicks{1u << 20};
