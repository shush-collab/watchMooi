cmake_minimum_required(VERSION 3.16)
project(watchmooi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ─────────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
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

# ── std::filesystem link (older GCC/Clang may need -lstdc++fs) ───────────────

set(STDFS_LIB "")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
  set(STDFS_LIB stdc++fs)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
  set(STDFS_LIB c++fs)
endif()

# ── macOS Homebrew hints ─────────────────────────────────────────────────────

if(APPLE)
  # Homebrew on Apple Silicon
  list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
  # Homebrew on Intel
  list(APPEND CMAKE_PREFIX_PATH "/usr/local")
endif()

# ── Main binary ──────────────────────────────────────────────────────────────

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
    ${STDFS_LIB}
)

# ── Tests ────────────────────────────────────────────────────────────────────
# Tests use mock Player/Firebase — only sync.cpp is linked.

enable_testing()

add_executable(watchmooi_tests
    tests/tests.cpp
    src/sync.cpp
)

target_include_directories(watchmooi_tests PRIVATE src)

target_link_libraries(watchmooi_tests PRIVATE
    GTest::gtest_main
    Threads::Threads
    ${STDFS_LIB}
)

include(GoogleTest)
gtest_discover_tests(watchmooi_tests)
