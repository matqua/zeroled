/*
 * SPDX-License-Identifier: MIT
 *
 * Key classification shared by screens. Following the CardputerZero key guidelines, the F/Z/X/C
 * letters act as Up/Left/Down/Right outside text entry, so arrows work without holding fn.
 */

#pragma once

#include "lvgl.h"

#include <cstdint>

namespace view {

enum class NavKey {
    None,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Space,
    Backspace,
};

inline NavKey classify_key(uint32_t key) {
    switch (key) {
        case LV_KEY_UP:
        case 'f':
        case 'F':
            return NavKey::Up;
        case LV_KEY_DOWN:
        case 'x':
        case 'X':
            return NavKey::Down;
        case LV_KEY_LEFT:
        case 'z':
        case 'Z':
            return NavKey::Left;
        case LV_KEY_RIGHT:
        case 'c':
        case 'C':
            return NavKey::Right;
        case LV_KEY_ENTER: // == '\n'
        case '\r':
            return NavKey::Enter;
        case ' ':
            return NavKey::Space;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            return NavKey::Backspace;
        default:
            return NavKey::None;
    }
}

// Keys that auto-repeat while held.
inline bool is_repeatable(NavKey key) {
    return key == NavKey::Up || key == NavKey::Down || key == NavKey::Left ||
           key == NavKey::Right || key == NavKey::Backspace;
}

} // namespace view
