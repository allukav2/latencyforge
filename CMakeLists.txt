cmake_minimum_required(VERSION 3.25)

project(LatencyForge VERSION 0.1.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# CRT は静的リンク (/MT)。追加ランタイム不要。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LF_REQUIRE_ADMIN "Embed a requireAdministrator manifest (OFF for dev/screenshot builds)" ON)
option(LF_BUILD_TESTS "Build unit tests" ON)

if(NOT MSVC)
  message(FATAL_ERROR "LatencyForge targets MSVC (VS2022) on Windows x64 only.")
endif()

add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

find_package(nlohmann_json CONFIG REQUIRED)

add_subdirectory(core)
add_subdirectory(app)

if(LF_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG REQUIRED)
  add_subdirectory(tests)
endif()
