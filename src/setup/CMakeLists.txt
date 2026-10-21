cmake_minimum_required(VERSION 3.24)

add_executable(dsvr_setup setup.cpp gui.cpp gui.h)
target_link_libraries(dsvr_setup PRIVATE dsvr_common comdlg32 advapi32 shell32)
set_target_properties(dsvr_setup PROPERTIES OUTPUT_NAME "DSVR-Setup")
