// Digimon World 4 PS2 recompilation - entry point.
#include "common.h"
#include "savestate.h"
#include <thread>
#include "memory.h"
#include "ee/kernel.h"
#include "hw/hw.h"
#include "hw/vif.h"
#include "hw/ipu.h"
#include "hw/gspipe.h"
#include "vu/vu.h"
#include "gs/gs.h"
#include "gs/gs_census.h"
#include "gs/gs_gpu.h"
#include "iop/iop.h"
#include "iop/cdvd.h"
#include "platform/platform.h"
#include "platform/audio.h"
#include "platform/subtitles.h"
#include "ee_modules.h"
#include <windows.h>
#include <map>
#include <mutex>

// default: MAIN KERNEL GS IOP CDVD MC GAME; chatty categories (SIF, PAD, SPU, IPU, ...) via --log=
bool g_log_enabled[LOG_COUNT] = {true, true, false, false, true, false, false, false, true, true, false, true, false, false, true};
static FILE* g_logfile = nullptr;
static std::mutex g_log_mutex;
static LARGE_INTEGER g_qpf, g_qpc0;

static const char* LOG_NAMES[LOG_COUNT] = {"main", "kern", "hw", "dma", "gs", "vif", "vu", "sif", "iop", "cdvd",
                                           "pad", "mc", "spu", "ipu", "game"};

double host_time_sec() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)(t.QuadPart - g_qpc0.QuadPart) / (double)g_qpf.QuadPart;
}
u64 host_time_us() { return (u64)(host_time_sec() * 1e6); }

void log_write(int cat, const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(g_log_mutex);
    double t = host_time_sec();
    if (g_logfile) {
        fprintf(g_logfile, "[%9.4f][%s] %s\n", t, LOG_NAMES[cat], buf);
        fflush(g_logfile);
    }
    if (platform::console_enabled()) {
        fprintf(stdout, "[%9.4f][%s] %s\n", t, LOG_NAMES[cat], buf);
        fflush(stdout);
    }
}

void fatal(const char* fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    log_write(LOG_MAIN, "FATAL: %s", buf);
    platform::fatal_message(buf);
    ExitProcess(1);
}

// ---------------------------------------------------------------------------
// Overlay reference images (for dispatch validation)
// ---------------------------------------------------------------------------
static std::map<std::string, std::vector<u8>> g_overlay_images;

extern "C" const u8* overlay_image_for(const char* name, u32* size) {
    auto it = g_overlay_images.find(name);
    if (it == g_overlay_images.end()) return nullptr;
    *size = (u32)it->second.size();
    return it->second.data();
}

static std::vector<u8> read_disc_file(const std::string& path) {
    u32 lsn, size;
    if (!cdvd::find_file(path, &lsn, &size)) fatal("File not found on disc: %s", path.c_str());
    std::vector<u8> d(((size + 2047) & ~2047u));
    cdvd::read_sectors(lsn, (u32)d.size() / 2048, d.data());
    d.resize(size);
    return d;
}

static std::string boot_file_from_disc() {
    u32 lsn = 0, size = 0;
    if (!cdvd::find_file("\\SYSTEM.CNF", &lsn, &size)) return {};
    std::vector<u8> data((size + 2047) & ~2047u);
    cdvd::read_sectors(lsn, (u32)data.size() / 2048, data.data());
    std::string config(data.begin(), data.begin() + size);
    size_t start = config.find("cdrom0:");
    if (start == std::string::npos) return {};
    start += 7;
    start = config.find_first_not_of(" \t", start);
    if (start == std::string::npos) return {};
    size_t end = config.find_first_of(";\r\n", start);
    std::string path = config.substr(start, end == std::string::npos ? std::string::npos : end - start);
    while (!path.empty() && (path.back() == ' ' || path.back() == '\t')) path.pop_back();
    return path;
}

static void load_overlay_images() {
    std::map<std::string, std::vector<u8>> files;
    for (const EEOverlaySource* s = g_ee_overlay_sources; s->name; s++) {
        auto it = files.find(s->file);
        if (it == files.end()) it = files.emplace(s->file, read_disc_file(s->file)).first;
        const auto& f = it->second;
        if (s->offset + s->size > f.size()) fatal("Overlay %s out of range in %s", s->name, s->file);
        g_overlay_images[s->name] = std::vector<u8>(f.begin() + s->offset, f.begin() + s->offset + s->size);
    }
    log_write(LOG_MAIN, "Loaded %zu overlay reference images", g_overlay_images.size());
}

