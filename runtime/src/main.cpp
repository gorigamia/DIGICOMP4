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
#include <commctrl.h>
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

static bool file_exists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool file_exists_u8(const std::string& p) {  // UTF-8 path (chosen in a file dialog)
    int n = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w.data(), n);
    DWORD a = GetFileAttributesW(w.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool g_trace_syscalls_flag = false;
static bool g_verify_redirects_flag = false;  // --verify-redirects: check the files read from a second image, then quit
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
namespace texthook { extern bool g_watch_msg; }  // --gs-thread=0/1 overrides the setting
extern "C" { int g_ee_patch[8]; }

static bool g_loader_cancel = false;
static HWND g_loader_bar = nullptr;
static HWND g_loader_label = nullptr;

static LRESULT CALLBACK LoaderWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_CLOSE || (msg == WM_COMMAND && LOWORD(w) == IDCANCEL)) {
        g_loader_cancel = true;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

static bool loader_pump() {
    MSG m;
    while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) return false;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return !g_loader_cancel;
}

static void loader_step(int percent, const wchar_t* text) {
    if (g_loader_label) SetWindowTextW(g_loader_label, text);
    if (g_loader_bar) SendMessageW(g_loader_bar, PBM_SETPOS, percent, 0);
    loader_pump();
}

