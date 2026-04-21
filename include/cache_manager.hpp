#pragma once

#include <string>
#include <filesystem>
#include <chrono>
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
            std::filesystem::create_directories(base());
        }

        static void cleanup_old(std::chrono::hours max_age = std::chrono::hours(24)) {
            auto now = std::filesystem::file_time_type::clock::now();

            for (const auto& entry : std::filesystem::directory_iterator(base())) {
                auto ftime = std::filesystem::last_write_time(entry);
                if (now - ftime > max_age) {
                    std::filesystem::remove_all(entry);
                }
            }
        }

        static std::string history_db() {
            return file("mcp_history.db");
        }
};

}
