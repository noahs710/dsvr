cmake_minimum_required(VERSION 3.24)

add_executable(dsvr_setup setup.cpp gui.cpp gui.h hardware.cpp fps_unlock.cpp json.cpp json.h launch.cpp launch.h)
target_link_libraries(dsvr_setup PRIVATE dsvr_common comdlg32 advapi32 shell32 dxgi winhttp)
set_target_properties(dsvr_setup PROPERTIES OUTPUT_NAME "DSVR-Setup")
