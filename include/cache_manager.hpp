#pragma once

#include <string>
#include <chrono>

#include "fs_alias.hpp"

#include "cache_path.hpp"

namespace Tools {

class CacheManager {
    public:
        static std::string base() {
            return get_cache_path();
        }

        static std::string file(const std::string& name) {
            return base() + name;
        }

        static void ensure() {
            fs::create_directories(base());
        }

        static void cleanup_old(std::chrono::hours max_age = std::chrono::hours(24)) {
            auto now = fs::file_time_type::clock::now();

            for (const auto& entry : fs::directory_iterator(base())) {
                auto ftime = fs::last_write_time(entry);
                if (now - ftime > max_age) {
                    fs::remove_all(entry);
                }
            }
        }

        static std::string history_db() {
            return file("mcp_history.db");
        }
};

}
