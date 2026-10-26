cmake_minimum_required(VERSION 3.24)
project(DSVR VERSION 0.16.0 LANGUAGES C CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT so the mod DLLs need no VC redist on user machines.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(DSVR_BUILD_TESTS "Build CTest unit tests" ON)
option(DSVR_BUILD_SETUP "Build DSVR-Setup installer/configurator" ON)
option(DSVR_BUILD_RESHADE_ADDON "Build the optional ReShade D3D11 host adapter" OFF)

# Warnings as errors for our own code (not third_party).
if(MSVC)
  # /MP: compile translation units within each target in parallel. Safe: no
  # #import, no PCH, no /Gm; every TU owns its object file. MSBuild /m
  # (--parallel) still owns project-level parallelism on top of this.
  add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/W4;/MP;/permissive-;/EHsc>")
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

add_subdirectory(src/common)
add_subdirectory(src/loader)
add_subdirectory(src/core)
if(DSVR_BUILD_RESHADE_ADDON)
  add_subdirectory(src/reshade)
endif()

if(DSVR_BUILD_SETUP)
  add_subdirectory(src/setup)
endif()

if(DSVR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# Convenience: refresh the overwritable dev test package in dist/.
# This does NOT run CTest or --doctor; the gated guarantee (build + tests +
# doctor, then package) is tools/auto_verify.ps1. Formal immutable releases
# still use: python tools/package.py --build build --out dist --version <new>
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND AND TARGET dinput8_proxy AND TARGET dsvr_core)
  set(_dsvr_pkg_args --build ${CMAKE_BINARY_DIR} --out ${CMAKE_SOURCE_DIR}/dist
      --version dev --latest)
  if(DSVR_BUILD_RESHADE_ADDON)
    list(APPEND _dsvr_pkg_args --reshade-addon)
  endif()
  add_custom_target(dsvr_package_latest
    COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_SOURCE_DIR}/tools/package.py ${_dsvr_pkg_args}
    COMMENT "Refreshing dist/DSVR-latest (no CTest/doctor gate; use tools/auto_verify.ps1 for the gated package)")
  add_dependencies(dsvr_package_latest dinput8_proxy dsvr_core)
  if(TARGET dsvr_setup)
    add_dependencies(dsvr_package_latest dsvr_setup)
  endif()
  if(TARGET dsvr_reshade)
    add_dependencies(dsvr_package_latest dsvr_reshade)
  endif()
endif()
