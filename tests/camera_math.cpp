#include "../runtime/src/game/camera_math.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main() {
    assert(dw4::horizontal_extent(2, true, 4.f/3) == 2);
    assert(std::fabs(dw4::horizontal_extent(2, true, 16.f/9) - 8.f/3) < 1e-6f);
    assert(dw4::horizontal_extent(2, false, 16.f/9) == 2);
    assert(dw4::horizontal_extent(2, true, NAN) == 2);
    assert(dw4::far_plane(1, 1000, 150) == 1500);
    assert(dw4::far_plane(1, 1000, 400) == 2000);
    assert(dw4::far_plane(1, 1000, 50) == 1000);
    assert(dw4::far_plane(10, 5, 200) == 5);
    assert(dw4::far_plane(1, std::numeric_limits<float>::max(), 200)
        == std::numeric_limits<float>::max());
    puts("camera math: PASS");
}
