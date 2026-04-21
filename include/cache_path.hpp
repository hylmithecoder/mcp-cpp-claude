#pragma once

#include <string>
#include <cstdlib>

#include "fs_alias.hpp"

namespace Tools {

inline std::string get_cache_path() {
    #ifdef _WIN32
        const char* base = std::getenv("LOCALAPPDATA");
        if (base) {
            return std::string(base) + "\\mcp\\";
        }
        return "mcp\\";

    #elif __APPLE__
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/Library/Caches/mcp/";
        }
        return "./mcp/";

    #else
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + "/.cache/mcp/";
        }
        return "./.cache/mcp/";
    #endif
    }

    inline void ensure_cache_dir() {
        fs::create_directories(get_cache_path());
    }

}
