// frame_time.glsl
//
// The physics clock every stage that animates reads, as setInstanceFrameUniforms
// sets it. Counted in the application's fixed simulation ticks rather than in
// frames or seconds; the physics runs at its own fixed rate, independent of the
// display's.

uniform uint u_time;              // physics tick count, not frames
uniform float u_timeRemainder;    // how far into the next physics tick this frame is

// Both clocks below wrap every k_timeWrapTicks, which keeps float sub-tick
// resolution however long a session runs; animation periodic over that window
// hides the wrap. Mirrors k_timeWrapTicks in FrameTime.h.
const uint k_timeWrapTicks = 1048576u;   // 2^20 ticks

// Whole ticks only, so it steps at the physics rate and reads the same on every
// peer of a session
float wrappedPhysicsTicks() {
   return float(u_time & (k_timeWrapTicks - 1u));
}

// The same plus the fraction of the tick this frame sits at, so it advances
// smoothly on any display
float wrappedPhysicsTime() {
   return wrappedPhysicsTicks() + u_timeRemainder;
}