static bool load_game_with_progress(const std::string& iso, u32& boot_lsn, u32& boot_size) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = LoaderWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"DW4Loader";
        RegisterClassExW(&wc);
        registered = true;
    }
    g_loader_cancel = false;
    HWND w = CreateWindowExW(WS_EX_DLGMODALFRAME, L"DW4Loader", L"Digimon World 4 - Carregando",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
                                                           460, 150, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!w) return false;
    RECT wr;
    GetWindowRect(w, &wr);
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(w, nullptr, (sw - (wr.right - wr.left)) / 2, (sh - (wr.bottom - wr.top)) / 2,
                 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    g_loader_label = CreateWindowW(L"STATIC", L"Lendo arquivos do jogo...", WS_CHILD | WS_VISIBLE,
                                   20, 24, 420, 24, w, nullptr, GetModuleHandleW(nullptr), nullptr);
    g_loader_bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                   20, 62, 420, 24, w, nullptr, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(g_loader_bar, PBM_SETMARQUEE, TRUE, 20);
    ShowWindow(w, SW_SHOW);
    UpdateWindow(w);
    loader_pump();

    bool ok = false;
    if (cdvd::open_image(iso) && loader_pump()) {
        SendMessageW(g_loader_bar, PBM_SETMARQUEE, FALSE, 0);
        SendMessageW(g_loader_bar, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        loader_step(20, L"Arquivos do jogo lidos. Localizando o executavel...");
        if (cdvd::find_file("SLUS_208.36", &boot_lsn, &boot_size) && loader_pump()) {
            loader_step(60, L"Preparando o jogo...");
            loader_step(85, L"Carregando texturas e audio...");
            ok = loader_pump();
            loader_step(100, L"Pronto. Iniciando...");
        }
    }
    DestroyWindow(w);
    g_loader_bar = g_loader_label = nullptr;
    return ok && !g_loader_cancel;
}

int main(int argc, char** argv) {
    QueryPerformanceFrequency(&g_qpf);
    QueryPerformanceCounter(&g_qpc0);
    std::string dir = exe_dir();
    platform::Config& cfg = platform::config();
    kernel::set_clock_scale(1.0);
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
        else if (a.rfind("--watch-mem=", 0) == 0) {
            // SEC:[*PTR+]OFF:LEN (hex addresses)
            namespace k = kernel;
            extern double watch_mem_parse(const char*);
            watch_mem_parse(a.c_str() + 12);
        }
        else if (a == "--perf=gsp") { kernel::g_native_perf = true; extern int perf_target_gsp(); perf_target_gsp(); }
        else if (a.rfind("--gs-thread=", 0) == 0) g_gs_thread_flag = atoi(a.c_str() + 12);
        else if (a.rfind("--gs-threads=", 0) == 0) gs::g_gs_threads = atoi(a.c_str() + 13);
        else if (a == "--gs-null") gs::g_gs_null = true;
        else if (a == "--widescreen") cfg.widescreen_hack = true;
        else if (a.rfind("--aspect=", 0) == 0) cfg.aspect = atoi(a.c_str() + 9);
        else if (a.rfind("--language=", 0) == 0) cfg.language = a.substr(11);
        else if (a == "--find-mdz") { extern bool find_mdz_flag(); find_mdz_flag(); }
        else if (a.rfind("--disc=", 0) == 0) cfg.disc = atoi(a.c_str() + 7) == 2 ? 2 : 1;
        else if (a == "--no-letterbox") cfg.remove_letterbox = true;
        else if (a == "--dump-textures") cfg.texture_dump = true;
        else if (a == "--no-hd-textures") cfg.hd_textures = false;
        else if (a == "--hd-ab") gs::gpu::g_hd_ab = true;
        else if (a == "--sharpen-ab") gs::gpu::g_sharpen_ab = true;
        else if (a == "--shots-final") platform::g_shots_final = true;
        else if (a.rfind("--hud-log=", 0) == 0) gs::gpu::g_hud_log_at = atof(a.c_str() + 10);
        else if (a.rfind("--step-mode=", 0) == 0) kernel::g_test_step_mode = atoi(a.c_str() + 12);
        else if (a.rfind("--time-scale=", 0) == 0) kernel::g_test_time_scale = (float)atof(a.c_str() + 13);
        else if (a.rfind("--fr-trace=", 0) == 0) { extern int g_fr_trace; g_fr_trace = atoi(a.c_str() + 11); }
        else if (a.rfind("--sharpen=", 0) == 0) cfg.sharpen = atoi(a.c_str() + 10);
        else if (a.rfind("--hd-lod-bias=", 0) == 0) cfg.hd_lod_bias = (float)atof(a.c_str() + 14);
        else if (a.rfind("--window-shot=", 0) == 0) platform::g_overlay_shot_at = atof(a.c_str() + 14);  // the window picture with overlays
        else if (a.rfind("--window-test=", 0) == 0) platform::g_window_test_at = atof(a.c_str() + 14);
        else if (a.rfind("--settings-test=", 0) == 0) platform::g_settings_test_at = atof(a.c_str() + 16);
        else if (a.rfind("--overlay-test=", 0) == 0) {
            platform::g_overlay_test_at = atof(a.c_str() + 15);
            if (const char* k = strchr(a.c_str() + 15, ',')) platform::g_overlay_test_downs = atoi(k + 1);
        }
        else if (a.rfind("--watch-text=", 0) == 0) kernel::g_watch_text_at = atof(a.c_str() + 13);
        else if (a == "--log-fps") platform::g_log_fps = true;
        else if (a.rfind("--turbo-from=", 0) == 0) platform::g_turbo_from = atof(a.c_str() + 13);
        else if (a.rfind("--frame-delay=", 0) == 0) gs::g_test_frame_delay_ms = atoi(a.c_str() + 14);
        else if (a == "--scene-skip-flag") { extern bool scene_skip_flag_test(); scene_skip_flag_test(); }
        else if (a == "--dispatch-log") kernel::g_dispatch_log = true;
        else if (a == "--uav-overlap") gs::gpu::g_no_uav_overlap = false;
        else if (a == "--no-batch-join") gs::gpu::g_no_batch_join = true;
        else if (a == "--frame-hashes") gs::gpu::g_frame_hashes = true;
        else if (a == "--gpu-profile") gs::gpu::g_gpu_profile = true;
        else if (a == "--poll-stats") hw::g_poll_stats = true;
        else if (a == "--gpu-timing") gs::gpu::g_gpu_timing = true;
        else if (a == "--text-log") { extern void texthook_set_log(bool); texthook_set_log(true); }
        else if (a.rfind("--clock-scale=", 0) == 0) kernel::set_clock_scale(atof(a.c_str() + 14));
        else if (a == "--force60") cfg.fps60 = true;
        else if (a == "--force60-battle") cfg.fps60 = cfg.fps60_battle = true;
        else if (a == "--force60-all") { cfg.fps60 = cfg.fps60_battle = true; kernel::g_test_force60_all = true; }
        else if (a.rfind("--state-save=", 0) == 0 || a.rfind("--state-load=", 0) == 0) {
            // test: --state-save=SEC:SLOT / --state-load=SEC:SLOT (host seconds after start)
            double at = 0;
            int slot = 1;
            sscanf(a.c_str() + 13, "%lf:%d", &at, &slot);
            bool save = a[8] == 's' && a[9] == 'a';
            std::thread([at, slot, save] {
                while (host_time_sec() < at) Sleep(50);
                if (save) ss::request_save(slot);
                else ss::request_load(slot);
            }).detach();
        }
        else if (a.rfind("--resume-test=", 0) == 0)
            sscanf(a.c_str() + 14, "%lf,%lf", &kernel::g_resume_test_at, &kernel::g_resume_test_period);
        else if (a.rfind("--vol=", 0) == 0) sscanf(a.c_str() + 6, "%d,%d,%d,%d", &cfg.vol_music, &cfg.vol_voice, &cfg.vol_sfx, &cfg.vol_movie);
        else if (a.rfind("--ramdump=", 0) == 0) {
            for (const char* p = a.c_str() + 10; *p;) {
                platform::g_ram_dump_at.push_back(atof(p));
                while (*p && *p != ',') p++;
                if (*p == ',') p++;
            }
        }
        else if (a.rfind("--renderer=", 0) == 0) cfg.renderer = a.substr(11) == "gpu" ? 1 : 0;
        else if (a.rfind("--scale=", 0) == 0) cfg.internal_scale = atoi(a.c_str() + 8);
        else if (a.rfind("--draw-distance=", 0) == 0) cfg.draw_distance = atoi(a.c_str() + 16);
        else if (a == "--gpu-compare") { g_gpu_compare_flag = true; cfg.renderer = 1; cfg.internal_scale = 1; }
        else if (a.rfind("--gs-census=", 0) == 0) {
            double f = 0, u = 1e30;
            sscanf(a.c_str() + 12, "%lf:%lf", &f, &u);
            gs::census_enable(f, u);
        }
        else if (a == "--ipu-trace") { extern void ipu_enable_trace(); ipu_enable_trace(); }
        else if (a == "--vu-interp") { extern void vu_set_mode(int); vu_set_mode(0); }
        else if (a == "--vu-verify") { extern void vu_set_mode(int); vu_set_mode(2); }
        else if (a.rfind("--shots-window=", 0) == 0) {
            double f = 0, u = 1e30;
            sscanf(a.c_str() + 15, "%lf:%lf", &f, &u);
            platform::set_screenshot_window(f, u);
        }
        else if (a == "--vu-dump") { extern bool vu_dump_flag(); vu_dump_flag(); }
        else if (a.rfind("--draw-stats=", 0) == 0) gs::g_draw_stats_at = atof(a.c_str() + 13);
        else if (a.rfind("--press=", 0) == 0) platform::add_autopress(a.substr(8));
        else if (a == "--log-draws") { extern bool gs_log_draws_flag; gs_log_draws_flag = true; }
        else if (a.rfind("--log-draws-from=", 0) == 0) { gs_log_draws_flag = true; gs::g_log_draws_from = atof(a.substr(17).c_str()); }
        else if (a.rfind("--vramdump=", 0) == 0) { gs::g_vram_dump_at = atof(a.substr(11).c_str()); gs::g_vram_dump_path = dir + "/vram.bin"; }
        else if (a.rfind("--wav=", 0) == 0) audio::set_wav_path(a.substr(6));
        else if (a.rfind("--shots=", 0) == 0) platform::set_screenshot_interval(atof(a.substr(8).c_str()), dir + "/shots");
    }
    platform::init_console(cfg.console);
    g_logfile = fopen((dir + "\\digimonworld4.log").c_str(), "w");
    log_write(LOG_MAIN, "Steam Deck: %s", platform::steam_deck_info().c_str());

    cfg.disc = 1;
    std::string iso = cfg.iso_path;
    if (iso.empty() || !file_exists_u8(iso)) {
        iso.clear();
        for (const std::string& candidate : {dir + "\\Digimon World 4 (USA).iso", dir + "\\disc1.iso"})
            if (file_exists_u8(candidate)) { iso = candidate; break; }
    }
    if (iso.empty() || !file_exists_u8(iso)) {
        iso = platform::choose_disc_image(1);
        if (iso.empty()) {
            platform::message(L"Selecione a imagem ISO do seu Digimon World 4 (USA), versao SLUS_208.36.");
            return 1;
        }
    }
    u32 boot_lsn = 0, boot_size = 0;
    if (!load_game_with_progress(iso, boot_lsn, boot_size)) {
        platform::message(L"Nao foi possivel ler a imagem ISO. Verifique se ela e a versao USA SLUS_208.36.");
        return 1;
    }
    if (cfg.iso_path != iso) { cfg.iso_path = iso; platform::save_config(); }
    if (g_verify_redirects_flag) return cdvd::verify_redirects() ? 1 : 0;
    mem::init();
    subs::init();
    platform::apply_patches();
    kernel::g_force60 = cfg.fps60;
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
