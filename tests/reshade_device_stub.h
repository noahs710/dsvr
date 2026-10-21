#pragma once
#include <reshade_events.hpp>
#include <cstdlib>

namespace reshade_test {
using namespace reshade::api;

// Only get_api is legal in this adapter fixture. Fail hard, including Release,
// if production starts guessing native pointers or invoking other SDK methods.
// Interface declarations follow the licensed SDK; no host implementation copied.
class Device final : public device {
  [[noreturn]] static void Unexpected() { std::abort(); }
 public:
  device_api api = device_api::d3d11;
  device_api get_api() const override { return api; }
  uint64_t get_native() const override { Unexpected(); }
  void get_private_data(const uint8_t[16], uint64_t*) const override { Unexpected(); }
  void set_private_data(const uint8_t[16], uint64_t) override { Unexpected(); }
  bool check_capability(device_caps) const override { Unexpected(); }
  bool check_format_support(format, resource_usage) const override { Unexpected(); }
  bool create_sampler(const sampler_desc&, sampler*) override { Unexpected(); }
  void destroy_sampler(sampler) override { Unexpected(); }
  bool create_resource(const resource_desc&, const subresource_data*, resource_usage, resource*, void**) override { Unexpected(); }
  void destroy_resource(resource) override { Unexpected(); }
  resource_desc get_resource_desc(resource) const override { Unexpected(); }
  bool create_resource_view(resource, resource_usage, const resource_view_desc&, resource_view*) override { Unexpected(); }
  void destroy_resource_view(resource_view) override { Unexpected(); }
  resource get_resource_from_view(resource_view) const override { Unexpected(); }
  resource_view_desc get_resource_view_desc(resource_view) const override { Unexpected(); }
  bool map_buffer_region(resource, uint64_t, uint64_t, map_access, void**) override { Unexpected(); }
  void unmap_buffer_region(resource) override { Unexpected(); }
  bool map_texture_region(resource, uint32_t, const subresource_box*, map_access, subresource_data*) override { Unexpected(); }
  void unmap_texture_region(resource, uint32_t) override { Unexpected(); }
  void update_buffer_region(const void*, resource, uint64_t, uint64_t) override { Unexpected(); }
  void update_texture_region(const subresource_data&, resource, uint32_t, const subresource_box*) override { Unexpected(); }
  bool create_pipeline(pipeline_layout, uint32_t, const pipeline_subobject*, pipeline*) override { Unexpected(); }
  void destroy_pipeline(pipeline) override { Unexpected(); }
  bool create_pipeline_layout(uint32_t, const pipeline_layout_param*, pipeline_layout*) override { Unexpected(); }
  void destroy_pipeline_layout(pipeline_layout) override { Unexpected(); }
  bool allocate_descriptor_tables(uint32_t, pipeline_layout, uint32_t, descriptor_table*) override { Unexpected(); }
  void free_descriptor_tables(uint32_t, const descriptor_table*) override { Unexpected(); }
  void get_descriptor_heap_offset(descriptor_table, uint32_t, uint32_t, descriptor_heap*, uint32_t*) const override { Unexpected(); }
  void copy_descriptor_tables(uint32_t, const descriptor_table_copy*) override { Unexpected(); }
  void update_descriptor_tables(uint32_t, const descriptor_table_update*) override { Unexpected(); }
  bool create_query_heap(query_type, uint32_t, query_heap*) override { Unexpected(); }
  void destroy_query_heap(query_heap) override { Unexpected(); }
  bool get_query_heap_results(query_heap, uint32_t, uint32_t, void*, uint32_t) override { Unexpected(); }
  void set_resource_name(resource, const char*) override { Unexpected(); }
  void set_resource_view_name(resource_view, const char*) override { Unexpected(); }
  bool create_fence(uint64_t, fence_flags, fence*, void**) override { Unexpected(); }
  void destroy_fence(fence) override { Unexpected(); }
  uint64_t get_completed_fence_value(fence) const override { Unexpected(); }
  bool wait(fence, uint64_t, uint64_t) override { Unexpected(); }
  bool signal(fence, uint64_t) override { Unexpected(); }
  bool get_property(device_properties, void*) const override { Unexpected(); }
  uint64_t get_resource_view_gpu_address(resource_view) const override { Unexpected(); }
  void get_acceleration_structure_size(acceleration_structure_type, acceleration_structure_build_flags, uint32_t, const acceleration_structure_build_input*, uint64_t*, uint64_t*, uint64_t*) const override { Unexpected(); }
  bool get_pipeline_shader_group_handles(pipeline, uint32_t, uint32_t, void*) override { Unexpected(); }
};

class Swapchain final : public swapchain {
 public:
  Device* device = nullptr;
  void* window = nullptr;
  resource images[2]{};
  uint32_t count = 2, current = 0, reads = 0, last_index = 0;
  uint64_t get_native() const override { std::abort(); }
  void get_private_data(const uint8_t[16], uint64_t*) const override { std::abort(); }
  void set_private_data(const uint8_t[16], uint64_t) override { std::abort(); }
  reshade::api::device* get_device() override { return device; }
  void* get_hwnd() const override { return window; }
  resource get_back_buffer(uint32_t index) override {
    ++reads; last_index = index; return index < 2 ? images[index] : resource{0};
  }
  uint32_t get_back_buffer_count() const override { return count; }
  uint32_t get_current_back_buffer_index() const override { return current; }
  bool check_color_space_support(color_space) const override { std::abort(); }
  color_space get_color_space() const override { std::abort(); }
};
}  // namespace reshade_test
