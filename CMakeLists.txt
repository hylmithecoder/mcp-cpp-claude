cmake_minimum_required(VERSION 3.14)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SQLite3 REQUIRED)

project(mcp VERSION 2026.1)

add_subdirectory(src)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include(FetchContent)

FetchContent_Declare(
    nlohmannjson
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(nlohmannjson)

add_executable(mcp ${SOURCE_SRC})

target_link_libraries(mcp PRIVATE nlohmann_json::nlohmann_json pthread)

if(SQLite3_FOUND)
    target_link_libraries(mcp PRIVATE ${SQLite3_LIBRARIES})
    target_include_directories(mcp PRIVATE ${SQLite3_INCLUDE_DIRS})
endif()