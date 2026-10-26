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
bool body_filter_available=false;
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
bool capture_menu_text=false,pending_footer_capture=false;
extern bool probe_disabled_item, item_was_disabled; // defined in mod_menu.cpp
InputBindingSignature InputLiveBindingSignature() {
  // No session attaches in this fixture: the frozen attached set is the
  // Config defaults (all profiles on), so a live-only Apply stays absent
  // from the restart line and a binding Apply shows it.
  return {};
}
BodyVisibilityStatus BodyVisibilityGetStatus() {
  if (capture_menu_text) {
    // Label captures log from here (inside the panel child); footer
    // captures only set the pending flag, which CalibrationGetStatus turns
    // into a whole-window log on the next frame.
    if (pending_footer_capture) pending_footer_capture=false;
    else ImGui::LogToFile(-1,"menu-label-capture.txt");
    capture_menu_text=false;
  }
  BodyVisibilityStatus result{};result.installed=body_filter_available;return result;
}
// The panel asks for a fresh native parts command when a visibility row is
// ticked. The status stub reports the filter as unavailable, so the row stays
// disabled here and the count stays zero; test_body_visibility owns the path.
int visibility_refresh_requests = 0;
bool BodyVisibilityRequestRefresh() { ++visibility_refresh_requests; return true; }
MotionBlurStatus MotionBlurGetStatus() {return {};}
CalibrationStatus CalibrationGetStatus() {
  if (ImGui::GetCurrentContext()) {
    menu_context=ImGui::GetCurrentContext();
    if (pending_footer_capture) {
      // Runs in ModMenuRenderTexture before DrawMenu: starting the log here
      // binds it to no window, so LogFinish fires at the menu window's End()
      // and the whole window (footer included) is captured.
      pending_footer_capture=false;
      ImGui::LogToFile(-1,"menu-label-capture.txt");
    }
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
    // (probe_disabled_item is consumed inside SettingsPanel; nothing to do here.)
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
int binding_reloads=0;
void InputRequestBindingReload() {++binding_reloads;}
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
  // Assert the shipped palette, readable fallback body font, and persistent
  // keyboard/gamepad navigation rather than a duplicate theme fixture.
  CHECK(menu_context);ImGui::SetCurrentContext(menu_context);
  const auto& theme=ImGui::GetStyle();
  const auto ash=theme.Colors[ImGuiCol_WindowBg];
  const auto parchment=theme.Colors[ImGuiCol_Text];
  const auto ochre=theme.Colors[ImGuiCol_CheckMark];
  CHECK(ash.x<.08f && ash.y<.08f && ash.z<.08f);
  CHECK(parchment.x>.8f && parchment.x>parchment.y && parchment.y>parchment.z);
  CHECK(ochre.x>ochre.y && ochre.y>ochre.z);
  CHECK(theme.Colors[ImGuiCol_PopupBg].x<.12f);
  CHECK(theme.Colors[ImGuiCol_TitleBgActive].z<.12f);
  CHECK(theme.WindowBorderSize==1 && theme.FrameBorderSize==1);
  CHECK(ImGui::GetIO().FontDefault);
  CHECK(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NavEnableKeyboard);
  CHECK(ImGui::GetIO().ConfigFlags&ImGuiConfigFlags_NavEnableGamepad);
  if(!calibration) {
    std::remove("menu-label-capture.txt");
    capture_menu_text=true;
    ModMenuRenderTexture(device.Get(),image.Get());
    FILE* captured=nullptr;
    CHECK(fopen_s(&captured,"menu-label-capture.txt","rb")==0 && captured);
    char menu_text[8192]{};
    const auto captured_size=fread(menu_text,1,sizeof(menu_text)-1,captured);
    fclose(captured);
    // Drive the shipped controls through ImGui's navigation activation, not a
    // duplicate settings-state helper: edit -> Apply -> Save identical values.
    // The grouping asserts below need the panel ID seed, so resolve it once
    // here and re-resolve it after the Combat visit (the panel child
    // rebinds across visits).
    CHECK(menu_context);ImGui::SetCurrentContext(menu_context);
    ImGuiWindow* panel_early=nullptr;
    for(auto* candidate:GImGui->Windows)
      if(std::string(candidate->Name).find("/Panel_")!=std::string::npos) panel_early=candidate;
    CHECK(panel_early);
    ImGuiWindow* panel=panel_early;
    CHECK(std::string(menu_text,captured_size).find("Hide face, headgear and hair")!=std::string::npos);
    // Everyday/advanced separation: one labeled always-visible group inside
    // Settings and one inside Combat. Same panels, same IDs/labels/behavior —
    // grouping only. Asserted on the Settings route through the same one-shot
    // label hook as the existing default-renders assertions (fired in
    // BodyVisibilityGetStatus + the CalibrationGetStatus menu-context probe):
    // the grouping label renders, the arm rows read under it, everyday rows
    // above and binding rows below stay visible, and enabling arm swing
    // reveals its threshold rows under the same grouping.
    {
      std::remove("menu-label-capture.txt");
      capture_menu_text=true;
      ModMenuRenderTexture(device.Get(),image.Get());
      FILE* ac=nullptr;
      CHECK(fopen_s(&ac,"menu-label-capture.txt","rb")==0 && ac);
      char atext[16384]{};
      const auto asize=fread(atext,1,sizeof(atext)-1,ac);
      fclose(ac);
      std::remove("menu-label-capture.txt");
      const std::string astr(atext,asize);
      CHECK(astr.find("Advanced tuning")!=std::string::npos);
      CHECK(astr.find("Track native arms")!=std::string::npos);
      // The whole Settings panel still renders: an everyday row above the
      // group and the binding rows below it stay visible.
      CHECK(astr.find("Snap turning")!=std::string::npos);
      CHECK(astr.find("WMR motion controller")!=std::string::npos);
      // Default-open shows the advanced controls: enabling the arm-swing
      // toggle reveals its threshold sliders under the same grouping.
      ImGui::ActivateItemByID(ImHashStr("Swinging the arms asks the game to run",0,panel->ID));
      ModMenuRenderTexture(device.Get(),image.Get());
      std::remove("menu-label-capture.txt");
      capture_menu_text=true;
      ModMenuRenderTexture(device.Get(),image.Get());
      FILE* sw=nullptr;
      CHECK(fopen_s(&sw,"menu-label-capture.txt","rb")==0 && sw);
      char stext[16384]{};
      const auto ssize=fread(stext,1,sizeof(stext)-1,sw);
      fclose(sw);
      std::remove("menu-label-capture.txt");
      const std::string sstr(stext,ssize);
      CHECK(sstr.find("Advanced tuning")!=std::string::npos);
      CHECK(sstr.find("Stride hand speed (m/s)")!=std::string::npos);
      // Restore the toggle so the draft matches the applied config again
      // before the refresh/apply coverage below (which needs applies==1 on
      // its first Apply).
      ImGui::ActivateItemByID(ImHashStr("Swinging the arms asks the game to run",0,panel->ID));
      ModMenuRenderTexture(device.Get(),image.Get());
    }
    // Same grouping treatment inside CombatPanel, asserted structurally (no
    // new hook is added above the footer capture point there): navigation
    // activation still reaches the advanced per-hand rows under the group
    // with no ID/label/behavior change. The Combat panel is reached via the
    // shipped next-panel edges (0 -> 3) without touching panel IDs/ordering.
    {
      for(int i=0;i<3;++i) {
        ModMenuControllerInput(0,0,false,false,false,true);
        ModMenuRenderTexture(device.Get(),image.Get());
        ModMenuControllerInput(0,0,false,false,false,false);
        ModMenuRenderTexture(device.Get(),image.Get());
      }
      ImGui::ActivateItemByID(ImHashStr("Raised shield hand guards, flick parries (preview)",0,panel->ID));
      ModMenuRenderTexture(device.Get(),image.Get());
      // Return to Settings via the shipped prev-panel edges (3 -> 0), then
      // re-resolve the seed: the panel child rebinds across Combat visits.
      for(int i=0;i<3;++i) {
        ModMenuControllerInput(0,0,false,false,true,false);
        ModMenuRenderTexture(device.Get(),image.Get());
        ModMenuControllerInput(0,0,false,false,false,false);
        ModMenuRenderTexture(device.Get(),image.Get());
      }
      for(auto* candidate:GImGui->Windows)
        if(std::string(candidate->Name).find("/Panel_")!=std::string::npos) panel=candidate;
      CHECK(panel);
      // Back on the Settings route: the draft is unchanged by the Combat
      // visit (activation only, no Apply), and the grouping still renders.
      std::remove("menu-label-capture.txt");
      capture_menu_text=true;
      ModMenuRenderTexture(device.Get(),image.Get());
      FILE* bk=nullptr;
      CHECK(fopen_s(&bk,"menu-label-capture.txt","rb")==0 && bk);
      char btext[8192]{};
      const auto bsize=fread(btext,1,sizeof(btext)-1,bk);
      fclose(bk);
      std::remove("menu-label-capture.txt");
      CHECK(std::string(btext,bsize).find("Advanced tuning")!=std::string::npos);
    }
    // Actual shipped controls apply AND save on the edit; no footer action.
    const auto saves_before=saves;
    ImGui::ActivateItemByID(ImHashStr("Track native arms",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(live_config.arm_ik_enabled && saved_config.arm_ik_enabled);
    CHECK(saves==saves_before+1 && applies==0);
    body_filter_available=true;
    ModMenuRenderTexture(device.Get(),image.Get());
    for(bool hidden:{true,false}) {
      ImGui::ActivateItemByID(ImHashStr("Hide torso and legs (arms and weapon stay)",0,panel->ID));
      ModMenuRenderTexture(device.Get(),image.Get());
      CHECK(live_config.hide_torso_and_legs==hidden && saved_config.hide_torso_and_legs==hidden);
    }
    CHECK(visibility_refresh_requests==0); // native model-update owner performs the refresh
    ImGui::ActivateItemByID(ImHashStr("Refresh profile filter now",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(binding_reloads==1 && saves==saves_before+3);
    const auto read_footer=[&]() -> std::string {
      std::remove("menu-label-capture.txt");
      // Frame 1 sets pending_footer_capture in BodyVisibilityGetStatus;
      // CalibrationGetStatus on frame 2 starts the log before DrawMenu, so
      // it spans the whole "DSVR controls" window including the footer.
      pending_footer_capture=true;
      capture_menu_text=true;
      ModMenuRenderTexture(device.Get(),image.Get());
      ModMenuRenderTexture(device.Get(),image.Get());
      FILE* f=nullptr;
      if(fopen_s(&f,"menu-label-capture.txt","rb")!=0 || !f) return "__missing__";
      char text[65536]{};const auto n=fread(text,1,sizeof(text)-1,f);fclose(f);
      std::remove("menu-label-capture.txt");return std::string(text,n);
    };
    const auto clean_footer=read_footer();
    CHECK(clean_footer.find("Restart needed")==std::string::npos);
    CHECK(clean_footer.find("Changes apply immediately and save automatically.")!=std::string::npos);
    CHECK(clean_footer.find("Save settings")==std::string::npos);
    CHECK(clean_footer.find("Revert edits")==std::string::npos);
    ImGui::ActivateItemByID(ImHashStr("Hide alternate player model",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(live_config.hide_second_model==saved_config.hide_second_model);
    CHECK(read_footer().find("Restart needed")==std::string::npos);
    probe_disabled_item=true;ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(!item_was_disabled);
    ImGui::ActivateItemByID(ImHashStr("Smooth turning",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    probe_disabled_item=true;ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(item_was_disabled && !live_config.smooth_turn && !saved_config.smooth_turn);
    ImGui::ActivateItemByID(ImHashStr("Smooth turning",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    ImGui::ActivateItemByID(ImHashStr("Quest 3 / Touch",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(!live_config.bind_profile_quest3 && !saved_config.bind_profile_quest3);
    CHECK(read_footer().find("Restart needed: bindings")!=std::string::npos);
    ImGui::ActivateItemByID(ImHashStr("Quest 3 / Touch",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(read_footer().find("Restart needed")==std::string::npos);
    const auto step_panel=[&](bool forward) {
      ModMenuControllerInput(0,0,false,false,!forward,forward);
      ModMenuRenderTexture(device.Get(),image.Get());
      ModMenuControllerInput(0,0,false,false,false,false);
      ModMenuRenderTexture(device.Get(),image.Get());
      for(auto* candidate:GImGui->Windows)
        if(std::string(candidate->Name).find("/Panel_")!=std::string::npos)panel=candidate;
    };
    step_panel(true);step_panel(true);
    ImGui::ActivateItemByID(ImHashStr("Use ReShade D3D11 host (restart required)",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(read_footer().find("Restart needed: ReShade host")!=std::string::npos);
    ImGui::ActivateItemByID(ImHashStr("Performance (70%)",0,panel->ID));
    ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(live_config.render_scale_percent==70 && saved_config.render_scale_percent==70);
    CHECK(read_footer().find("Restart needed: ReShade host, eye resolution")!=std::string::npos);
    step_panel(false);step_panel(false);
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
    ModMenuControllerInput(0,0,true,false);
    probe_pointer_click=true;ModMenuRenderTexture(device.Get(),image.Get());
    CHECK(navigation_select_down && !pointer_mouse_down);
    ModMenuControllerInput(0,0,false,false);
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
