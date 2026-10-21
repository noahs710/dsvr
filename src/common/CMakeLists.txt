cmake_minimum_required(VERSION 3.24)

add_library(dsvr_common STATIC
  version.h
  logging.h
  logging.cpp
  config.h
  config.cpp
  blur_files.cpp
  blur_files.h
  blur_state.h
  memory.h
  memory.cpp
)

target_include_directories(dsvr_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(dsvr_common PUBLIC bcrypt)
