#pragma once

#if defined(__ANDROID__) || defined(ANDROID)
    #include <ghc/filesystem.hpp>
    namespace fs = ghc::filesystem;
#else
    #include <filesystem>
    namespace fs = std::filesystem;
#endif
