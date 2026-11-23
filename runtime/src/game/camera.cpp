#include "camera_math.h"
#include "../platform/platform.h"
#include "ee_recomp.h"

// SLUS_208.36 perspective camera, immediately before its frustum matrix is built.
// Address identified by ElHecht's PCSX2 16:9 patch; dynamic aspect math is local.
// https://github.com/PCSX2/pcsx2_patches/blob/main/patches/SLUS-20836_B871C1D0.pnach
extern "C" void dw4_camera_frustum(EEContext* c) {
    const auto& cfg = platform::config();
    c->fpr[12].f = dw4::horizontal_extent(c->fpr[12].f, cfg.widescreen_hack,
                                         platform::view_aspect());
    c->fpr[13].u = c->fpr[12].u ^ 0x80000000u;
    c->fpr[17].f = dw4::far_plane(c->fpr[16].f, c->fpr[17].f, cfg.draw_distance);
    static unsigned count = 0;
    if (count++ < 4)
        log_write(LOG_MAIN, "DW4 camera: horizontal=%g near=%g far=%g aspect=%g",
                  c->fpr[12].f, c->fpr[16].f, c->fpr[17].f, platform::view_aspect());
}
