// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstring>

namespace HarleyGraphics {
constexpr int scales[] = {50, 75, 100};
constexpr const char* presets[] = {"Performance", "Balanced", "Quality"};

inline int PresetIndex(const char* name) {
    for (int i = 0; i < 3; ++i) if (std::strcmp(name, presets[i]) == 0) return i;
    return -1;
}

struct Selection {
    int index;
    bool armed = false;
    // Refocus and controller reconnection both require releasing all controls.
    int Update(bool active, float x, float y, bool accept, bool cancel) {
        if (!active || !std::isfinite(x) || !std::isfinite(y)) { armed = false; return -1; }
        if (!accept && !cancel && std::fabs(x) < .35f && std::fabs(y) < .35f) {
            armed = true;
            return -1;
        }
        if (!armed) return -1;
        if (cancel || accept) {
            armed = false;
            return cancel ? 0 : scales[index];
        }
        const float axis = std::fabs(x) >= std::fabs(y) ? x : y;
        if (std::fabs(axis) >= .65f) {
            if (axis > 0 && index < 2) ++index;
            if (axis < 0 && index > 0) --index;
            armed = false;
        }
        return -1;
    }
};
}
