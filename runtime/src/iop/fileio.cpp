// PS2 FILEIO RPC service, sharing the IOMAN descriptors with recompiled IRX modules.
#include "iop.h"
#include "iop_recomp.h"
#include "../memory.h"
#include "../hw/sif.h"
#include <algorithm>
extern "C" {
void iop_imp_ioman_4(IOPContext*);
void iop_imp_ioman_5(IOPContext*);
void iop_imp_ioman_6(IOPContext*);
void iop_imp_ioman_8(IOPContext*);
}
namespace iop::modules {
void register_fileio() {
    const u32 scratch = iop::alloc(16384, 64);
    register_native_server(0x80000001, 4096, [scratch, reply_address = u32(0)](u32 fno, u32 buf, u32 size) mutable -> u32 {
        IOPContext c{};
        u32 result = (u32)-1;
        auto word = [buf](u32 off) { return mem::iop_rd32(buf + off); };
        // SLUS_208.36 uses FILEIO 2800: RPC acknowledges acceptance; a separate
        // SIF command delivers the result and signals the caller's semaphore.
        if (fno == 255 && size >= 8) {
            reply_address = word(0);
            memcpy(mem::iop_ptr(buf), "2800....", 8);
            return buf;
        }
        if (size < 12 || !reply_address) { mem::iop_wr32(buf, 0); return buf; }
        u32 reply[8] = {word(0), fno, word(4), word(8), (u32)-1, 0, 0, 0};
        if (fno == 0 && size >= 21) {
            c.r[4] = buf + 20; c.r[5] = word(12);
            iop_imp_ioman_4(&c); result = c.r[2];
        } else if (fno == 1 && size >= 16) {
            c.r[4] = word(12); iop_imp_ioman_5(&c); result = c.r[2];
        } else if (fno == 4 && size >= 24) {
            c.r[4] = word(12); c.r[5] = word(16); c.r[6] = word(20);
            iop_imp_ioman_8(&c); result = c.r[2];
        } else if (fno == 2 && size >= 24 && (s32)word(20) >= 0) {
            const u32 fd = word(12), dst = word(16), length = word(20);
            result = 0;
            while (result < length) {
                u32 amount = std::min<u32>(length - result, 16384);
                c.r[4] = fd; c.r[5] = scratch; c.r[6] = amount;
                iop_imp_ioman_6(&c);
                if ((s32)c.r[2] < 0) { if (!result) result = c.r[2]; break; }
                if (!c.r[2]) break;
                sif::ee_write((dst + result) & 0x1fffffff, mem::iop_ptr(scratch), c.r[2]);
                result += c.r[2];
                if (c.r[2] < amount) break;
            }
        } else if (fno == 3 && size >= 24 && word(12) <= 2) {
            // Guest stdout/stderr; no host filesystem writes.
            result = word(20);
        } else {
            log_write(LOG_IOP, "FILEIO unsupported fno=%u size=%u", fno, size);
        }
        reply[4] = result;
        sif::ee_write(reply_address & 0x1fffffff, reply, sizeof(reply));
        sif::send_cmd_to_ee(0x80000011, nullptr, 0);
        mem::iop_wr32(buf, 1);
        return buf;
    });
}
}
