// InstanceFrameUniforms.h
#pragma once

#include "FrameRenderParams.h"

// Sets the per-frame uniforms shared by every instanced, camera-relative draw:
// view, projection, u_frame, u_time, u_timeRemainder, and the Dekker-split
// camera position (u_cameraPositionHigh / u_cameraPositionLow). Uniforms the
// program does not declare are skipped, so any such shader can share this
// regardless of which subset it uses.
void setInstanceFrameUniforms(unsigned int programID, const FrameRenderParams& params);
