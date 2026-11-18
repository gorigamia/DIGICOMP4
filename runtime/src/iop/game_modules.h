#pragma once

#include "iop_recomp.h"
#include <string>

struct GameIopModule {
    const char* path;
    uint32_t base;
    uint32_t entry;
    uint32_t gp;
    uint32_t mem_size;
    uint32_t file_size;
    const uint8_t* image;
    const IOPFuncEntry* functions;
    unsigned function_count;
};

extern const GameIopModule g_game_iop_modules[];
extern const unsigned g_game_iop_module_count;

namespace iop::modules {
bool start_game_module(const std::string& path, const std::string& args);
}