static u32 load_elf(const std::vector<u8>& elf) {
    u32 entry, phoff;
    u16 phnum;
    memcpy(&entry, &elf[24], 4);
    memcpy(&phoff, &elf[28], 4);
    memcpy(&phnum, &elf[44], 2);
    for (int i = 0; i < phnum; i++) {
        u32 ph[8];
        memcpy(ph, &elf[phoff + i * 32], 32);
        if (ph[0] != 1) continue;
        u32 off = ph[1], vaddr = ph[2], filesz = ph[4], memsz = ph[5];
        if (memsz) memset(mem::ram + (vaddr & 0x1FFFFFF), 0, memsz);
        if (filesz) memcpy(mem::ram + (vaddr & 0x1FFFFFF), &elf[off], filesz);
        log_write(LOG_MAIN, "ELF segment %08x filesz %x memsz %x", vaddr, filesz, memsz);
    }
    return entry;
}

std::string exe_dir() {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string s(p);
    return s.substr(0, s.find_last_of("\\/"));
}

static bool file_exists_u8(const std::string& p) {  // UTF-8 path
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w.data(), n);
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool g_trace_syscalls_flag = false;
static bool g_verify_redirects_flag = false;
bool gs_log_draws_flag = false;
namespace kernel { extern bool g_dispatch_log; }
namespace gs { extern bool g_gs_null; extern int g_gs_threads; extern double g_draw_stats_at; extern double g_log_draws_from; extern bool g_log_draws; extern double g_vram_dump_at; extern std::string g_vram_dump_path; }
static bool g_watchdog_flag = false;
namespace hw { extern bool g_poll_stats; }
namespace gs { namespace gpu { extern bool g_no_uav_overlap, g_gpu_timing, g_no_batch_join, g_frame_hashes, g_gpu_profile; } }
namespace kernel { extern bool g_trace_syscalls; extern bool g_watchdog; extern bool g_native_perf; }

static DWORD WINAPI emu_thread(void*) {
    kernel::g_trace_syscalls = g_trace_syscalls_flag;
    kernel::g_watchdog = g_watchdog_flag;
    gs::g_log_draws = gs_log_draws_flag;
    hw::init();
    vif::init();
    vu::init();
    ipu::init();
    iop::init();
    load_overlay_images();
    std::string boot = boot_file_from_disc();
    if (boot.empty()) fatal("SYSTEM.CNF does not name a boot executable");
    log_write(LOG_MAIN, "Disc %d, boot file %s", platform::config().disc, boot.c_str());
    std::vector<u8> elf = read_disc_file(boot);
    u32 entry = load_elf(elf);
    kernel::init();
    log_write(LOG_MAIN, "Starting EE at %08x", entry);
    kernel::run_main(entry);
    return 0;
}

static bool g_gpu_compare_flag = false;
static int g_gs_thread_flag = -1;
namespace texthook { extern bool g_watch_msg; }
extern "C" { int g_ee_patch[8]; }

