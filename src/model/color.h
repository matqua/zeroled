/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "wled_types.h"

namespace model {

struct Hsv {
    float h{0.0F};   // degrees 0..360
    float s{0.0F};   // 0..1
    float v{0.0F};   // 0..1
};

Hsv rgb_to_hsv(WledColor color);
WledColor hsv_to_rgb(Hsv hsv);

// Wraps a hue into [0, 360).
float wrap_hue(float degrees);

} // namespace model
