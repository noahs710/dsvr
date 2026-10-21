include(FetchContent)
FetchContent_Declare(reshade_sdk
  URL https://codeload.github.com/crosire/reshade/zip/7d13ead8f630d045b9e7e2ac2ce18b2e7e5b25c3
  URL_HASH SHA256=b0da34281c68a6bc4bc28da09623a9f4a2307335012eafffda8cd8ab1504e6e1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# Only the SDK headers are compiled into our small adapter; no ReShade runtime
# is built, installed or distributed by this target.
FetchContent_MakeAvailable(reshade_sdk)
add_library(dsvr_reshade_sdk INTERFACE)
target_include_directories(dsvr_reshade_sdk SYSTEM INTERFACE ${reshade_sdk_SOURCE_DIR}/include)
add_library(dsvr_reshade SHARED addon.cpp)
target_link_libraries(dsvr_reshade PRIVATE dsvr_reshade_sdk)
set_target_properties(dsvr_reshade PROPERTIES
  OUTPUT_NAME "DSVR-ReShade" SUFFIX ".addon64")
