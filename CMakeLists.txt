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

FetchContent_Declare(
        libftp
        GIT_REPOSITORY https://github.com/deniskovalchuk/libftp.git
        GIT_TAG        v1.5.0
        PATCH_COMMAND  sed -i "s/Boost 1.88.0/Boost/g" CMakeLists.txt)
FetchContent_MakeAvailable(libftp)

# Find libxml2
find_package(LibXml2 REQUIRED)

add_executable(${PROJECT_NAME} ${SOURCE_SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json ${SQLITE3_LIBS} LibXml2::LibXml2 ftp)
if(SQLITE3_INCS)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SQLITE3_INCS})
endif()
if(WIN32)
    target_link_libraries(mcp PRIVATE ws2_32 bcrypt)
    target_compile_definitions(mcp PRIVATE _HAS_STD_BYTE=0)

    # ---------------------------------------------------------------------------
    # Qt6 Path configuration (Windows / MinGW Cross-compilation)
    # ---------------------------------------------------------------------------
    # Workaround for Qt 6.11.0 host tools vs 6.10.2 target version mismatch
    if(NOT COMMAND _qt_internal_should_include_targets)
        macro(_qt_internal_should_include_targets)
            if(ARGC EQUAL 1)
                set(${ARGV0} TRUE)
            elseif(ARGC EQUAL 2)
                set(${ARGV1} TRUE)
            endif()
        endmacro()
    endif()

    # Detect the downloaded Qt6 Windows binaries in vendor/qt6
    file(GLOB QT6_CANDIDATES "${CMAKE_CURRENT_SOURCE_DIR}/vendor/qt6/[0-9]*/mingw_64")
    list(LENGTH QT6_CANDIDATES CANDIDATE_COUNT)
    if(CANDIDATE_COUNT GREATER 0)
        list(GET QT6_CANDIDATES 0 DETECTED_QT6_DIR)
        message(STATUS "Found Qt6 Windows MinGW installation: ${DETECTED_QT6_DIR}")
        set(CMAKE_PREFIX_PATH "${DETECTED_QT6_DIR}")
    else()
        message(WARNING "Qt6 MinGW binaries not found in vendor/qt6. Using default prefix path.")
        set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/qt6/6.10.2/mingw_64")
    endif()

    # Enable Qt6 and locate components
    find_package(Qt6 REQUIRED COMPONENTS Widgets)

    # Build Qt6 GUI companion app
    add_executable(mcp-gui WIN32 
        src/config_gui.cpp 
        src/windowing/mainwindow.cpp 
        include/windowing/mainwindow.hpp
        src/resources.rc
    )
    set_target_properties(mcp-gui PROPERTIES
        AUTOMOC ON
        AUTOUIC ON
        AUTORCC ON
    )
    target_link_libraries(mcp-gui PRIVATE Qt6::Widgets ws2_32 bcrypt)
elseif(ANDROID)
    target_link_libraries(${PROJECT_NAME} PRIVATE log ghc_filesystem)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()