// Headless WARP coverage of the state the production XR blit mutates.
#include "../src/core/d3d11_state_guard.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
      D3D11_SDK_VERSION,&device,nullptr,&context)));
  D3D11_TEXTURE2D_DESC td{}; td.Width=td.Height=16;td.MipLevels=td.ArraySize=1;
  td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;
  td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> color,sampled;
  ComPtr<ID3D11RenderTargetView> target;
  ComPtr<ID3D11ShaderResourceView> resource;
  CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&color)));
  CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&sampled)));
  CHECK(SUCCEEDED(device->CreateRenderTargetView(color.Get(),nullptr,&target)));
  CHECK(SUCCEEDED(device->CreateShaderResourceView(sampled.Get(),nullptr,&resource)));
  td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;td.BindFlags=D3D11_BIND_DEPTH_STENCIL;
  ComPtr<ID3D11Texture2D> depth;
  ComPtr<ID3D11DepthStencilView> depth_view;
  CHECK(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&depth)));
  CHECK(SUCCEEDED(device->CreateDepthStencilView(depth.Get(),nullptr,&depth_view)));
  D3D11_BUFFER_DESC bd{};bd.ByteWidth=256;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
  bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=4;
  ComPtr<ID3D11Buffer> buffer;
  ComPtr<ID3D11UnorderedAccessView> unordered;
  CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&buffer)));
  D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
  ud.Buffer.NumElements=64;ud.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_APPEND;
  CHECK(SUCCEEDED(device->CreateUnorderedAccessView(buffer.Get(),&ud,&unordered)));
  auto* rt=target.Get();auto* uv=unordered.Get();UINT initial_count=7;
  context->OMSetRenderTargetsAndUnorderedAccessViews(1,&rt,depth_view.Get(),1,1,&uv,&initial_count);
  D3D11_BLEND_DESC blend_desc{};blend_desc.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
  ComPtr<ID3D11BlendState> blend;
  CHECK(SUCCEEDED(device->CreateBlendState(&blend_desc,&blend)));
  const float factors[4]{.1f,.2f,.3f,.4f};context->OMSetBlendState(blend.Get(),factors,0x12345678);
  D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;
  dd.DepthFunc=D3D11_COMPARISON_GREATER;
  ComPtr<ID3D11DepthStencilState> depth_state;
  CHECK(SUCCEEDED(device->CreateDepthStencilState(&dd,&depth_state)));
  context->OMSetDepthStencilState(depth_state.Get(),31);
  D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_WIREFRAME;rd.CullMode=D3D11_CULL_FRONT;
  ComPtr<ID3D11RasterizerState> raster;
  CHECK(SUCCEEDED(device->CreateRasterizerState(&rd,&raster)));
  context->RSSetState(raster.Get());
  const D3D11_VIEWPORT viewports[2]{{1,2,8,9,0,1},{3,4,5,6,.2f,.8f}};
  context->RSSetViewports(2,viewports);
  const char* vs_source="float4 main(float4 p:POSITION):SV_Position{return p;}";
  const char* ps_source="float4 main():SV_Target{return 1;}";
  const char* cs_source="[numthreads(1,1,1)] void main(){}";
  ComPtr<ID3DBlob> vs_blob,ps_blob,cs_blob;
  CHECK(SUCCEEDED(D3DCompile(vs_source,std::strlen(vs_source),nullptr,nullptr,nullptr,"main","vs_5_0",0,0,&vs_blob,nullptr)));
  CHECK(SUCCEEDED(D3DCompile(ps_source,std::strlen(ps_source),nullptr,nullptr,nullptr,"main","ps_5_0",0,0,&ps_blob,nullptr)));
  CHECK(SUCCEEDED(D3DCompile(cs_source,std::strlen(cs_source),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&cs_blob,nullptr)));
  ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11ComputeShader> cs;
  CHECK(SUCCEEDED(device->CreateVertexShader(vs_blob->GetBufferPointer(),vs_blob->GetBufferSize(),nullptr,&vs)));
  CHECK(SUCCEEDED(device->CreatePixelShader(ps_blob->GetBufferPointer(),ps_blob->GetBufferSize(),nullptr,&ps)));
  CHECK(SUCCEEDED(device->CreateComputeShader(cs_blob->GetBufferPointer(),cs_blob->GetBufferSize(),nullptr,&cs)));
  context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);context->CSSetShader(cs.Get(),nullptr,0);
  D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
  ComPtr<ID3D11InputLayout> layout;
  CHECK(SUCCEEDED(device->CreateInputLayout(&element,1,vs_blob->GetBufferPointer(),vs_blob->GetBufferSize(),&layout)));
  context->IASetInputLayout(layout.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
  ComPtr<ID3D11SamplerState> sampler;
  CHECK(SUCCEEDED(device->CreateSamplerState(&sd,&sampler)));
  auto* sr=resource.Get();auto* sp=sampler.Get();context->PSSetShaderResources(0,1,&sr);context->PSSetSamplers(0,1,&sp);
  D3D11_QUERY_DESC qd{D3D11_QUERY_OCCLUSION_PREDICATE,0};ComPtr<ID3D11Predicate> predicate;
  CHECK(SUCCEEDED(device->CreatePredicate(&qd,&predicate)));
  context->SetPredication(predicate.Get(),TRUE);
  D3D11_BUFFER_DESC constants_desc{};
  constants_desc.ByteWidth=1024; constants_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
  ComPtr<ID3D11Buffer> constants[2];
  CHECK(SUCCEEDED(device->CreateBuffer(&constants_desc,nullptr,&constants[0])));
  CHECK(SUCCEEDED(device->CreateBuffer(&constants_desc,nullptr,&constants[1])));
  ID3D11Buffer* bindings[2]{constants[0].Get(),constants[1].Get()};
  context->PSSetConstantBuffers(0,2,bindings);
  ComPtr<ID3D11DeviceContext1> context1;
  CHECK(SUCCEEDED(context.As(&context1)));
  UINT first[2]{16,32}, counts[2]{16,16};
  context1->PSSetConstantBuffers1(0,2,bindings,first,counts);
  {
    dsvr::D3D11StateGuard guard(context.Get());
    ComPtr<ID3D11Predicate> now;BOOL condition=TRUE;context->GetPredication(&now,&condition);
    CHECK(!now);
    context->OMSetRenderTargets(0,nullptr,nullptr);context->OMSetBlendState(nullptr,nullptr,~0u);
    context->OMSetDepthStencilState(nullptr,0);context->RSSetState(nullptr);context->RSSetViewports(0,nullptr);
    context->VSSetShader(nullptr,nullptr,0);context->PSSetShader(nullptr,nullptr,0);context->CSSetShader(nullptr,nullptr,0);
    ID3D11ShaderResourceView* no_sr=nullptr;ID3D11SamplerState* no_sp=nullptr;
    context->PSSetShaderResources(0,1,&no_sr);context->PSSetSamplers(0,1,&no_sp);
    ID3D11Buffer* no_constants[2]{};
    context->PSSetConstantBuffers(0,2,no_constants);
    context->IASetInputLayout(nullptr);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    guard.Restore();guard.Restore();
  }
  ComPtr<ID3D11Buffer> restored_constants[2];
  context->PSGetConstantBuffers(0,1,&restored_constants[0]);
  context->PSGetConstantBuffers(1,1,&restored_constants[1]);
  CHECK(restored_constants[0].Get()==bindings[0]);
  CHECK(restored_constants[1].Get()==bindings[1]);
  ID3D11Buffer* ranged[2]{};UINT restored_first[2]{}, restored_counts[2]{};
  context1->PSGetConstantBuffers1(0,2,ranged,restored_first,restored_counts);
  for (unsigned slot=0;slot<2;++slot) {
    CHECK(ranged[slot]==bindings[slot]);
    CHECK(restored_first[slot]==first[slot] && restored_counts[slot]==counts[slot]);
    ranged[slot]->Release();
  }
  ComPtr<ID3D11RenderTargetView> got_rt;ComPtr<ID3D11DepthStencilView> got_dv;
  context->OMGetRenderTargets(1,&got_rt,&got_dv);CHECK(got_rt.Get()==rt && got_dv.Get()==depth_view.Get());
  ComPtr<ID3D11UnorderedAccessView> got_uv;context->OMGetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,1,1,&got_uv);
  CHECK(got_uv.Get()==uv);
  ComPtr<ID3D11BlendState> got_blend;FLOAT got_factors[4]{};UINT mask=0;
  context->OMGetBlendState(&got_blend,got_factors,&mask);
  CHECK(got_blend.Get()==blend.Get() && std::memcmp(got_factors,factors,sizeof(factors))==0 && mask==0x12345678);
  ComPtr<ID3D11DepthStencilState> got_depth;UINT stencil=0;context->OMGetDepthStencilState(&got_depth,&stencil);
  CHECK(got_depth.Get()==depth_state.Get() && stencil==31);
  ComPtr<ID3D11RasterizerState> got_raster;context->RSGetState(&got_raster);CHECK(got_raster.Get()==raster.Get());
  D3D11_VIEWPORT got_vp[2]{};UINT vp_count=2;context->RSGetViewports(&vp_count,got_vp);
  CHECK(vp_count==2 && std::memcmp(got_vp,viewports,sizeof(viewports))==0);
  ComPtr<ID3D11VertexShader> got_vs;ComPtr<ID3D11PixelShader> got_ps;ComPtr<ID3D11ComputeShader> got_cs;
  context->VSGetShader(&got_vs,nullptr,nullptr);context->PSGetShader(&got_ps,nullptr,nullptr);context->CSGetShader(&got_cs,nullptr,nullptr);
  CHECK(got_vs.Get()==vs.Get() && got_ps.Get()==ps.Get() && got_cs.Get()==cs.Get());
  ComPtr<ID3D11ShaderResourceView> got_sr;ComPtr<ID3D11SamplerState> got_sp;
  context->PSGetShaderResources(0,1,&got_sr);context->PSGetSamplers(0,1,&got_sp);
  CHECK(got_sr.Get()==sr && got_sp.Get()==sp);
  ComPtr<ID3D11InputLayout> got_layout;D3D11_PRIMITIVE_TOPOLOGY got_topology;
  context->IAGetInputLayout(&got_layout);context->IAGetPrimitiveTopology(&got_topology);
  CHECK(got_layout.Get()==layout.Get() && got_topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
  ComPtr<ID3D11Predicate> got_predicate;BOOL condition=FALSE;context->GetPredication(&got_predicate,&condition);
  CHECK(got_predicate.Get()==predicate.Get() && condition==TRUE);
  context->SetPredication(nullptr,FALSE);
  bd={};bd.ByteWidth=4;bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> counter;CHECK(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&counter)));
  context->CopyStructureCount(counter.Get(),0,unordered.Get());
  D3D11_MAPPED_SUBRESOURCE mapped{};CHECK(SUCCEEDED(context->Map(counter.Get(),0,D3D11_MAP_READ,0,&mapped)));
  const UINT count=*static_cast<const UINT*>(mapped.pData);context->Unmap(counter.Get(),0);
  CHECK(count==initial_count);
  context->ClearState();
  dsvr::D3D11StateGuard null_guard(nullptr);
  std::puts("PASS WARP native render state, predication and UAV counter restoration");
}
