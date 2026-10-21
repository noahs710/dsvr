cmake_minimum_required(VERSION 3.24)

add_library(dinput8_proxy SHARED
  loader.cpp
  loader.def
)

target_link_libraries(dinput8_proxy PRIVATE dsvr_common)
set_target_properties(dinput8_proxy PROPERTIES OUTPUT_NAME "dinput8")

# The proxy must stay tiny and dependency-free (static CRT, no extra DLLs).
target_link_options(dinput8_proxy PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/loader.def)
