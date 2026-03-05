cmake_minimum_required(VERSION 3.16)
project(watchmooi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ─────────────────────────────────────────────────────────────

# nlohmann/json (header-only, fetched automatically)
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)

# Google Test (for unit tests)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json googletest)

# libmpv
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

# libcurl
find_package(CURL REQUIRED)

# pthreads
find_package(Threads REQUIRED)
# ── Sources ──────────────────────────────────────────────────────────────────
add_executable(watchmooi
    src/main.cpp
    src/player.cpp
    src/firebase.cpp
    src/sync.cpp
)

target_include_directories(watchmooi PRIVATE src)

target_link_libraries(watchmooi PRIVATE
    PkgConfig::MPV
    CURL::libcurl
    nlohmann_json::nlohmann_json
    Threads::Threads
)

# ── Tests ────────────────────────────────────────────────────────────────────
# Tests use mock Player/Firebase, so they don't need libmpv or libcurl.
# Only sync.cpp is linked because tests exercise Sync logic with mocks.

enable_testing()

add_executable(watchmooi_tests
    tests/tests.cpp
    src/sync.cpp
)

target_include_directories(watchmooi_tests PRIVATE src)

target_link_libraries(watchmooi_tests PRIVATE
    GTest::gtest_main
    Threads::Threads
)

include(GoogleTest)
gtest_discover_tests(watchmooi_tests)

