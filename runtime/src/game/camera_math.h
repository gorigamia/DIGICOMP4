#pragma once
#include <cmath>
#include <algorithm>
namespace dw4 {
inline float horizontal_extent(float original, bool widescreen, float aspect) {
    if (!widescreen || !std::isfinite(aspect) || aspect < 1.0f) return original;
    return original * (aspect / (4.0f / 3.0f));
}
inline float far_plane(float near_z, float far_z, int percent) {
    if (!std::isfinite(near_z) || !std::isfinite(far_z) || near_z <= 0 || far_z <= near_z)
        return far_z;
    float scaled = far_z * (std::clamp(percent, 100, 200) / 100.0f);
    return std::isfinite(scaled) ? scaled : far_z;
}
}
