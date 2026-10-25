cmake_minimum_required(VERSION 3.24)

include(FetchContent)
# MinHook: tiny x64 hook library (fresh fetch, pinned). Evaluation vs
# SafetyHook recorded in MODLOG: MinHook chosen for size + Present-hook
# track record; SafetyHook stays the fallback if detours misbehave.
FetchContent_Declare(minhook
  GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
  GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

# OpenXR SDK: headers + loader (any runtime: Link / Virtual Desktop /
# SteamVR -- the active runtime is chosen by the system, not by us).
FetchContent_Declare(openxr
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
  GIT_TAG release-1.1.49
)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(openxr)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG f5befd2d29e66809cd1110a152e375a7f1981f06 # v1.91.9b, MIT
)
FetchContent_MakeAvailable(imgui)
add_library(dsvr_gui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(dsvr_gui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(dsvr_gui PUBLIC d3d11 d3dcompiler)
# Force the loader onto our static CRT (it defaults to /MD, which clashes
# with DSVR's /MT and produced LNK2005/LNK2019 on first build).
if(TARGET openxr_loader)
  set_target_properties(openxr_loader PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_library(dsvr_core SHARED
  core.cpp
  gate.h
  gate.cpp
  crash.h
  crash.cpp
  diagnostics.h
  diagnostics.cpp
  mod_menu.cpp
  mod_menu.h
  ui_suppression.h
  native_ui.cpp
  native_ui.h
  motion_blur.cpp
  motion_blur.h
  stereo_gate.h
  afr_policy.h
  combat_contact.h
  calibration.cpp
  calibration.h
  weapon_grip.cpp
  weapon_grip.h
  combat_native.cpp
  combat_native.h
  combat_atkparam.h
  render.h
  render.cpp
  render_host.h
  render_host.cpp
  reshade_bridge.h
  reshade_bridge.cpp
  xr.h
  xr.cpp
  depth_probe.h
  depth_probe.cpp
  display_rate.h
  stereo_render.h
  camera.h
  camera.cpp
  camera_observer.h
  camera_observer.cpp
  camhook.h
  camhook.cpp
  frame_rate.h
  frame_rate.cpp
  input.h
  input.cpp
  xinput_bridge.h
  xinput_bridge.cpp
  xinput_iat_exchange.asm
  presence.h
  presence.cpp
  body_visibility.h
  body_visibility.cpp
  arm_ik.h
  arm_ik.cpp
  arm_ik_math.h
  combat_math.h
  swing_attack_math.h
  shield_guard_math.h
  arm_swing_math.h
  reach_intent_math.h
  hand_math.h
  gesture_math.h
  vr_trace.h
  vr_trace.cpp
  lab_scenario.h
  lab_scenario.cpp
  lab_runner.h
  lab_runner.cpp
)

option(DSVR_ENABLE_LAB "Build opt-in process-local diagnostic scenario controls" OFF)
if(DSVR_ENABLE_LAB)
  target_compile_definitions(dsvr_core PRIVATE DSVR_ENABLE_LAB)
endif()

target_link_libraries(dsvr_core PRIVATE dsvr_common dsvr_gui dbghelp minhook d3d11 dxgi d3dcompiler OpenXR::openxr_loader)
set_target_properties(dsvr_core PROPERTIES OUTPUT_NAME "DSVR")

# Export the loader entry point without a .def file.
target_link_options(dsvr_core PRIVATE /EXPORT:DsvrCoreStart)
