// Built-in HLE IOP modules: loadfile, iopheap, and module loading dispatch.
#include "modules.h"
#include "iop.h"
#include "../memory.h"
#include "cdvd.h"
#include "pad.h"
#include "mc.h"
#include "gs_iop.h"
#include "game_modules.h"
#include <algorithm>

namespace iop::modules {
void register_fileio();

void hexdump(int cat, const char* title, const u8* p, u32 n) {
    if (!g_log_enabled[cat]) return;
    log_write(cat, "%s (%u bytes)", title, n);
    char line[128];
    for (u32 i = 0; i < n && i < 256; i += 16) {
        int k = snprintf(line, sizeof(line), "  %04x:", i);
        for (u32 j = 0; j < 16 && i + j < n; j++) k += snprintf(line + k, sizeof(line) - k, " %02x", p[i + j]);
        log_write(cat, "%s", line);
    }
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

void load(const std::string& path, const std::string& args) {
    std::string p = upper(path);
    log_write(LOG_IOP, "LoadModule '%s' args='%s'", path.c_str(), args.c_str());
    if (p.find("GS_IOP") != std::string::npos) gs_iop::start(args);
    else if (p.find("PADMAN") != std::string::npos) pad::start();
    else if (p.find("MCSERV") != std::string::npos) mc::start();
    else if (p.find("SIO2MAN") != std::string::npos || p.find("MCMAN") != std::string::npos ||
             p.find("LIBSD") != std::string::npos) {
        // These system modules are handled by native IOP services.
    } else if (!start_game_module(path, args)) {
        log_write(LOG_IOP, "No generated IOP module matches '%s'", path.c_str());
    }
}

void register_builtin() {
    register_fileio();
    // loadfile
    register_native_server(0x80000006, 0x200, [](u32 fno, u32 buf, u32 size) -> u32 {
        (void)size;
        u8* b = mem::iop_ptr(buf);
        if (fno == 0 || fno == 1) {  // MOD_LOAD / ELF_LOAD
            std::string path((char*)b + 8, strnlen((char*)b + 8, 252));
            u32 arglen;
            memcpy(&arglen, b, 4);
            std::string args((char*)b + 8 + 252, std::min<u32>(arglen, 252));
            load(path, args);
            u32 res[2] = {1, 0};  // module id, module result
            memcpy(b, res, 8);
            return buf;
        }
        if (fno == 255) {  // version query: libkernel compares against its own version string
            // SLUS_208.36 uses the IOPRP280 image and SDK version 2800.
            memcpy(b, "2800", 4);
            return buf;
        }
        log_write(LOG_IOP, "loadfile: unhandled fno %u", fno);
        hexdump(LOG_IOP, "loadfile args", b, 64);
        memset(b, 0, 8);
        return buf;
    });
    // iopheap
    register_native_server(0x80000003, 0x40, [](u32 fno, u32 buf, u32 size) -> u32 {
        (void)size;
        u32* b = (u32*)mem::iop_ptr(buf);
        if (fno == 1) {
            u32 a = alloc(b[0]);
            LOG(LOG_IOP, "iopheap alloc(%x) -> %08x", b[0], a);
            b[0] = a;
        } else if (fno == 2) {
            free(b[0]);
            b[0] = 0;
        } else {
            log_write(LOG_IOP, "iopheap: unhandled fno %u", fno);
            b[0] = 0;
        }
        return buf;
    });
    cdvd::register_servers();
    pad::register_servers();
    mc::register_servers();
}

}  // namespace iop::modules
