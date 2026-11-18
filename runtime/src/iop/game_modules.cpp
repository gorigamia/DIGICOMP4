#include "game_modules.h"
#include "iop.h"
#include "iop_kernel.h"
#include "../memory.h"
#include <algorithm>
#include <map>

namespace iop::modules {

static std::map<uint32_t, bool> g_started;

static std::string normalize_path(std::string path) {
    size_t device = path.find(':');
    if (device != std::string::npos) path.erase(0, device + 1);
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    size_t version = path.find(';');
    if (version != std::string::npos) path.resize(version);
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char ch) { return (char)toupper(ch); });
    return path;
}

bool start_game_module(const std::string& path, const std::string& args) {
    const std::string normalized = normalize_path(path);
    const size_t separator = normalized.find_last_of('/');
    const std::string basename = separator == std::string::npos ? normalized : normalized.substr(separator + 1);
    for (unsigned index = 0; index < g_game_iop_module_count; index++) {
        const GameIopModule& module = g_game_iop_modules[index];
        const std::string module_path = normalize_path(module.path);
        if (normalized != module_path && basename != module_path.substr(module_path.find_last_of('/') + 1)) continue;
        if (g_started[module.base]) return true;
        if (module.base + module.mem_size > 0x00200000) {
            fatal("IOP module %s exceeds IOP RAM", module.path);
        }

        g_started[module.base] = true;
        memset(mem::iop_ptr(module.base), 0, module.mem_size);
        memcpy(mem::iop_ptr(module.base), module.image, module.file_size);
        kern::register_module_funcs(module.functions, module.function_count);

        const u32 stack = iop::alloc(0x4000, 16);
        const u32 argv = iop::alloc(512, 16);
        if (!stack || !argv) fatal("IOP heap exhausted loading %s", module.path);
        u32 cursor = argv + 64;
        std::vector<std::string> arguments;
        arguments.push_back(module_path.substr(module_path.find_last_of('/') + 1));
        // LoadModule supplies NUL-separated argument strings, not a single printable option.
        size_t pos = 0;
        while (pos < args.size()) {
            size_t end = args.find('\0', pos);
            if (end == std::string::npos) end = args.size();
            if (end != pos) arguments.push_back(args.substr(pos, end - pos));
            pos = end + 1;
        }
        if (arguments.size() >= 16) fatal("Too many IOP module arguments");
        for (size_t n = 0; n < arguments.size(); ++n) {
            const auto& arg = arguments[n];
            if (cursor + arg.size() + 1 > argv + 512) fatal("IOP argument buffer overflow");
            mem::iop_wr32(argv + (u32)n * 4, cursor);
            memcpy(mem::iop_ptr(cursor), arg.c_str(), arg.size() + 1);
            cursor += (u32)arg.size() + 1;
        }
        IOPContext context{};
        context.r[29] = stack + 0x4000 - 16;
        context.r[28] = module.gp;
        context.r[4] = (u32)arguments.size();
        context.r[5] = argv;
        log_write(LOG_IOP, "Starting %s at %08x (args='%s')", module.path, module.entry, args.c_str());
        kern::call_func(&context, module.entry);
        kern::run_ready();
        return true;
    }
    return false;
}

}  // namespace iop::modules