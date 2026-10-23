// Render the actual native menu into a headless WARP texture. No game or input
// driving; optional BMP artifacts exercise the same code shipped in the DLL.
#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/core/arm_ik.h"
#include "../src/core/mod_menu.h"
#include "../src/core/menu_input_events.h"
#include "../src/core/calibration.h"
#include "../src/core/body_visibility.h"
#include "../src/core/diagnostics.h"
#include "../src/core/xr.h"
#include "../src/core/menu_presentation.h"
#include "../src/core/weapon_grip.h"
#include "../src/core/combat_native.h"
#include "../src/core/native_ui.h"
#include "../src/core/motion_blur.h"
#define CHECK(x) do {if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
namespace {
bool calibration=false;
dsvr::MenuPointerState pointer_state{};
bool probe_pointer_click=false;
bool pointer_button_clicked=false;
bool pointer_mouse_down=false;
bool navigation_select_down=false;
float pointer_mouse_x=0,pointer_mouse_y=0;
bool inject_deferred_mouse=false;
bool observe_deferred_mouse=false;
bool deferred_mouse_observed=false;
bool deferred_mouse_seen=false;
int deferred_input_events=0;
bool pointer_button_test_active=false;
ImVec2 pointer_button_rect_min{},pointer_button_rect_max{};
bool pointer_button_hovered=false;
dsvr::Config live_config{},saved_config{};
int applies=0,saves=0;
ImGuiContext* menu_context=nullptr;
float footer_x=0,footer_y=0;
}

int CheckInputEventQueue() {
  dsvr::MenuWindowEventQueue queue;
  CHECK(dsvr::IsMenuInputEdge(WM_KEYDOWN));
  CHECK(dsvr::IsMenuInputEdge(WM_KEYUP));
  CHECK(dsvr::IsMenuInputEdge(WM_LBUTTONDOWN));
  CHECK(dsvr::IsMenuInputEdge(WM_LBUTTONUP));
  CHECK(!dsvr::IsMenuInputEdge(WM_MOUSEMOVE));
  CHECK(queue.Push({WM_LBUTTONDOWN,MK_LBUTTON,0}));
  CHECK(queue.Push({WM_LBUTTONUP,0,0}));
  dsvr::MenuWindowEventQueue drained;
  queue.Swap(drained);
  dsvr::MenuWindowEvent event{};
  CHECK(drained.Pop(event) && event.message==WM_LBUTTONDOWN);
  CHECK(drained.Pop(event) && event.message==WM_LBUTTONUP);
  CHECK(!drained.Pop(event));
  CHECK(queue.Push({WM_KEYDOWN,VK_RETURN,0}));
  queue.Clear();
  CHECK(!queue.Pop(event));
  for (std::size_t i=0;i<dsvr::MenuWindowEventQueue::kCapacity;++i)
    CHECK(queue.Push({WM_KEYDOWN,static_cast<WPARAM>(i),0}));
  CHECK(!queue.Push({WM_KEYUP,VK_RETURN,0}));
  return 0;
}

int CheckPanelRegistration() {
  using namespace dsvr;
  const auto draw=[] {};
  CHECK(!ModMenuRegisterPanel(nullptr,"Test panel",draw));
  CHECK(!ModMenuRegisterPanel("","Test panel",draw));
  CHECK(!ModMenuRegisterPanel("test-panel",nullptr,draw));
  CHECK(!ModMenuRegisterPanel("test-panel","",draw));
  CHECK(!ModMenuRegisterPanel("test-panel","Test panel",nullptr));
  CHECK(ModMenuRegisterPanel("test-panel","Test panel",draw));
  CHECK(!ModMenuRegisterPanel("test-panel","Different name",draw));
  // Invalid/duplicate registrations must not consume slots; names need not be unique.
  for (int i=1;i<16;++i) {
    const auto id=std::string("test-panel-")+std::to_string(i);
    CHECK(ModMenuRegisterPanel(id.c_str(),"Test panel",draw));
  }
  CHECK(!ModMenuRegisterPanel("test-panel-overflow","Overflow",draw));
  return 0;
}