int main(int argc, char** argv) {
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_qpc0);
    std::string dir = exe_dir();
    platform::Config& cfg = platform::config();
    cfg.renderer = 0;
    cfg.gs_thread = false;
    kernel::set_clock_scale(0.5);
    platform::load_config(dir + "\\digimonworld4.ini");
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--console") cfg.console = true;
        else if (a.rfind("--log=", 0) == 0) {
            std::string cats = a.substr(6);
            for (int c = 0; c < LOG_COUNT; c++)
                if (cats.find(LOG_NAMES[c]) != std::string::npos) g_log_enabled[c] = true;
        } else if (a.rfind("--iso=", 0) == 0) cfg.iso_path = a.substr(6);
        else if (a == "--trace-syscalls") g_trace_syscalls_flag = true;
        else if (a == "--watchdog") g_watchdog_flag = true;
        else if (a == "--perf") kernel::g_native_perf = true;
        else if (a.rfind("--perf-from=", 0) == 0) { kernel::g_native_perf = true; kernel::g_native_perf_from = atof(a.c_str() + 12); }
        else if (a == "--perf=raster") { kernel::g_native_perf = true; extern int perf_target_raster(); perf_target_raster(); }
        else if (a == "--watch-msg") texthook::g_watch_msg = true;
        else if (a == "--steamdeck") platform::g_force_steam_deck = true;
        else if (a == "--verify-redirects") g_verify_redirects_flag = true;
        else if (a.rfind("--watch-mem=", 0) == 0) { extern double watch_mem_parse(const char*); watch_mem_parse(a.c_str() + 12); }
        else if (a == "--perf=gsp") { kernel::g_native_perf = true; extern int perf_target_gsp(); perf_target_gsp(); }
        else if (a.rfind("--gs-thread=", 0) == 0) g_gs_thread_flag = atoi(a.c_str() + 12);
        else if (a.rfind("--gs-threads=", 0) == 0) gs::g_gs_threads = atoi(a.c_str() + 13);
        else if (a == "--gs-null") gs::g_gs_null = true;
        else if (a.rfind("--language=", 0) == 0) cfg.language = a.substr(11);
        else if (a.rfind("--disc=", 0) == 0) cfg.disc = atoi(a.c_str() + 7) == 2 ? 2 : 1;
        else if (a == "--no-hd-textures") cfg.hd_textures = false;
        else if (a.rfind("--clock-scale=", 0) == 0) kernel::set_clock_scale(atof(a.c_str() + 14));
        else if (a == "--force60") cfg.fps60 = true;
        else if (a.rfind("--renderer=", 0) == 0) cfg.renderer = a.substr(11) == "gpu" ? 1 : 0;
        else if (a.rfind("--scale=", 0) == 0) cfg.internal_scale = atoi(a.c_str() + 8);
        else if (a == "--gpu-compare") { g_gpu_compare_flag = true; cfg.renderer = 1; cfg.internal_scale = 1; }
        else if (a == "--ipu-trace") { extern void ipu_enable_trace(); ipu_enable_trace(); }
        else if (a.rfind("--shots=", 0) == 0) platform::set_screenshot_interval(atof(a.substr(8).c_str()), dir + "/shots");
    }
    platform::init_console(cfg.console);
    g_logfile = fopen((dir + "\\digimonworld4.log").c_str(), "w");
    log_write(LOG_MAIN, "Steam Deck: %s", platform::steam_deck_info().c_str());

    // Digimon World 4: force conservative defaults and disable Grandia-specific features.
    cfg.disc = 1;
    cfg.jp_voices = false;
    cfg.widescreen_hack = cfg.hud_fix = cfg.remove_letterbox = false;
    cfg.hd_textures = cfg.sharp_text = false;
    cfg.movie_skip = cfg.cutscene_skip = cfg.subtitles = cfg.autosave_state = false;
    cfg.fps60 = cfg.fps60_battle = false;
    cfg.big_text = 0;

    std::string iso = cfg.iso_path;
    if (iso.empty() || !file_exists_u8(iso)) {
        iso.clear();
        for (const std::string& candidate : {dir + "\\Digimon World 4 (USA).iso", dir + "\\disc1.iso"})
            if (file_exists_u8(candidate)) { iso = candidate; break; }
    }
    u32 boot_lsn = 0, boot_size = 0;
    if (iso.empty() || !cdvd::open_image(iso) ||
        !cdvd::find_file("SLUS_208.36", &boot_lsn, &boot_size)) {
        platform::message(L"Coloque Digimon World 4 (USA).iso ao lado de DigimonWorld4.exe. Use a versao PS2 USA (SLUS_208.36).");
        return 1;
    }
    if (cfg.iso_path != iso) { cfg.iso_path = iso; platform::save_config(); }
    if (g_verify_redirects_flag) return cdvd::verify_redirects() ? 1 : 0;

    mem::init();
    subs::init();
    platform::apply_patches();
    kernel::g_force60 = cfg.fps60;
    g_ee_patch[1] = g_ee_patch[2] = cfg.remove_letterbox;
    g_ee_patch[3] = cfg.movie_skip;
    if (!platform::init_video()) return 1;
    log_write(LOG_MAIN, "video: initialization complete");
    gs::raster_request_gpu(cfg.renderer != 0, cfg.internal_scale, g_gpu_compare_flag);
    log_write(LOG_MAIN, "GS: initializing");
    gs::init();
    log_write(LOG_MAIN, "GS: initialized");
    gsp::start(g_gs_thread_flag >= 0 ? g_gs_thread_flag != 0 : cfg.gs_thread);
    log_write(LOG_MAIN, "GS pipeline: started");
    audio::init();
    log_write(LOG_MAIN, "audio: initialized");
    HANDLE emulation_thread = CreateThread(nullptr, 8 * 1024 * 1024, emu_thread, nullptr, 0, nullptr);
    if (!emulation_thread) fatal("CreateThread for EE failed (%lu)", GetLastError());
    CloseHandle(emulation_thread);
    log_write(LOG_MAIN, "EE: emulation thread started");
    platform::run_message_loop();
    return 0;
}
