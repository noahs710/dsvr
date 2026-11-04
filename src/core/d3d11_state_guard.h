#pragma once

#include <d3d11_1.h>

namespace dsvr {

// Protect the native renderer's state cache from the XR fullscreen blit. Only
// states the blit writes (plus output bindings it can implicitly unbind) are
// captured. There are no allocations, GPU waits or context clears here.
class D3D11StateGuard {
 public:
  explicit D3D11StateGuard(ID3D11DeviceContext* context) : context_(context) {
    if (!context_) return;
    context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
                                 render_targets_, &depth_view_);
    context_->OMGetRenderTargetsAndUnorderedAccessViews(
        0, nullptr, nullptr, 0, D3D11_PS_CS_UAV_REGISTER_COUNT, unordered_views_);
    context_->OMGetBlendState(&blend_, blend_factor_, &sample_mask_);
    context_->OMGetDepthStencilState(&depth_state_, &stencil_ref_);
    context_->RSGetState(&rasterizer_);
    context_->RSGetViewports(&viewport_count_, viewports_);
    context_->VSGetShader(&vs_.shader, vs_.instances, &vs_.count);
    context_->PSGetShader(&ps_.shader, ps_.instances, &ps_.count);
    context_->GSGetShader(&gs_.shader, gs_.instances, &gs_.count);
    context_->HSGetShader(&hs_.shader, hs_.instances, &hs_.count);
    context_->DSGetShader(&ds_.shader, ds_.instances, &ds_.count);
    context_->CSGetShader(&cs_.shader, cs_.instances, &cs_.count);
    context_->PSGetShaderResources(0, 1, &pixel_resource_);
    context_->PSGetSamplers(0, 1, &pixel_sampler_);
    context_->QueryInterface(__uuidof(ID3D11DeviceContext1),
                              reinterpret_cast<void**>(&context1_));
    if (context1_)
      context1_->PSGetConstantBuffers1(0, 2, pixel_constants_, first_constant_, constant_count_);
    else
      context_->PSGetConstantBuffers(0, 2, pixel_constants_);
    context_->IAGetInputLayout(&input_layout_);
    context_->IAGetPrimitiveTopology(&topology_);
    context_->GetPredication(&predicate_, &predicate_value_);
    // A native visibility predicate must not suppress the compositor image.
    context_->SetPredication(nullptr, FALSE);
  }

  ~D3D11StateGuard() { Restore(); }
  D3D11StateGuard(const D3D11StateGuard&) = delete;
  D3D11StateGuard& operator=(const D3D11StateGuard&) = delete;

  void Restore() {
    if (!context_) return;
    UINT render_target_count = 0;
    for (UINT i = 0; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
      if (render_targets_[i]) render_target_count = i + 1;
    // RTV and pixel-UAV slots share the output-merger namespace. Preserve
    // native append/consume counters by passing KEEP (-1) initial counts.
    UINT keep_counts[D3D11_PS_CS_UAV_REGISTER_COUNT];
    for (auto& value : keep_counts) value = UINT(-1);
    context_->OMSetRenderTargetsAndUnorderedAccessViews(
        render_target_count, render_targets_, depth_view_, render_target_count,
        D3D11_PS_CS_UAV_REGISTER_COUNT - render_target_count,
        unordered_views_ + render_target_count, keep_counts + render_target_count);
    context_->OMSetBlendState(blend_, blend_factor_, sample_mask_);
    context_->OMSetDepthStencilState(depth_state_, stencil_ref_);
    context_->RSSetState(rasterizer_);
    context_->RSSetViewports(viewport_count_, viewports_);
    context_->VSSetShader(vs_.shader, vs_.instances, vs_.count);
    context_->PSSetShader(ps_.shader, ps_.instances, ps_.count);
    context_->GSSetShader(gs_.shader, gs_.instances, gs_.count);
    context_->HSSetShader(hs_.shader, hs_.instances, hs_.count);
    context_->DSSetShader(ds_.shader, ds_.instances, ds_.count);
    context_->CSSetShader(cs_.shader, cs_.instances, cs_.count);
    context_->PSSetShaderResources(0, 1, &pixel_resource_);
    context_->PSSetSamplers(0, 1, &pixel_sampler_);
    if (context1_)
      context1_->PSSetConstantBuffers1(0, 2, pixel_constants_, first_constant_, constant_count_);
    else
      context_->PSSetConstantBuffers(0, 2, pixel_constants_);
    context_->IASetInputLayout(input_layout_);
    context_->IASetPrimitiveTopology(topology_);
    context_->SetPredication(predicate_, predicate_value_);

    for (auto*& target : render_targets_) Release(target);
    for (auto*& view : unordered_views_) Release(view);
    Release(depth_view_); Release(blend_); Release(depth_state_);
    Release(rasterizer_); Release(pixel_resource_); Release(pixel_sampler_);
    for (auto*& constants : pixel_constants_) Release(constants);
    Release(context1_);
    Release(input_layout_); Release(predicate_);
    vs_.Release(); ps_.Release(); gs_.Release();
    hs_.Release(); ds_.Release(); cs_.Release();
    context_ = nullptr;
  }

 private:
  template<class T> static void Release(T*& object) {
    if (object) { object->Release(); object = nullptr; }
  }
  template<class T> struct ShaderState {
    T* shader = nullptr;
    ID3D11ClassInstance* instances[256]{};
    UINT count = 256;
    void Release() {
      D3D11StateGuard::Release(shader);
      for (auto*& instance : instances) D3D11StateGuard::Release(instance);
    }
  };
  ID3D11DeviceContext* context_ = nullptr;
  ID3D11DeviceContext1* context1_ = nullptr;
  UINT first_constant_[2]{}, constant_count_[2]{};
  ID3D11RenderTargetView* render_targets_[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
  ID3D11DepthStencilView* depth_view_ = nullptr;
  ID3D11UnorderedAccessView* unordered_views_[D3D11_PS_CS_UAV_REGISTER_COUNT]{};
  ID3D11BlendState* blend_ = nullptr;
  FLOAT blend_factor_[4]{};
  UINT sample_mask_ = 0, stencil_ref_ = 0;
  ID3D11DepthStencilState* depth_state_ = nullptr;
  ID3D11RasterizerState* rasterizer_ = nullptr;
  D3D11_VIEWPORT viewports_[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
  UINT viewport_count_ = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
  ShaderState<ID3D11VertexShader> vs_;
  ShaderState<ID3D11PixelShader> ps_;
  ShaderState<ID3D11GeometryShader> gs_;
  ShaderState<ID3D11HullShader> hs_;
  ShaderState<ID3D11DomainShader> ds_;
  ShaderState<ID3D11ComputeShader> cs_;
  ID3D11ShaderResourceView* pixel_resource_ = nullptr;
  ID3D11SamplerState* pixel_sampler_ = nullptr;
  ID3D11Buffer* pixel_constants_[2]{};
  ID3D11InputLayout* input_layout_ = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY topology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
  ID3D11Predicate* predicate_ = nullptr;
  BOOL predicate_value_ = FALSE;
};

}  // namespace dsvr
