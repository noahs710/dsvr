cmake_minimum_required(VERSION 3.24)

# Two variants: the LAB-gated bodies in vr_trace.cpp (input-blocker/step/
# contact/interaction observers) compile out without the define, so the four
# LAB tests link the LAB variant. test_lab_runner exercises the no-LAB path
# with the plain variant. Each TU compiles exactly once per variant (2x total,
# down from 9x spread across the old per-target lists).
add_library(dsvr_test_support STATIC
  ../src/core/vr_trace.cpp
  ../src/core/combat_native.cpp
  ../src/core/lab_runner.cpp
  ../src/core/lab_scenario.cpp
)
target_include_directories(dsvr_test_support PUBLIC
  $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(dsvr_test_support PUBLIC dsvr_common minhook)
add_library(dsvr_test_support_lab STATIC
  ../src/core/vr_trace.cpp
  ../src/core/combat_native.cpp
  ../src/core/lab_runner.cpp
  ../src/core/lab_scenario.cpp
)
target_compile_definitions(dsvr_test_support_lab PRIVATE DSVR_ENABLE_LAB)
target_include_directories(dsvr_test_support_lab PUBLIC
  $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(dsvr_test_support_lab PUBLIC dsvr_common minhook)

# No external test framework: plain CHECK (never assert), one binary per
# area, all runnable without the game, plus bounded Python tool tests.
add_executable(test_xr_inactive_frame_pump test_xr_inactive_frame_pump.cpp)
target_include_directories(test_xr_inactive_frame_pump PRIVATE
  $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME xr_inactive_frame_pump COMMAND test_xr_inactive_frame_pump)
add_executable(test_cpu_frame_timer test_cpu_frame_timer.cpp)
add_test(NAME cpu_frame_timer COMMAND test_cpu_frame_timer)
add_executable(test_d3d11_state_guard test_d3d11_state_guard.cpp)
target_link_libraries(test_d3d11_state_guard PRIVATE d3d11 d3dcompiler)
add_test(NAME d3d11_state_guard COMMAND test_d3d11_state_guard)
add_executable(test_frame_rate_native test_frame_rate_native.cpp ../src/core/frame_rate.cpp)
target_include_directories(test_frame_rate_native PRIVATE $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME frame_rate_native COMMAND test_frame_rate_native)

add_executable(test_weapon_weight test_weapon_weight.cpp)
target_include_directories(test_weapon_weight PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME weapon_weight COMMAND test_weapon_weight)

add_executable(test_combat_contact test_combat_contact.cpp)
add_test(NAME combat_contact COMMAND test_combat_contact)
add_executable(test_combat_atkparam test_combat_atkparam.cpp)
add_test(NAME combat_atkparam COMMAND test_combat_atkparam)
add_executable(test_combat_trace test_combat_trace.cpp)
target_compile_definitions(test_combat_trace PRIVATE DSVR_ENABLE_LAB)
target_link_libraries(test_combat_trace PRIVATE dsvr_test_support_lab)
add_test(NAME combat_native_trace COMMAND test_combat_trace)
add_test(NAME combat_native_trace_filtered COMMAND test_combat_trace --combat-only)
add_test(NAME combat_native_trace_session COMMAND test_combat_trace --session-only)
if(WIN32)
  find_program(DSVR_PWSH_EXECUTABLE NAMES pwsh REQUIRED)
  add_test(NAME combat_capture_transaction COMMAND ${DSVR_PWSH_EXECUTABLE}
    -NoProfile -File ${CMAKE_SOURCE_DIR}/tools/run_combat_capture.ps1 -Mode SelfTest -WindowMinutes 30)
  # Slow by design: real 1 s case windows + pwsh/python startup, not trimmable fat.
  set_tests_properties(combat_capture_transaction PROPERTIES LABELS slow)
endif()
add_executable(test_depth_probe test_depth_probe.cpp ../src/core/depth_probe.cpp)
target_link_libraries(test_depth_probe PRIVATE d3d11 minhook)
add_test(NAME depth_probe COMMAND test_depth_probe)
add_executable(test_afr_policy test_afr_policy.cpp)
add_test(NAME afr_policy COMMAND test_afr_policy)
add_executable(test_stereo_caller test_stereo_caller.cpp)
add_test(NAME stereo_caller COMMAND test_stereo_caller)
add_executable(test_ui_suppression test_ui_suppression.cpp)
add_test(NAME ui_suppression COMMAND test_ui_suppression)
add_executable(test_blur_enforcement test_blur_enforcement.cpp)
add_test(NAME blur_enforcement COMMAND test_blur_enforcement)
add_executable(test_blur_files test_blur_files.cpp)
target_link_libraries(test_blur_files PRIVATE dsvr_common)
add_test(NAME blur_files COMMAND test_blur_files)
add_executable(test_native_effects test_native_effects.cpp ../src/core/native_ui.cpp ../src/core/motion_blur.cpp)
target_include_directories(test_native_effects PRIVATE $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME native_effects COMMAND test_native_effects)
add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE dsvr_common)
add_test(NAME config COMMAND test_config)
add_executable(test_supported_builds test_supported_builds.cpp)
add_test(NAME supported_builds COMMAND test_supported_builds)
add_executable(test_logging test_logging.cpp)

add_test(NAME logging_buffer COMMAND test_logging)
add_executable(test_calibration_math test_calibration_math.cpp)
add_test(NAME calibration_math COMMAND test_calibration_math)
add_executable(test_calibration_fit test_calibration_fit.cpp)
add_test(NAME calibration_fit COMMAND test_calibration_fit)
add_executable(test_calibration test_calibration.cpp ../src/core/calibration.cpp)
add_test(NAME calibration_persistence COMMAND test_calibration)
add_executable(test_weapon_grip test_weapon_grip.cpp ../src/core/weapon_grip.cpp)
add_test(NAME weapon_grip COMMAND test_weapon_grip)
add_executable(test_combat_native test_combat_native.cpp)
target_link_libraries(test_combat_native PRIVATE dsvr_test_support_lab)
add_test(NAME combat_equipment COMMAND test_combat_native)
add_executable(test_mod_menu test_mod_menu.cpp ../src/core/mod_menu.cpp)
target_link_libraries(test_mod_menu PRIVATE dsvr_common dsvr_gui d3d11 dxgi)
add_test(NAME mod_menu_render COMMAND test_mod_menu)

add_executable(test_core_start test_core_start.cpp ../src/core/core.cpp)
target_link_libraries(test_core_start PRIVATE advapi32)
foreach(mode observer disabled disabled-nohost disabled-blur-failed disabled-earlyframe vr refused nohost session)
  add_test(NAME core_start_${mode} COMMAND test_core_start ${mode})
endforeach()
add_executable(test_core_start_lab test_core_start.cpp ../src/core/core.cpp)
target_compile_definitions(test_core_start_lab PRIVATE DSVR_ENABLE_LAB=1)
target_link_libraries(test_core_start_lab PRIVATE advapi32)
add_test(NAME core_start_session_lab COMMAND test_core_start_lab session)
add_test(NAME core_start_session_no_input COMMAND test_core_start_lab session-no-input)

add_executable(test_camera_observer test_camera_observer.cpp
  ../src/core/camhook.cpp ../src/core/camera_observer.cpp)
target_include_directories(test_camera_observer PRIVATE
  $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME camera_observer COMMAND test_camera_observer)

add_executable(test_camera_consumer test_camera_consumer.cpp)
target_link_libraries(test_camera_consumer PRIVATE dsvr_test_support)
add_test(NAME camera_consumer COMMAND test_camera_consumer)
add_test(NAME camera_consumer_derived COMMAND test_camera_consumer derived)
add_test(NAME camera_constant_upload COMMAND test_camera_consumer upload)
add_test(NAME camera_constant_upload_signature COMMAND test_camera_consumer upload-signature)
add_test(NAME camera_consumer_refused COMMAND test_camera_consumer refused)
add_test(NAME camera_consumer_signature COMMAND test_camera_consumer signature)

add_executable(test_diagnostics_mode test_diagnostics_mode.cpp
  ../src/core/diagnostics.cpp ../src/core/autonomous_capture.cpp)
target_link_libraries(test_diagnostics_mode PRIVATE dsvr_common)
add_test(NAME diagnostics_observer COMMAND test_diagnostics_mode observer)
add_test(NAME diagnostics_observer_refused COMMAND test_diagnostics_mode observer-refused)
add_test(NAME diagnostics_vr COMMAND test_diagnostics_mode vr)
add_test(NAME diagnostics_trace_go COMMAND test_diagnostics_mode go)
add_test(NAME diagnostics_disabled COMMAND test_diagnostics_mode disabled)
add_test(NAME diagnostics_combat COMMAND test_diagnostics_mode combat)
add_test(NAME diagnostics_autonomous_session COMMAND test_diagnostics_mode session)
add_test(NAME diagnostics_autonomous_operator_stop COMMAND test_diagnostics_mode session-stop)
# Slow by design: each waits on the real 350 ms settings debounce plus the
# 40 ms worker pump, several times per test. Not trimmable without changing
# production timing, so they are labeled out of the quick loop instead.
set_tests_properties(diagnostics_observer diagnostics_observer_refused
  diagnostics_vr diagnostics_disabled diagnostics_combat diagnostics_trace_go PROPERTIES LABELS slow)

add_executable(test_memory test_memory.cpp)
target_link_libraries(test_memory PRIVATE dsvr_common)
add_test(NAME memory COMMAND test_memory)
add_executable(test_memory_write_path test_memory_write_path.cpp)
target_link_libraries(test_memory_write_path PRIVATE bcrypt)
add_test(NAME memory_write_path COMMAND test_memory_write_path)
add_executable(test_presence test_presence.cpp ../src/core/presence.cpp)
target_include_directories(test_presence PRIVATE $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME presence_visibility COMMAND test_presence)
add_executable(test_body_visibility test_body_visibility.cpp ../src/core/body_visibility.cpp)
target_include_directories(test_body_visibility PRIVATE $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME body_visibility COMMAND test_body_visibility)
add_test(NAME body_visibility_enable_rollback COMMAND test_body_visibility enable)

add_executable(test_camera_math test_camera_math.cpp)
add_test(NAME camera_math COMMAND test_camera_math)
add_executable(test_menu_presentation test_menu_presentation.cpp)
add_test(NAME menu_presentation COMMAND test_menu_presentation)
add_executable(test_gesture_math test_gesture_math.cpp)
add_test(NAME gesture_math COMMAND test_gesture_math)
add_executable(test_menu_state test_menu_state.cpp)
add_test(NAME menu_state COMMAND test_menu_state)
add_executable(test_turn_math test_turn_math.cpp)
add_test(NAME turn_math COMMAND test_turn_math)
add_executable(test_hand_math test_hand_math.cpp)
add_test(NAME hand_math COMMAND test_hand_math)
add_executable(test_combat_math test_combat_math.cpp)
add_test(NAME combat_math COMMAND test_combat_math)
add_executable(test_swing_attack test_swing_attack.cpp)
add_test(NAME swing_attack COMMAND test_swing_attack)
add_executable(test_shield_guard test_shield_guard.cpp)
add_executable(test_skill_flick test_skill_flick.cpp)
add_executable(test_cast_aim test_cast_aim.cpp)
add_test(NAME cast_aim COMMAND test_cast_aim)
add_executable(test_equip_wheel test_equip_wheel.cpp)
add_test(NAME equip_wheel COMMAND test_equip_wheel)
add_executable(test_blade_rearm test_blade_rearm.cpp)
add_test(NAME blade_rearm COMMAND test_blade_rearm)
add_executable(test_weapon_inertia test_weapon_inertia.cpp)
add_test(NAME weapon_inertia COMMAND test_weapon_inertia)
add_test(NAME shield_guard COMMAND test_shield_guard)
add_test(NAME skill_flick COMMAND test_skill_flick)
add_executable(test_arm_swing test_arm_swing.cpp)
add_test(NAME arm_swing COMMAND test_arm_swing)
add_executable(test_setup_theme test_setup_theme.cpp)
# No dsvr_common: every dsvr::/setup:: dependency of gui.cpp is a test stub
# in this TU (config persistence, hardware probe, headset sidecar); linking
# the real common lib would collide with those stubs (LNK2005).
target_link_libraries(test_setup_theme PRIVATE user32 gdi32 shell32 comdlg32)
add_test(NAME setup_theme COMMAND test_setup_theme)
add_executable(test_setup_json test_setup_json.cpp ../src/setup/json.cpp)
# json.cpp is pure string building over setup_internal.h structs; linking the
# real dsvr_common gives it config.cpp (WideFromUtf8/ReadLastHeadsetHz) with
# no stubs, unlike the gui.cpp-based theme test.
target_link_libraries(test_setup_json PRIVATE dsvr_common)
add_test(NAME setup_json COMMAND test_setup_json)
add_executable(test_perf_fit test_perf_fit.cpp)
target_link_libraries(test_perf_fit PRIVATE dsvr_common)
add_test(NAME perf_fit COMMAND test_perf_fit)
add_executable(test_interaction_target test_interaction_target.cpp)
add_test(NAME interaction_target COMMAND test_interaction_target)
add_executable(test_reach_intent test_reach_intent.cpp)
add_test(NAME reach_intent COMMAND test_reach_intent)
add_executable(test_stereo_math test_stereo_math.cpp)
add_test(NAME stereo_math COMMAND test_stereo_math)
add_executable(test_display_rate test_display_rate.cpp)
add_test(NAME display_rate COMMAND test_display_rate)
add_executable(test_xr_eye_resolution test_xr_eye_resolution.cpp)
add_test(NAME xr_eye_resolution COMMAND test_xr_eye_resolution)
add_executable(test_frame_pacing test_frame_pacing.cpp)
add_test(NAME frame_pacing COMMAND test_frame_pacing)
add_executable(test_stereo_render test_stereo_render.cpp)
add_test(NAME stereo_render COMMAND test_stereo_render)
add_executable(test_combo_math test_combo_math.cpp)
add_test(NAME combo_math COMMAND test_combo_math)
add_executable(test_xr_stereo_frame test_xr_stereo_frame.cpp)
target_include_directories(test_xr_stereo_frame PRIVATE
  $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME xr_stereo_frame COMMAND test_xr_stereo_frame)
add_executable(test_xr_mono_recovery test_xr_mono_recovery.cpp)
target_link_libraries(test_xr_mono_recovery PRIVATE OpenXR::openxr_loader d3d11 d3dcompiler dxgi)
add_test(NAME xr_mono_recovery COMMAND test_xr_mono_recovery)
add_executable(test_comfort_vignette test_comfort_vignette.cpp)
add_test(NAME comfort_vignette COMMAND test_comfort_vignette)
set_tests_properties(xr_mono_recovery PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
add_executable(test_frame_queue test_frame_queue.cpp)
add_test(NAME frame_queue COMMAND test_frame_queue)
add_executable(test_roomscale_math test_roomscale_math.cpp)
add_test(NAME roomscale_math COMMAND test_roomscale_math)
add_executable(test_pad_math test_pad_math.cpp)
add_test(NAME pad_math COMMAND test_pad_math)
add_executable(test_input test_input.cpp ../src/core/input.cpp ../src/core/input_frame_acquisition.cpp
  ../src/core/weapon_grip.cpp)
target_include_directories(test_input PRIVATE
  $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME input COMMAND test_input)
add_executable(test_input_frame_acquisition test_input_frame_acquisition.cpp ../src/core/input_frame_acquisition.cpp)
target_include_directories(test_input_frame_acquisition PRIVATE
  $<TARGET_PROPERTY:openxr_loader,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME input_frame_acquisition COMMAND test_input_frame_acquisition)
set_tests_properties(input PROPERTIES TIMEOUT 15)
add_executable(test_arm_ik_math test_arm_ik_math.cpp)
add_test(NAME arm_ik_math COMMAND test_arm_ik_math)
add_executable(test_arm_pose_math test_arm_pose_math.cpp)
add_test(NAME arm_pose_math COMMAND test_arm_pose_math)
add_executable(test_arm_ik_naturalness test_arm_ik_naturalness.cpp)
add_test(NAME arm_ik_naturalness COMMAND test_arm_ik_naturalness)
add_executable(test_xinput_bridge test_xinput_bridge.cpp ../src/core/xinput_bridge.cpp
  ../src/core/xinput_iat_exchange.asm)
target_link_libraries(test_xinput_bridge PRIVATE dsvr_common)
target_include_directories(test_xinput_bridge PRIVATE
  $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
add_test(NAME xinput_bridge COMMAND test_xinput_bridge)
foreach(mode foreign allocation enable)
  add_test(NAME xinput_bridge_${mode} COMMAND test_xinput_bridge ${mode})
endforeach()
set_tests_properties(xinput_bridge PROPERTIES TIMEOUT 15)
add_executable(test_xinput_iat test_xinput_iat.cpp ../src/core/xinput_iat_exchange.asm)
target_include_directories(test_xinput_iat PRIVATE
  $<TARGET_PROPERTY:minhook,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(test_xinput_iat PRIVATE dsvr_common)
add_test(NAME xinput_iat COMMAND test_xinput_iat)
set_tests_properties(xinput_iat PROPERTIES TIMEOUT 15)
add_executable(test_arm_pose_native test_arm_pose_native.cpp ../src/core/arm_ik.cpp ../src/core/weapon_grip.cpp)
target_link_libraries(test_arm_pose_native PRIVATE minhook)
add_test(NAME arm_pose_native COMMAND test_arm_pose_native)
add_executable(test_tracking_frame test_tracking_frame.cpp)
add_test(NAME tracking_frame COMMAND test_tracking_frame)
add_executable(test_vr_heading test_vr_heading.cpp)
add_test(NAME vr_heading COMMAND test_vr_heading)
add_executable(test_body_pose test_body_pose.cpp)
add_test(NAME body_pose COMMAND test_body_pose)
add_executable(test_render_host test_render_host.cpp ../src/core/render_host.cpp)
add_test(NAME render_host COMMAND test_render_host)
if(DSVR_BUILD_RESHADE_ADDON)
  add_executable(test_reshade_addon test_reshade_addon.cpp)
  target_link_libraries(test_reshade_addon PRIVATE dsvr_reshade_sdk)
  target_link_libraries(test_reshade_addon PRIVATE d3d11)
  add_dependencies(test_reshade_addon dsvr_reshade dsvr_core)
  add_test(NAME reshade_addon COMMAND test_reshade_addon
    $<TARGET_FILE:dsvr_reshade> $<TARGET_FILE:dsvr_core>)
  add_library(reshade_frame_fixture SHARED reshade_fixture.cpp
    ../src/core/render_host.cpp ../src/core/reshade_bridge.cpp)
  target_link_libraries(reshade_frame_fixture PRIVATE d3d11)
  set_target_properties(reshade_frame_fixture PROPERTIES OUTPUT_NAME "DSVR"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/reshade_fixture")
  add_dependencies(test_reshade_addon reshade_frame_fixture)
  add_test(NAME reshade_frame COMMAND test_reshade_addon
    $<TARGET_FILE:dsvr_reshade> $<TARGET_FILE:dsvr_core>
    $<TARGET_FILE:reshade_frame_fixture>)
endif()
add_executable(test_vr_trace_capture test_vr_trace_capture.cpp)
add_executable(test_trace_sink test_trace_sink.cpp)
target_link_libraries(test_trace_sink PRIVATE dsvr_common)
add_test(NAME trace_sink COMMAND test_trace_sink $<TARGET_FILE:dsvr_trace_writer>)
target_link_libraries(test_vr_trace_capture PRIVATE dsvr_test_support)
add_test(NAME vr_trace_capture COMMAND test_vr_trace_capture)
add_executable(test_trace_sink_failure test_trace_sink_failure.cpp)
target_link_libraries(test_trace_sink_failure PRIVATE dsvr_test_support)
add_dependencies(test_trace_sink_failure dsvr_trace_writer)
add_test(NAME trace_sink_failure_cleanup COMMAND test_trace_sink_failure $<TARGET_FILE:dsvr_trace_writer>)
add_test(NAME vr_trace_session_header COMMAND test_vr_trace_capture --session-fixture)
add_executable(test_lab_scenario test_lab_scenario.cpp)
target_link_libraries(test_lab_scenario PRIVATE dsvr_test_support)
add_test(NAME lab_scenario COMMAND test_lab_scenario)
add_executable(test_lab_runner test_lab_runner.cpp)
target_link_libraries(test_lab_runner PRIVATE dsvr_test_support)
add_test(NAME lab_runner COMMAND test_lab_runner)
add_executable(test_lab_runner_opt_in test_lab_runner.cpp)
target_compile_definitions(test_lab_runner_opt_in PRIVATE DSVR_ENABLE_LAB)
target_link_libraries(test_lab_runner_opt_in PRIVATE dsvr_test_support_lab)
add_test(NAME lab_runner_opt_in COMMAND test_lab_runner_opt_in)
find_package(Python3 COMPONENTS Interpreter QUIET)
add_executable(test_camera_writes test_camera_writes.cpp
  ../src/core/camera.cpp)
add_test(NAME camera_writes COMMAND test_camera_writes)
if(Python3_FOUND)
  add_test(NAME hmd_report COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hmd_report.py)
  add_test(NAME hmd_game_report COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_hmd_game_report.py)
  if(WIN32)
    add_test(NAME hmd_runner COMMAND ${DSVR_PWSH_EXECUTABLE} -NoProfile -File
      ${CMAKE_SOURCE_DIR}/tools/run_hmd_tests.ps1 -SelfTest)
    set_tests_properties(hmd_runner PROPERTIES TIMEOUT 15 LABELS slow)
  endif()
  add_test(NAME package COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_package.py)
  set_tests_properties(package PROPERTIES TIMEOUT 60 LABELS slow)
  add_test(NAME native_stereo_contract COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_native_stereo.py)
  set_tests_properties(native_stereo_contract PROPERTIES TIMEOUT 30)
  add_test(NAME lab_report COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_lab_report.py $<TARGET_FILE:test_lab_scenario>)
  add_test(NAME vr_trace COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_vr_trace.py $<TARGET_FILE:test_vr_trace_capture>
    $<TARGET_FILE:test_combat_trace>)
  add_test(NAME autonomous_session_report COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_autonomous_session_report.py
    ${CMAKE_SOURCE_DIR}/tools/vr_trace.py)
  add_test(NAME autopilot_analyze COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_autopilot_analyze.py)
    add_test(NAME trace_sink_killed_producer COMMAND ${Python3_EXECUTABLE} -B
    ${CMAKE_CURRENT_SOURCE_DIR}/test_trace_sink.py $<TARGET_FILE:dsvr_trace_writer>)
    set_tests_properties(trace_sink_killed_producer PROPERTIES TIMEOUT 60)
  add_test(NAME skeleton_scan
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/skeleton_scan.py --self-test)
endif()
