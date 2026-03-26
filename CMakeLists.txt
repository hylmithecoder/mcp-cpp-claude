cmake_minimum_required(VERSION 3.14)
if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(mcp VERSION 2026.0.2)

# Prevent leakage of host headers (/usr/include) during cross-compilation
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

add_subdirectory(src)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

include(FetchContent)

# ghc::filesystem (for Android compatibility with old NDKs)
if(ANDROID)
    FetchContent_Declare(ghc_filesystem
        GIT_REPOSITORY https://github.com/gulrak/filesystem.git
        GIT_TAG v1.5.14)
    FetchContent_MakeAvailable(ghc_filesystem)
endif()

# nlohmann_json
if(NOT CMAKE_CROSSCOMPILING)
    find_package(nlohmann_json QUIET)
endif()
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmannjson
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.12.0)
    FetchContent_MakeAvailable(nlohmannjson)
endif()

# SQLite3
if(NOT CMAKE_CROSSCOMPILING)
    find_package(SQLite3 QUIET)
endif()
if(SQLite3_FOUND)
    set(SQLITE3_LIBS ${SQLite3_LIBRARIES})
    set(SQLITE3_INCS ${SQLite3_INCLUDE_DIRS})
else()
    FetchContent_Declare(sqlite3
        GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
        GIT_TAG master)
    FetchContent_MakeAvailable(sqlite3)
    set(SQLITE3_LIBS SQLite3)
    # Get the source directory for includes
    FetchContent_GetProperties(sqlite3)
    set(SQLITE3_INCS "${sqlite3_SOURCE_DIR}")
endif()

add_executable(mcp ${SOURCE_SRC})
target_link_libraries(mcp PRIVATE nlohmann_json::nlohmann_json ${SQLITE3_LIBS})
if(SQLITE3_INCS)
    target_include_directories(mcp PRIVATE ${SQLITE3_INCS})
endif()
if(WIN32)
    target_link_libraries(mcp PRIVATE ws2_32)
    target_compile_definitions(mcp PRIVATE _HAS_STD_BYTE=0)
elseif(ANDROID)
    target_link_libraries(mcp PRIVATE log ghc_filesystem)
else()
    target_link_libraries(mcp PRIVATE pthread)
endif()