namespace dsvr {
NativeUiStatus NativeUiGetStatus() {return {};}
BodyVisibilityStatus BodyVisibilityGetStatus() {return {};}
MotionBlurStatus MotionBlurGetStatus() {return {};}
CalibrationStatus CalibrationGetStatus() {
  if (ImGui::GetCurrentContext()) {
    menu_context=ImGui::GetCurrentContext();
    if (inject_deferred_mouse) {
      ImGui::GetIO().ConfigDebugIgnoreFocusLoss=true;
      const int queued_before=GImGui->InputEventsQueue.Size;
      ImGui::GetIO().AddMouseButtonEvent(0,true);
      deferred_input_events=GImGui->InputEventsQueue.Size-queued_before;
      inject_deferred_mouse=false;
      ModMenuClose();
    }
    if (observe_deferred_mouse) {
      deferred_mouse_seen=ImGui::IsMouseClicked(0)||ImGui::IsMouseDown(0);
      ImGui::GetIO().ConfigDebugIgnoreFocusLoss=false;
      deferred_mouse_observed=true;
      observe_deferred_mouse=false;
    }
    if (probe_pointer_click) {
      pointer_mouse_down=ImGui::IsMouseClicked(0)||ImGui::IsMouseDown(0);
      navigation_select_down=ImGui::IsKeyDown(ImGuiKey_GamepadFaceDown);
      pointer_mouse_x=ImGui::GetIO().MousePos.x;
      pointer_mouse_y=ImGui::GetIO().MousePos.y;
      probe_pointer_click=false;
    }
    if (false) {
    }
  }
  CalibrationStatus s;s.screen_visible=calibration;s.capture_input=calibration;
  s.instruction="Hold arms straight out sideways at shoulder height, palms down";return s;
}
UserCalibration CalibrationCurrent() {return {};}
bool CalibrationCapturesInput() {return calibration;}
void CalibrationRequest() {calibration=true;}
void CalibrationCancel() {calibration=false;}
void CalibrationRecenter() {}
Config DiagnosticsCurrentConfig() {return live_config;}
SettingsSnapshot DiagnosticsSettingsSnapshot() {return {live_config,saved_config,false};}
bool DiagnosticsRequestConfig(const Config& value,bool save) {
  live_config=value;
  if(save){++saves;saved_config=value;}else ++applies;
  return true;
}
std::string DiagnosticsSettingsStatus() {
  footer_x=ImGui::GetWindowPos().x+ImGui::GetStyle().WindowPadding.x;
  footer_y=GImGui->LastItemData.ID ? GImGui->LastItemData.Rect.GetCenter().y :
      GImGui->LastItemData.Rect.Min.y-ImGui::GetStyle().ItemSpacing.y-
      (ImGui::GetFontSize()+2*ImGui::GetStyle().FramePadding.y)*.5f;
  if (pointer_button_test_active) {
    ImGui::SetCursorScreenPos({900.0f,640.0f});
    if (ImGui::Button("Pointer routing test",{160.0f,40.0f}))
      pointer_button_clicked=true;
    pointer_button_rect_min=ImGui::GetItemRectMin();
    pointer_mouse_x=ImGui::GetIO().MousePos.x;
    pointer_mouse_y=ImGui::GetIO().MousePos.y;
    pointer_button_rect_max=ImGui::GetItemRectMax();
    pointer_button_hovered=ImGui::IsItemHovered();
  }
  return "";
}
void DiagnosticsRequestLogExport() {}
void DiagnosticsLogMarker() {}
bool DiagnosticsRequestCommand(const char*,std::function<bool()>) {return true;}
CombatNativeStatus CombatNativeGetStatus() {return {};}
bool ArmIkCombatBench(CombatBenchSnapshot& out) {out = {};return false;}
bool WeaponGripGet(int,bool,WeaponGripProfile&) {return false;}
bool WeaponGripPreview(const WeaponGripProfile&) {return true;}
void WeaponGripClearPreview() {}
bool WeaponGripSave(const WeaponGripProfile&) {return true;}
bool WeaponGripReset(int,bool) {return true;}
void XrRecenterSanctuary() {}
void XrToggleSanctuary() {}
bool XrRenderMetricsSnapshot(RenderMetrics&) {return false;}
bool XrEffectiveRate(EffectiveRate& out) {out={};return true;}
MenuPointerState XrMenuPointerInput(unsigned,unsigned) { return pointer_state; }
}
int main(int argc,char** argv) {
  using Microsoft::WRL::ComPtr;using namespace dsvr;
  CHECK(CheckInputEventQueue()==0);
  CHECK(CheckPanelRegistration()==0);
  calibration=argc>2 && std::string(argv[2])=="calibration";
  const unsigned width=argc>3?static_cast<unsigned>(std::stoi(argv[3])):1280;
  const unsigned height=argc>4?static_cast<unsigned>(std::stoi(argv[4])):720;
  HWND window=CreateWindowExW(0,L"STATIC",L"DSVR menu preview",WS_POPUP,0,0,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  CHECK(window);
  ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
  CHECK(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
  D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
  desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> image;CHECK(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&image)));
  ComPtr<ID3D11RenderTargetView> target;CHECK(SUCCEEDED(device->CreateRenderTargetView(image.Get(),nullptr,&target)));
  ID3D11RenderTargetView* original=target.Get();context->OMSetRenderTargets(1,&original,nullptr);
  const float color[]={.12f,.14f,.17f,1};context->ClearRenderTargetView(target.Get(),color);
  ModMenuToggle();CHECK(ModMenuCapturesInput());
  for(int i=0;i<3;++i) ModMenuRenderTexture(device.Get(),image.Get());
  if(!calibration) {
    // Drive the shipped controls through ImGui's navigation activation, not a
    // duplicate settings-state helper: edit -> Apply -> Save identical values.
    CHECK(menu_context);ImGui::SetCurrentContext(menu_context);
    ImGuiWindow* panel=nullptr;
    for(auto* candidate:GImGui->Windows)
      if(std::string(candidate->Name).find("/Panel_")!=std::string::npos) panel=candidate;
    CHECK(panel);
    ImGui::ActivateItemByID(ImHashStr("Track native arms",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    auto click=[&](float x) {
      pointer_state={true,true,false,x,footer_y};
      ModMenuRenderTexture(device.Get(),image.Get());
      pointer_state.select=true;ModMenuRenderTexture(device.Get(),image.Get());
      pointer_state.select=false;ModMenuRenderTexture(device.Get(),image.Get());
    };
    click(footer_x+55.f);
    CHECK(applies==1 && live_config.arm_ik_enabled);
    click(footer_x+110.f+12.f+77.5f);
    CHECK(saves==1 && live_config.arm_ik_enabled);
    pointer_state={};
  }
  inject_deferred_mouse=true;
  ModMenuRenderTexture(device.Get(),image.Get());
  CHECK(deferred_input_events>0);
  CHECK(!ModMenuVisible() || calibration);
  ModMenuToggle();
  observe_deferred_mouse=true;
  ModMenuRenderTexture(device.Get(),image.Get());
  CHECK(deferred_mouse_observed && !deferred_mouse_seen);
  pointer_button_test_active=true;
  pointer_state={true,true,false,980.0f,660.0f};
  ModMenuRenderTexture(device.Get(),image.Get());
  const float test_x=(pointer_button_rect_min.x+pointer_button_rect_max.x)*.5f;
  const float test_y=(pointer_button_rect_min.y+pointer_button_rect_max.y)*.5f;
  pointer_state={true,true,false,test_x,test_y};
  ModMenuRenderTexture(device.Get(),image.Get());
  pointer_state.select=true;
  ModMenuRenderTexture(device.Get(),image.Get());
  pointer_state.select=false;
  ModMenuRenderTexture(device.Get(),image.Get());
  CHECK(pointer_button_hovered && pointer_button_clicked);
  pointer_button_test_active=false;
    pointer_state={true,true,false,100.0f,65.0f};
    probe_pointer_click=true;
    ModMenuRenderTexture(device.Get(),image.Get());
    pointer_state.select=true;
    probe_pointer_click=true;
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(pointer_mouse_down);
    CHECK(std::fabs(pointer_mouse_x-100.0f)<1.0f && std::fabs(pointer_mouse_y-65.0f)<1.0f);
    pointer_state={true,false,false,0.0f,0.0f};
    probe_pointer_click=true;
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(!pointer_mouse_down && pointer_mouse_x < -1.0e20f && pointer_mouse_y < -1.0e20f);
    // A/focus navigation remains available with spatial presentation even if
    // aim is lost. It must never become a phantom mouse click.
    pointer_state.navigation_select=true;
    probe_pointer_click=true;ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(navigation_select_down && !pointer_mouse_down);
    pointer_state.navigation_select=false;
    probe_pointer_click=true;ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(!navigation_select_down && !pointer_mouse_down);
    pointer_state={true,true,false,100.f,65.f};
    ModMenuControllerInput(0,1,false,false);
    probe_pointer_click=true;ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(pointer_mouse_x < -1.0e20f); // a hovering ray cannot steal stick focus
    ModMenuControllerInput(0,0,false,false);
    pointer_state={};
  ComPtr<ID3D11RenderTargetView> restored;context->OMGetRenderTargets(1,&restored,nullptr);CHECK(restored.Get()==target.Get());
  if(argc>1) {
    auto read=desc;read.BindFlags=0;read.Usage=D3D11_USAGE_STAGING;read.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;CHECK(SUCCEEDED(device->CreateTexture2D(&read,nullptr,&staging)));
    context->CopyResource(staging.Get(),image.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
    CHECK(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)));
    FILE* file=nullptr;CHECK(fopen_s(&file,argv[1],"wb")==0 && file);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+width*height*4;
    BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=width;info.biHeight=-static_cast<LONG>(height);info.biPlanes=1;info.biBitCount=32;
    fwrite(&header,sizeof(header),1,file);fwrite(&info,sizeof(info),1,file);
    std::vector<unsigned char> row(width*4);
    for(unsigned y=0;y<height;++y) {
      auto* source=static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch;
      for(unsigned x=0;x<width;++x){row[x*4]=source[x*4+2];row[x*4+1]=source[x*4+1];row[x*4+2]=source[x*4];row[x*4+3]=255;}
      fwrite(row.data(),row.size(),1,file);
    }
    fclose(file);context->Unmap(staging.Get(),0);
  }
  ModMenuClose();CHECK(!ModMenuVisible() || calibration);ModMenuShutdown();DestroyWindow(window);
  std::puts("native menu render/state restoration: PASS");return 0;
}
