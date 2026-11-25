/*
 * SPDX-License-Identifier: MIT
 */

#include "color.h"

#include <algorithm>
#include <cmath>

namespace model {

float wrap_hue(float degrees) {
    float h = std::fmod(degrees, 360.0F);
    if (h < 0.0F) {
        h += 360.0F;
    }
    return h;
}

Hsv rgb_to_hsv(WledColor color) {
    const float r = color.r / 255.0F;
    const float g = color.g / 255.0F;
    const float b = color.b / 255.0F;
    const float max = std::max({r, g, b});
    const float min = std::min({r, g, b});
    const float delta = max - min;

    Hsv hsv;
    hsv.v = max;
    hsv.s = max <= 0.0F ? 0.0F : delta / max;
    if (delta <= 0.0F) {
        hsv.h = 0.0F;
    }
    else if (max == r) {
        hsv.h = 60.0F * std::fmod((g - b) / delta, 6.0F);
    }
    else if (max == g) {
        hsv.h = 60.0F * ((b - r) / delta + 2.0F);
    }
    else {
        hsv.h = 60.0F * ((r - g) / delta + 4.0F);
    }
    hsv.h = wrap_hue(hsv.h);
    return hsv;
}

WledColor hsv_to_rgb(Hsv hsv) {
    const float h = wrap_hue(hsv.h);
    const float s = std::clamp(hsv.s, 0.0F, 1.0F);
    const float v = std::clamp(hsv.v, 0.0F, 1.0F);
    const float c = v * s;
    const float x = c * (1.0F - std::fabs(std::fmod(h / 60.0F, 2.0F) - 1.0F));
    const float m = v - c;

    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    if (h < 60.0F) {
        r = c; g = x;
    }
    else if (h < 120.0F) {
        r = x; g = c;
    }
    else if (h < 180.0F) {
        g = c; b = x;
    }
    else if (h < 240.0F) {
        g = x; b = c;
    }
    else if (h < 300.0F) {
        r = x; b = c;
    }
    else {
        r = c; b = x;
    }

    const auto to_byte = [](float value) {
        return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return WledColor{to_byte(r + m), to_byte(g + m), to_byte(b + m)};
}

} // namespace model
