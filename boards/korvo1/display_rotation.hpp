#pragma once

// The rotation this firmware was built for (CONFIG_PRISM_DISPLAY_ROTATION) and the frame it gives.

#include "rotation.hpp"
#include "sdkconfig.h"

namespace korvo1 {

#if defined(CONFIG_PRISM_DISPLAY_ROTATION_90)
inline constexpr Rotation kRotation = Rotation::clockwise;
#elif defined(CONFIG_PRISM_DISPLAY_ROTATION_270)
inline constexpr Rotation kRotation = Rotation::counter_clockwise;
#else
inline constexpr Rotation kRotation = Rotation::none;
#endif

inline constexpr int kFrameWidth = frame_width(kRotation);
inline constexpr int kFrameHeight = frame_height(kRotation);

} // namespace korvo1
