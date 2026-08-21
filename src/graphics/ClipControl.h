#pragma once

#include <glad/glad.h>

// glClipControl chooses the depth range clip space maps onto. It is core since
// GL 4.5, but the vendored glad loader stops at 4.4, so the entry point is
// resolved here from the same loader glad was handed.
namespace clipControl {

// Signature glad's loader already uses, so the caller passes on what it has.
using LoadProc = void* (*)(const char*);

// Values from the GL registry, absent from the glad headers along with the call.
constexpr GLenum k_negativeOneToOne{0x935E};
constexpr GLenum k_zeroToOne{0x935F};

// Resolves the entry point against a current context. Throws when the driver
// does not supply it, which a 4.6 core context is required to.
void load(LoadProc getProcAddress);

// Maps clip space onto k_negativeOneToOne (the GL default) or k_zeroToOne.
void setDepthMode(GLenum depthMode);

}  // namespace clipControl
