#include "mod_menu.h"
#include "menu_input_events.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cfloat>
#include <mutex>
#include <string>
#include <vector>
#include "../common/config.h"
#include "../common/logging.h"
#include "calibration.h"
#include "body_visibility.h"
#include "core.h"
#include "cpu_frame_timer.h"
#include "diagnostics.h"
#include "xr.h"
#include "input.h"
#include "weapon_grip.h"
#include "arm_ik.h"
#include "combat_native.h"
#include "native_ui.h"
#include "motion_blur.h"
#include "../common/ui_palette.h"
namespace dsvr {
// Test seam (declared in tests/test_mod_menu.cpp): the fixture arms
// probe_disabled_item, SettingsPanel runs one probe item inside the gated
// span, and records whether ImGuiItemFlags_Disabled covered it here.
// Production never arms it, so the probe item never renders there.
bool probe_disabled_item = false, item_was_disabled = false;
namespace {
using Microsoft::WRL::ComPtr;
constexpr float Channel(unsigned char value) { return value / 255.0f; }
constexpr ImVec4 PaletteColor(palette::Rgb8 color, float alpha = 1.0f) {
  return {Channel(color.r), Channel(color.g), Channel(color.b), alpha};
}
constexpr ImVec4 kBone = PaletteColor(palette::bone);
constexpr ImVec4 kMoss = PaletteColor(palette::moss);
constexpr ImVec4 kEmber = PaletteColor(palette::ember);
std::atomic<bool> g_open{false};
std::atomic<bool> g_reset_imgui_input{false};
std::mutex g_input_mutex, g_panel_mutex;
struct ControllerInput {
  float x = 0, y = 0;
  bool select = false, back = false;
  bool prev_panel = false, next_panel = false;
};
ControllerInput g_controller;
// Panel stepping is edge-triggered: one press moves exactly one panel, and the
// edge state is dropped whenever the menu takes or loses input.
bool g_panel_step_held[2] = {false, false};
MenuWindowEventQueue g_events;
void CancelMenuInput() {
  g_reset_imgui_input.store(true);
  std::lock_guard<std::mutex> lock(g_input_mutex);
  g_controller = {};
  g_panel_step_held[0] = g_panel_step_held[1] = false;
  g_events.Clear();
}
struct Panel {
  std::string id, name;
  ModMenuPanel draw = nullptr;
};
std::vector<Panel> g_panels;
// The panel list is six built-in screens plus whatever a subsystem registered.
int PanelCount() {
  std::lock_guard<std::mutex> lock(g_panel_mutex);
  return 6 + static_cast<int>(g_panels.size());
}
ImGuiContext *g_gui = nullptr;
ComPtr<ID3D11Device> g_device;
ComPtr<ID3D11DeviceContext> g_context;
HWND g_window = nullptr;
WNDPROC g_original = nullptr;
DWORD g_thread = 0;
ULONGLONG g_last_frame = 0;
// No draft buffer: every row edits the live config in place (g_edit is
// re-seeded from the applied config each frame) and the footer pushes any
// difference immediately with save. Rows the running session cannot honor
// are labeled "(restart required)"; closing with those pending shows a
// reminder popup, and restarting is left to the operator.
Config g_edit;
// Restart-reminder state: modal open, last acknowledged pending set, and a
// close requested off the render thread (toggle/B-button path).
bool g_restart_modal = false;
std::string g_restart_ack;
bool g_close_requested = false;
int g_panel = 0;
ImFont *g_title_font = nullptr;
bool g_pointer_mode = true;
ImVec2 g_navigation_pointer{};
MenuPointerSmoother g_pointer_smooth;
LRESULT CALLBACK MenuWindowProc(HWND window, UINT message, WPARAM key, LPARAM data) {
  const bool capture = ModMenuCapturesInput();
  if (message == WM_KILLFOCUS) {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_events.Clear();
    g_reset_imgui_input.store(true);
  } else if (capture && (message == WM_CHAR || message == WM_MOUSEWHEEL ||
                         message == WM_MOUSEHWHEEL || IsMenuInputEdge(message))) {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    g_events.Push({message, key, data});
  }
  if (capture && ((message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
                  (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST)))
    return 0;
  return g_original ? CallWindowProcW(g_original, window, message, key, data)
                    : DefWindowProcW(window, message, key, data);
}
BOOL CALLBACK FindWindow(HWND window, LPARAM out) {
  DWORD process = 0;
  GetWindowThreadProcessId(window, &process);
  if (process == GetCurrentProcessId() && GetWindow(window, GW_OWNER) == nullptr) {
    // GetWindowText sends WM_GETTEXT within this process. Calling it from
    // the render thread can block on the game' s window thread ; no title is
    // needed because the temporary DSVR swapchain window is already destroyed .
    auto &found = *reinterpret_cast<HWND *>(out);
    if (IsWindowVisible(window) && !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)) {
      found = window;
      return FALSE;
    }
    if (!found)
      found = window;
    // hidden - window WARP fixture / startup fallback
  }
  return TRUE;
}
bool Initialize(ID3D11Device *device) {
  if (g_gui)
    return g_device.Get() == device && g_thread == GetCurrentThreadId();
  HWND window = nullptr;
  EnumWindows(FindWindow, reinterpret_cast<LPARAM>(&window));
  if (!window)
    return false;
  g_gui = ImGui::CreateContext();
  ImGui::SetCurrentContext(g_gui);
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
  io.BackendPlatformName = "DSVR";
  wchar_t windows[MAX_PATH]{};
  GetWindowsDirectoryW(windows, MAX_PATH);
  const std::wstring font_dir = std::wstring(windows) + L"\\Fonts\\";
  const auto body_font = Utf8FromWide(font_dir + L"segoeui.ttf");
  ImFont *body = nullptr;
  if (GetFileAttributesW((font_dir + L"segoeui.ttf").c_str()) != INVALID_FILE_ATTRIBUTES)
    body = io.Fonts->AddFontFromFileTTF(body_font.c_str(), 20);
  if (!body)
    body = io.Fonts->AddFontDefault();
  io.FontDefault = body;
  // Titles use a serif display face ; body text stays Segoe UI because it is the
  // one that survives headset resolution at 20 px . First installed face wins .
  for (const wchar_t *file : {L"georgiab.ttf", L"GOUDOS.TTF", L"BOOKOS.TTF", L"timesbd.ttf"}) {
    const auto path = Utf8FromWide(font_dir + file);
    if (GetFileAttributesW((font_dir + file).c_str()) == INVALID_FILE_ATTRIBUTES)
      continue;
    g_title_font = io.Fonts->AddFontFromFileTTF(path.c_str(), 30);
    if (g_title_font)
      break;
  }
  if (!g_title_font &&
      GetFileAttributesW((font_dir + L"segoeui.ttf").c_str()) != INVALID_FILE_ATTRIBUTES)
    g_title_font = io.Fonts->AddFontFromFileTTF(body_font.c_str(), 30);
  if (io.Fonts->Fonts.empty())
    io.Fonts->AddFontDefault();
  auto &style = ImGui::GetStyle();
  style.WindowPadding = {24, 22};
  style.FramePadding = {12, 8};
  style.ItemSpacing = {12, 12};
  style.ItemInnerSpacing = {8, 6};
  // Charcoal iron, worn bone, moss and ember carry the same palette as Setup.
  style.WindowRounding = 2;
  style.FrameRounding = 2;
  style.GrabRounding = 2;
  style.ScrollbarRounding = 2;
  style.WindowBorderSize = 1;
  style.FrameBorderSize = 1;
  style.ChildBorderSize = 0;
  style.ScrollbarSize = 16;
  style.Colors[ImGuiCol_WindowBg] = PaletteColor(palette::background, .98f);
  style.Colors[ImGuiCol_ChildBg] = {0, 0, 0, 0};
  style.Colors[ImGuiCol_PopupBg] = PaletteColor(palette::surface);
  style.Colors[ImGuiCol_TitleBg] = PaletteColor(palette::surface);
  style.Colors[ImGuiCol_TitleBgActive] = PaletteColor(palette::raised);
  style.Colors[ImGuiCol_TitleBgCollapsed] = PaletteColor(palette::surface);
  style.Colors[ImGuiCol_Border] = PaletteColor(palette::border, .85f);
  style.Colors[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
  style.Colors[ImGuiCol_Text] = PaletteColor(palette::text);
  style.Colors[ImGuiCol_TextDisabled] = PaletteColor(palette::muted);
  style.Colors[ImGuiCol_TextSelectedBg] = PaletteColor(palette::ember, .48f);
  style.Colors[ImGuiCol_FrameBg] = PaletteColor(palette::surface);
  style.Colors[ImGuiCol_FrameBgHovered] = PaletteColor(palette::raised);
  style.Colors[ImGuiCol_FrameBgActive] = PaletteColor(palette::ember_dark);
  style.Colors[ImGuiCol_Button] = PaletteColor(palette::surface);
  style.Colors[ImGuiCol_ButtonHovered] = PaletteColor(palette::ember_dark);
  style.Colors[ImGuiCol_ButtonActive] = PaletteColor(palette::ember, .92f);
  style.Colors[ImGuiCol_Header] = PaletteColor(palette::raised);
  style.Colors[ImGuiCol_HeaderHovered] = PaletteColor(palette::ember_dark);
  style.Colors[ImGuiCol_HeaderActive] = PaletteColor(palette::ember, .92f);
  style.Colors[ImGuiCol_Separator] = PaletteColor(palette::border_bright, .85f);
  style.Colors[ImGuiCol_CheckMark] = kEmber;
  style.Colors[ImGuiCol_SliderGrab] = kMoss;
  style.Colors[ImGuiCol_SliderGrabActive] = kEmber;
  style.Colors[ImGuiCol_PlotHistogram] = kMoss;
  style.Colors[ImGuiCol_ScrollbarBg] = PaletteColor(palette::background);
  style.Colors[ImGuiCol_ScrollbarGrab] = PaletteColor(palette::border_bright);
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = PaletteColor(palette::moss_dark);
  style.Colors[ImGuiCol_ScrollbarGrabActive] = kEmber;
  style.Colors[ImGuiCol_NavCursor] = kEmber;
  style.Colors[ImGuiCol_NavHighlight] = kEmber;
  g_device = device;
  device->GetImmediateContext(&g_context);
  if (!ImGui_ImplDX11_Init(device, g_context.Get())) {
    ImGui::DestroyContext(g_gui);
    g_gui = nullptr;
    g_device.Reset();
    g_context.Reset();
    return false;
  }
  g_window = window;
  g_thread = GetCurrentThreadId();
  // The game must own the keyboard the moment it opens: a window that starts
  // behind SteamVR's overlay or the Setup window receives no XInput focus and
  // the player sees a live scene with dead controls. Windows only lets a
  // process take foreground if it already owns it or was granted it, so the
  // grant is requested by the launcher (AllowSetForegroundWindow) and this
  // call is the attempt, not a guarantee. Polling continues either way: the
  // XInput bridge merges motion input regardless of foreground state.
  if (GetForegroundWindow() != window) {
    const DWORD fg_thread =
        GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
    const DWORD own_thread = GetCurrentThreadId();
    const bool attached = fg_thread && fg_thread != own_thread &&
                          AttachThreadInput(own_thread, fg_thread, TRUE);
    const bool raised = SetForegroundWindow(window) != FALSE;
    BringWindowToTop(window);
    if (attached)
      AttachThreadInput(own_thread, fg_thread, FALSE);
    DSVR_LOG("Focus: startup foreground request raised=%d window=%p "
             "(background input stays live either way)",
             (int)raised, (void *)window);
  }
  g_original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
  SetLastError(0);
  const auto previous =
      SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MenuWindowProc));
  if (previous)
    g_original = reinterpret_cast<WNDPROC>(previous);
  DSVR_LOG("Menu: Home opens DSVR controls; controller shortcut is left Menu + right grip");
  return true;
}
void FeedInput(unsigned width, unsigned height, const MenuPointerState& pointer) {
  auto &io = ImGui::GetIO();
  ControllerInput controller;
  MenuWindowEventQueue events;
  {
    std::lock_guard<std::mutex> lock(g_input_mutex);
    controller = g_controller;
    events.Swap(g_events);
  }
  // Panel switching on the controller's face buttons. The spatial menu has no
  // keyboard, so ImGui's Tab channel never fires in the headset and the panel
  // list is otherwise reachable only by aiming the ray at it. X steps back, Y
  // steps forward, one screen per press, wrapping in both directions.
  {
    const int panel_count = PanelCount();
    const auto step = [&](bool held, bool &was_held, int delta) {
        const bool edge = held && !was_held;
        was_held = held;
      if (!edge || panel_count <= 1) return;
      g_panel = (g_panel + delta) % panel_count;
      if (g_panel < 0) g_panel += panel_count;
      };
    step(controller.prev_panel, g_panel_step_held[0], -1);
    step(controller.next_panel, g_panel_step_held[1], +1);
  }
  if (g_reset_imgui_input.exchange(false)) {
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
    g_pointer_mode = true;
    g_pointer_smooth.Reset();
  }
  // Smooth before anything reads the position : the mode hysteresis below and the
  // ImGui cursor have to agree on one point , or a still hand flips pointer mode
  // on and off by itself . A lost ray re - primes instead of interpolating across
  // the gap , so the cursor never slides to wherever the last ray left it .
  if (pointer.valid)
    g_pointer_smooth.Approach(pointer.x, pointer.y, GetTickCount64());
  else
    g_pointer_smooth.Reset();
  const bool smoothed = g_pointer_smooth.Primed();
  const float px = smoothed ? g_pointer_smooth.X() : pointer.x;
  const float py = smoothed ? g_pointer_smooth.Y() : pointer.y;
  const bool navigating =
      std::fabs(controller.x) > .6f || std::fabs(controller.y) > .6f || controller.select || pointer.navigation_select;
  if (navigating) {
    if (g_pointer_mode)
      g_navigation_pointer = {px, py};
    g_pointer_mode = false;
  } else if (pointer.valid && (pointer.select || std::hypot(px - g_navigation_pointer.x,
                                                            py - g_navigation_pointer.y) > 16.f)) {
    g_pointer_mode = true;
  }
  const bool pointing = pointer.spatial_menu && pointer.valid && g_pointer_mode;
  const auto now = GetTickCount64();
  io.DeltaTime = g_last_frame
                     ? std::clamp(static_cast<float>(now - g_last_frame) / 1000.f, .001f, .1f)
                     : 1.f / 90;
  g_last_frame = now;
  io.DisplaySize = {static_cast<float>(width), static_cast<float>(height)};
  const bool focused = GetForegroundWindow() == g_window;
  io.AddFocusEvent(focused || pointer.spatial_menu);
  // Outside pointer mode the desktop cursor is a phantom input . A live mouse
  // position makes ImGui treat the mouse as the active navigation source , so
  // the d - pad never drives navigation . Report "no mouse" instead .
  if (pointing)
    io.AddMousePosEvent(px, py);
  else
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
  for (int i = 0; i < 3; ++i)
    io.AddMouseButtonEvent(i, pointing && i == 0 && MenuPointerMouseDown(pointer));
  struct Key {
    int native;
    ImGuiKey gui;
  };
  constexpr Key keys[] = {{VK_TAB, ImGuiKey_Tab},
                          {VK_LEFT, ImGuiKey_LeftArrow},
                          {VK_RIGHT, ImGuiKey_RightArrow},
                          {VK_UP, ImGuiKey_UpArrow},
                          {VK_DOWN, ImGuiKey_DownArrow},
                          {VK_RETURN, ImGuiKey_Enter},
                          {VK_ESCAPE, ImGuiKey_Escape},
                          {VK_SPACE, ImGuiKey_Space},
                          {VK_BACK, ImGuiKey_Backspace},
                          {VK_DELETE, ImGuiKey_Delete},
                          {VK_HOME, ImGuiKey_Home},
                          {VK_END, ImGuiKey_End},
                          {'A', ImGuiKey_A},
                          {'C', ImGuiKey_C},
                          {'V', ImGuiKey_V},
                          {'X', ImGuiKey_X},
                          {'Y', ImGuiKey_Y},
                          {'Z', ImGuiKey_Z}};
  for (auto key : keys)
    io.AddKeyEvent(key.gui, focused && (GetAsyncKeyState(key.native) & 0x8000));
  io.AddKeyEvent(ImGuiMod_Ctrl, focused && (GetAsyncKeyState(VK_CONTROL) & 0x8000));
  io.AddKeyEvent(ImGuiMod_Shift, focused && (GetAsyncKeyState(VK_SHIFT) & 0x8000));
  MenuWindowEvent event{};
  while (events.Pop(event)) {
    const bool key_down = event.message == WM_KEYDOWN || event.message == WM_SYSKEYDOWN;
    const bool key_up = event.message == WM_KEYUP || event.message == WM_SYSKEYUP;
    if (key_down || key_up) {
      for (auto key : keys)
        if (key.native == static_cast<int>(event.key)) {
          io.AddKeyEvent(key.gui, key_down);
          break;
        }
      if (event.key == VK_CONTROL || event.key == VK_LCONTROL || event.key == VK_RCONTROL)
        io.AddKeyEvent(ImGuiMod_Ctrl, key_down);
      if (event.key == VK_SHIFT || event.key == VK_LSHIFT || event.key == VK_RSHIFT)
        io.AddKeyEvent(ImGuiMod_Shift, key_down);
    } else if (pointer.spatial_menu &&
               (event.message == WM_LBUTTONDOWN || event.message == WM_LBUTTONDBLCLK ||
                event.message == WM_LBUTTONUP || event.message == WM_RBUTTONDOWN ||
                event.message == WM_RBUTTONDBLCLK || event.message == WM_RBUTTONUP ||
                event.message == WM_MBUTTONDOWN || event.message == WM_MBUTTONDBLCLK ||
                event.message == WM_MBUTTONUP || event.message == WM_XBUTTONDOWN ||
                event.message == WM_XBUTTONDBLCLK || event.message == WM_XBUTTONUP)) {
      // In spatial mode the tracked ray is the sole mouse - button source .
    } else if (event.message == WM_LBUTTONDOWN || event.message == WM_LBUTTONDBLCLK) {
      io.AddMouseButtonEvent(0, true);
    } else if (event.message == WM_LBUTTONUP) {
      io.AddMouseButtonEvent(0, false);
    } else if (event.message == WM_RBUTTONDOWN || event.message == WM_RBUTTONDBLCLK) {
      io.AddMouseButtonEvent(1, true);
    } else if (event.message == WM_RBUTTONUP) {
      io.AddMouseButtonEvent(1, false);
    } else if (event.message == WM_MBUTTONDOWN || event.message == WM_MBUTTONDBLCLK) {
      io.AddMouseButtonEvent(2, true);
    } else if (event.message == WM_MBUTTONUP) {
      io.AddMouseButtonEvent(2, false);
    } else if (event.message == WM_XBUTTONDOWN || event.message == WM_XBUTTONDBLCLK) {
      io.AddMouseButtonEvent(GET_XBUTTON_WPARAM(event.key) == XBUTTON1 ? 3 : 4, true);
    } else if (event.message == WM_XBUTTONUP) {
      io.AddMouseButtonEvent(GET_XBUTTON_WPARAM(event.key) == XBUTTON1 ? 3 : 4, false);
    } else if (event.message == WM_CHAR) {
      io.AddInputCharacterUTF16(static_cast<ImWchar16>(event.key));
    } else if (event.message == WM_MOUSEWHEEL) {
      io.AddMouseWheelEvent(0, static_cast<float>(GET_WHEEL_DELTA_WPARAM(event.key)) / WHEEL_DELTA);
    } else if (event.message == WM_MOUSEHWHEEL) {
      io.AddMouseWheelEvent(static_cast<float>(GET_WHEEL_DELTA_WPARAM(event.key)) / WHEEL_DELTA, 0);
    }
  }
  io.AddKeyEvent(ImGuiKey_GamepadFaceDown, MenuGamepadSelect(pointer, controller.select));
  io.AddKeyEvent(ImGuiKey_GamepadFaceRight, controller.back);
  io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, controller.x < -.6f);
  io.AddKeyEvent(ImGuiKey_GamepadDpadRight, controller.x > .6f);
  io.AddKeyEvent(ImGuiKey_GamepadDpadUp, controller.y > .6f);
  io.AddKeyEvent(ImGuiKey_GamepadDpadDown, controller.y < -.6f);
  // The tracked ray is a hit-test only. Anything ImGui paints into the panel
  // texture is baked into the game backbuffer, and the theater rect or the
  // theater panel then magnifies it across the view (live report 31 Oct 2026:
  // the pointer ray "painting itself all over the screen").
  io.MouseDrawCursor = false;
}


void Title(const char *text) {
  if (g_title_font)
    ImGui::PushFont(g_title_font);
  ImGui::PushStyleColor(ImGuiCol_Text, kBone);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  if (g_title_font)
    ImGui::PopFont();
}
void CalibrationScreen(const CalibrationStatus &status) {
  const auto size = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, {0, 0, 0, 0});
  ImGui::PushStyleColor(ImGuiCol_FrameBg, {.035f, .055f, .041f, .78f});
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {.075f, .10f, .065f, .88f});
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, {.10f, .13f, .08f, .94f});
  ImGui::PushStyleColor(ImGuiCol_TextDisabled, kBone);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {10, 8});
  ImGui::Begin("DSVR calibration", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBackground);
  ImGui::SetWindowFontScale(1.7f);
  const float poster_w = std::max(1.f, std::min(1680.f, size.x - 72.f));
  const float poster_h = std::max(1.f, std::min(980.f, size.y - 64.f));
  const ImVec2 poster_min{(size.x - poster_w) * .5f,
                          (size.y - poster_h) * .5f};
  const ImVec2 poster_max{poster_min.x + poster_w, poster_min.y + poster_h};
  auto *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(poster_min, poster_max, IM_COL32(7, 13, 9, 104), 4.f);
  draw->AddRect(poster_min, poster_max, IM_COL32(145, 158, 113, 190), 4.f, 0, 1.8f);
  draw->AddRectFilled({poster_min.x + 1, poster_min.y + 1},
                      {poster_max.x - 1, poster_min.y + 3},
                      IM_COL32(190, 159, 94, 235));
  const float content = poster_w - 68.f;
  ImGui::SetCursorPos({poster_min.x + 34.f, poster_min.y + 22.f});
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content);
  ImGui::TextDisabled("DSVR  /  BODY CALIBRATION");
  Title("Set your natural reach");
  ImGui::TextWrapped("Choose how you play. Keep both controllers visible in any relaxed pose; hold both triggers for one second to save.");
  auto cfg = DiagnosticsCurrentConfig();
  int play_position = cfg.play_style == "seated" ? 1 : cfg.play_style == "roomscale" ? 2 : 0;
  ImGui::SetNextItemWidth(std::min(360.f, content));
  if (ImGui::Combo("How you play", &play_position, "Standing\0Seated\0Walking around\0")) {
    cfg.play_style = play_position == 1 ? "seated" : play_position == 2 ? "roomscale" : "standing";
    DiagnosticsRequestConfig(cfg, true);
  }
  if (play_position == 1)
    ImGui::TextWrapped("Recommended pose: rest your hands comfortably on your lap. Keep your wrists natural.");
  else if (play_position == 2)
    ImGui::TextWrapped("Recommended pose: stand naturally with your hands relaxed a little away from your sides.");
  else
    ImGui::TextWrapped("Recommended pose: a relaxed A shape. No exact angle or height is required.");
  ImGui::Spacing();
  const auto origin = ImGui::GetCursorScreenPos();
  const ImU32 ink = IM_COL32(223, 211, 184, 235);
  const ImU32 hand = status.pose_ready ? IM_COL32(165, 173, 140, 255)
                                       : IM_COL32(193, 126, 88, 255);
  const float cx = origin.x + content * .5f;
  auto draw_wrist = [&](int side, ImVec2 guide) {
    ImVec2 wrist=guide;
    if(status.hand_preview_valid[side]) {
      wrist.x=cx+status.hand_preview_position[side][0]*content*.24f;
      wrist.y=origin.y+30.f+status.hand_preview_position[side][1]*92.f;
    }
    draw->AddCircleFilled(wrist,9,hand);
    if(status.hand_preview_valid[side]) {
      const float a=status.hand_preview_angle[side];
      const ImVec2 tip{wrist.x+std::sin(a)*19.f,wrist.y-std::cos(a)*19.f};
      draw->AddLine(wrist,tip,IM_COL32(232,211,160,245),3.2f);
      draw->AddCircleFilled(tip,3.4f,IM_COL32(232,211,160,255));
    }
  };
  draw->AddCircle({cx, origin.y + 24}, 19, ink, 32, 3.2f);
  draw->AddLine({cx, origin.y + 46}, {cx, origin.y + 124}, ink, 3.2f);
  if (play_position == 1) {
    draw->AddLine({cx - 62, origin.y + 132}, {cx + 62, origin.y + 132}, ink, 3.2f);
    draw->AddLine({cx - 5, origin.y + 65}, {cx - 70, origin.y + 103}, ink, 3.2f);
    draw->AddLine({cx + 5, origin.y + 65}, {cx + 70, origin.y + 103}, ink, 3.2f);
    draw_wrist(0,{cx-70,origin.y+103});
    draw_wrist(1,{cx+70,origin.y+103});
  } else {
    draw->AddLine({cx - 5, origin.y + 66}, {cx - content * .14f, origin.y + 84}, ink, 3.2f);
    draw->AddLine({cx - content * .14f, origin.y + 84}, {cx - content * .21f, origin.y + 112}, ink, 3.2f);
    draw->AddLine({cx + 5, origin.y + 66}, {cx + content * .14f, origin.y + 84}, ink, 3.2f);
    draw->AddLine({cx + content * .14f, origin.y + 84}, {cx + content * .21f, origin.y + 112}, ink, 3.2f);
    draw_wrist(0,{cx-content*.21f,origin.y+112});
    draw_wrist(1,{cx+content*.21f,origin.y+112});
  }
  ImGui::Dummy({content, 148});
  ImGui::TextDisabled(status.pose_ready
      ? "L   BOTH CONTROLLERS TRACKED   R"
      : "L   WAITING FOR TRACKING   R");
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, status.save_failed
      ? PaletteColor(palette::ember_light) : PaletteColor(palette::text));
  ImGui::TextWrapped("%s", status.instruction);
  ImGui::PopStyleColor();
  if (status.pose_hint && *status.pose_hint) {
    ImGui::PushStyleColor(ImGuiCol_Text, kMoss);
    ImGui::TextWrapped("%s", status.pose_hint);
    ImGui::PopStyleColor();
  }
  ImGui::ProgressBar(status.hold_progress, {content, 14}, "");
  ImGui::TextDisabled("Wrist marks follow tracked grip position and rotation. Respawns and loads keep the saved fit; calm play refines this session only. This hold saves it.");
  if (status.can_cancel && ImGui::Button("Skip - keep my old measurements")) {
    CalibrationCancel();
    CancelMenuInput();
  }
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  ImGui::End();
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar(2);
}
void CalibrationPanel() {
  Title("Calibration");
  const auto profile = CalibrationCurrent();
  const auto status = CalibrationGetStatus();
  ImGui::TextWrapped(
      "Startup calibration runs before the game menu. Deaths and loads keep the saved measurements. "
      "Use Recalibrate to replace them during this session.");
  if (profile.valid) {
    ImGui::Text("Eye height  %.2f m%s", profile.eye_height_m,
                profile.floor_height_measured ? " (measured)" : " (setup value)");
    ImGui::Text("Estimated arm span  %.2f m", profile.arm_span_m);
    ImGui::Text("Estimated left / right reach  %.2f / %.2f m", profile.arm_reach_m[0],
                profile.arm_reach_m[1]);
    ImGui::Text("Saved profile revision  %llu", static_cast<unsigned long long>(profile.revision));
    ImGui::Text("Wrist alignment  %s%s",
        status.hand_alignment_valid ? "available" : "needs calibration",
        status.hand_alignment_valid && !status.hand_alignment_saved ? " (session fit)" : "");
    if(status.refinement_review_needed)
      ImGui::TextWrapped("Live wrist fit differs too much for a quiet update. Recalibrate to save a fresh one-second hold.");
  }
  if (ImGui::Button("Recalibrate...")) {
    CalibrationRequest();
    ModMenuClose();
  }
  ImGui::SameLine();
  if (ImGui::Button("Recenter view")) {
    CalibrationRecenter();
    XrRecenterTheater();
  }
  ImGui::TextWrapped(
      "Recenter aligns the view and tracking origin. It keeps your reach and wrist calibration.");
  int position = g_edit.play_style == "seated" ? 1 : g_edit.play_style == "roomscale" ? 2 : 0;
  if (ImGui::Combo("Play position", &position, "Standing\0Seated\0Roomscale\0")) {
    g_edit.play_style = position == 1 ? "seated" : position == 2 ? "roomscale" : "standing";
  }
  ImGui::TextWrapped(
      "Calibration supplies eye height. Seated lift raises the view without changing measured reach or anatomical scale.");
  int fit = g_edit.embodiment_fit == "arm" ? 1 : 0;
  (void)fit;
  ImGui::BeginDisabled(true);
  const char* fit_items[] = {"Head height + arm reach"};
  int fit_shown = 0;
  ImGui::Combo("Body fit", &fit_shown, fit_items, 1);
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "One combined fit: your calibrated height puts the camera on the native Head bone, and your measured arm reach constrains the same world scale (within a quarter either way) so the hands stay reachable without moving the eye off the head.");
  ImGui::Separator();
  if (g_edit.play_style == "seated") {
    ImGui::SliderFloat("Seated height offset (m)", &g_edit.seated_height_offset_m,
                                  0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped("Raise the seated viewpoint to a comfortable standing height. Your saved measurements stay unchanged.");
  }
  ImGui::Checkbox("Override calibrated eye height", &g_edit.eye_height_override);
  if (g_edit.eye_height_override) {
    ImGui::SliderFloat("Manual eye height (m)", &g_edit.eye_height, .5f, 2.5f, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Manual placement only. Your saved body measurements and world scale stay unchanged. Turn off the override to return to calibration.");
  } else if (!profile.valid)
    ImGui::TextDisabled("Using fallback height until calibration is available.");
  ImGui::SliderFloat("World / player scale trim", &g_edit.world_scale, .5f, 2.f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::SliderFloat("Avatar size (x)", &g_edit.avatar_scale, .6f, 1.6f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(
      "Avatar size multiplies the same single applied ratio as the trim above, so the head, hands, roomscale and the IK body never separate. It is not an independent mesh resize: bones, colliders and other characters are untouched.");
  ImGui::SliderFloat("Wrist roll trim (degrees)", &g_edit.grip_roll_correction_deg, -45,
                                45, "%.1f", ImGuiSliderFlags_AlwaysClamp);
  if (status.saving)
    ImGui::TextDisabled("Saving calibration...");
}
void RenderingPanel() {
  Title("Rendering & performance");
  ImGui::TextWrapped(
      "Active path: centered mono. Native stereo and alternating-eye AFR stay unavailable until a real per-eye scene-render hook is verified. Duplicating one finished image is not stereo.");
  ImGui::TextDisabled("True stereo: safely refused     AFR: safely refused");
  ImGui::TextWrapped(
      "Gameplay is immersive first person by default. The 16:9 theater opens for the title screen, loading and DSVR menu. Startup calibration runs in a separate immersive room. Native game menus are not reliably detected yet; use F8 to toggle theater for those. Hold the right trigger and push the right stick up/down to resize the theater window live.");
  ImGui::TextWrapped(
      "DSR HUD and native menus are still baked into the mono game image; no separate native UI pass is verified. Theater screen bounds are fitted inside both eye views to avoid cropping; separate HUD/menu layers remain blocked on locating that pass.");
  RenderMetrics metrics{};
  if (XrRenderMetricsSnapshot(metrics)) {
    ImGui::Text("Source  %u x %u    Eye  %u x %u", metrics.source_width, metrics.source_height,
                metrics.eye_width, metrics.eye_height);
    ImGui::Text("CPU submission  %.2f ms    XR wait  %.2f ms", metrics.cpu_frame_ms,
                metrics.xr_wait_ms);
    ImGui::Text("Image wait  %.2f ms    Copy / draw  %.2f / %.2f ms", metrics.image_wait_ms,
                metrics.source_copy_cpu_ms, metrics.eye_blit_cpu_ms);
    ImGui::Text("Image timeouts  %llu    Held-image skips  %llu",
                static_cast<unsigned long long>(metrics.image_timeouts),
                static_cast<unsigned long long>(metrics.held_image_skips));
    if (metrics.gpu_valid)
      ImGui::Text("GPU copy / draw  %.2f / %.2f ms", metrics.gpu_copy_ms, metrics.gpu_blit_ms);
    else
      ImGui::TextDisabled("GPU timing pending.");
  } else
    ImGui::TextDisabled("Timing becomes available after a VR frame is submitted.");
  EffectiveRate rate{};
  if (XrEffectiveRate(rate)) {
    ImGui::Text("Observed app cadence %.1f Hz; physical headset refresh %.1f Hz",
                rate.runtime_hz, rate.display_hz);
    if (rate.runtime_throttled)
      ImGui::TextWrapped(
                "The OpenXR runtime is throttling app frames below the physical headset refresh.");
  }
  
  
  
  
  
  
  ImGui::TextWrapped(
        "DSR-FPS-Unlock owns frame limiting: it uses the last headset-reported refresh in VR and the current monitor refresh on desktop. DSVR never sets a native cap. If no valid refresh is available, the unlocker stays disabled instead of using a preset.");
  ImGui::Separator();
  ImGui::Checkbox("Use ReShade D3D11 host (restart required)", &g_edit.reshade_host);
  ImGui::TextWrapped(
      "Present/DXGI is the default compatibility path. ReShade avoids the Present hook but needs DSVR-ReShade.addon64 and has no automatic fallback; restart after changing hosts.");
  ImGui::TextWrapped(
      "Eye-buffer resolution (restart required): the eye swapchains are sized once when the session creates them, so this applies after restarting the game. The native game resolution also affects rendering cost. Choose the scale that remains smooth at the automatic headset or monitor refresh.");
  ImGui::SliderInt("Eye resolution (%) (restart required)", &g_edit.render_scale_percent, 50, 200, "%d",
                              ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped("100%% uses each eye's exact runtime recommendation. Game image resolution is reported separately.");
  RenderMetrics render_metrics{};
  if (XrRenderMetricsSnapshot(render_metrics)) {
    ImGui::Text("Headset: L %ux%u / R %ux%u", render_metrics.eye_widths[0],
        render_metrics.eye_heights[0], render_metrics.eye_widths[1], render_metrics.eye_heights[1]);
    ImGui::Text("Game image: %ux%u", render_metrics.source_width, render_metrics.source_height);
    if (render_metrics.source_retained)
      ImGui::Text("Waiting for a fresh image (%llu ms)", render_metrics.source_age_ms);
  }
  if (ImGui::Button("Performance (70%)")) {
    g_edit.render_scale_percent = 70;
  }
  ImGui::SameLine();
  if (ImGui::Button("Balanced (85%)")) {
    g_edit.render_scale_percent = 85;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quality (100%)")) {
    g_edit.render_scale_percent = 100;
  }
  static const char* aa_labels[] = {"Off", "FXAA"};
  int aa_index = std::clamp(g_edit.aa_mode, 0, 1);
  if (ImGui::Combo("Anti-aliasing", &aa_index, aa_labels, 2)) {
    g_edit.aa_mode = aa_index;
  }
  ImGui::TextWrapped(
      "FXAA smooths the complete image. Native MSAA 2x/4x and TAA are unavailable because the game supplies a completed 1x frame without scene motion data.");
  ImGui::Checkbox("Contrast adaptive sharpening (CAS)", &g_edit.cas_enabled);
  ImGui::BeginDisabled(!g_edit.cas_enabled);
  ImGui::SliderFloat("CAS strength", &g_edit.cas_strength, 0.0f, 1.0f, "%.2f",
                     ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "Sharpening runs after the game image and VR menu are composed, so it also affects text and room details. Keep the strength low to avoid halos.");
  
    ImGui::Checkbox("Comfort vignette", &g_edit.comfort_vignette);
  ImGui::BeginDisabled(!g_edit.comfort_vignette);
  ImGui::SliderFloat("Vignette strength", &g_edit.vignette_strength, 0, 1, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::SliderFloat("Field of view (comfort)", &g_edit.fov_scale, 0.7f, 1.3f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(
      "1.00 matches your headset. Lower narrows the view if turning feels queasy; higher widens it. Changes apply immediately and save automatically.");
        ImGui::SliderFloat("Theater screen width (horizontal FOV)", &g_edit.mono_screen_fov_deg, 30,
                         110, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(
      "Wider shows more of the game; narrower makes it feel like a smaller cinema screen. Aspect ratio is preserved.");
  ImGui::SliderFloat("Theater distance (m)", &g_edit.menu_distance_m, 1, 4, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::Checkbox("Keep theater during gameplay", &g_edit.gameplay_theater);
  ImGui::Checkbox("Desktop mirror (spectator window)", &g_edit.desktop_present);
  ImGui::TextWrapped(
      "Off is the default: play is headset-only with no desktop window at all. "
      "On keeps the game window mirroring play for spectators and capture; "
      "the headset image is unaffected either way.");
  ImGui::TextWrapped(
      "Off is the default: play is immersive and the flat screen appears only for menus, the title screen and cutscenes. Turn this on to keep the flat screen while playing.");
  if (ImGui::Button("Toggle theater"))
    XrToggleTheater();
}
void SettingsPanel() {
  Title("Comfort & body");
  const auto body = BodyVisibilityGetStatus();
  ImGui::BeginDisabled(!body.installed);
  ImGui::Checkbox("Hide face, headgear and hair", &g_edit.hide_head);
  ImGui::Checkbox("Hide torso and legs (arms and weapon stay)", &g_edit.hide_torso_and_legs);
  ImGui::EndDisabled();
  if (!body.installed)
    ImGui::TextWrapped("Native head geometry filter unavailable.");
  else {
    ImGui::TextWrapped(
        "Tick either row to change it immediately; untick to bring the part back. "
        "The menu canvas itself always draws the whole body, and the filter returns "
        "to gameplay masking on the next native update — so the change can read "
        "as landing the moment the menu closes. "
        "Arms and held weapons are never filtered.");
    ImGui::TextDisabled("Filtered %llu policy refreshes %llu refused %llu",
                        static_cast<unsigned long long>(body.filtered),
                        static_cast<unsigned long long>(body.refreshes),
                        static_cast<unsigned long long>(body.refused));
  }
  ImGui::BeginDisabled();
  ImGui::Checkbox("Forearms, hands and held equipment only", &g_edit.forearm_only);
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "Forearm visibility is unavailable while the native mesh filter is being replaced. "
      "The previous bone-collapse method stretched the upper arms and has been removed.");
  ImGui::Checkbox("Hide alternate player model", &g_edit.hide_second_model);
  const auto ui = NativeUiGetStatus();
  ImGui::BeginDisabled(!ui.installed);
  ImGui::Checkbox("Hide native UI in immersive VR", &g_edit.hide_ui_in_vr);
  ImGui::EndDisabled();
  if (!ui.installed)
    ImGui::TextDisabled("Native UI hook unavailable for this session.");
  ImGui::Separator();
  static const char *movement_modes[] = {"Head (default)", "Controller hand"};
  int movement_mode = static_cast<int>(g_edit.movement_orientation);
  if (ImGui::Combo("Left stick oriented by", &movement_mode, movement_modes, 2)) {
    g_edit.movement_orientation = static_cast<MovementOrientation>(movement_mode);
  }

  static const char *movement_controllers[] = {"Non-dominant hand", "Left hand", "Right hand"};
  int movement_controller = static_cast<int>(g_edit.movement_controller);
  ImGui::BeginDisabled(g_edit.movement_orientation != MovementOrientation::Controller);
  if (ImGui::Combo("Controller hand", &movement_controller, movement_controllers, 3)) {
    g_edit.movement_controller = static_cast<MovementController>(movement_controller);
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "Smooth locomotion always has one live steering reference: your head, or the selected hand's aim. If that hand leaves view or points straight up/down with no stable heading, movement continues head-oriented instead of going dead. A changed reference takes effect when the stick is next centred.");

    ImGui::Checkbox("Left hand dominant", &g_edit.dominant_hand_left);
  ImGui::Checkbox("Snap turning", &g_edit.snap_turn);
    ImGui::BeginDisabled(!g_edit.snap_turn);
  ImGui::SliderInt("Snap angle", &g_edit.snap_turn_degrees, 15, 90, "%d", ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::Checkbox("Smooth turning", &g_edit.smooth_turn);
  ImGui::BeginDisabled(!g_edit.smooth_turn);
  if (probe_disabled_item) {
    ImGui::Button("##probe");
    item_was_disabled =
        (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    probe_disabled_item = false;
  }
  ImGui::SliderFloat("Turn speed (degrees/s)", &g_edit.smooth_turn_speed_dps, 30, 360,
                          "%.0f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  // Everyday controls above. The advanced rows below read under one labeled
  // always-visible group: same panel, same IDs/labels/behavior — grouping
  // only. (Plain Text row, not SeparatorText: its log decoration prefix
  // would hide the grouping from the label-capture grep, so a plain row
  // keeps the grouping visible to the assertions below. No collapse, no
  // reorder: the label row is unconditional so the default render still
  // shows every advanced control.)
  ImGui::Separator();
  ImGui::TextUnformatted("Advanced tuning");
    
  ImGui::Checkbox("Track native arms", &g_edit.arm_ik_enabled);
  ImGui::Checkbox("Observe arm targets without applying them", &g_edit.arm_ik_log_only);
        ImGui::Checkbox("Native animation cannot move IK bodyparts", &g_edit.arm_ik_block_animation);
  ImGui::Separator();
  ImGui::TextUnformatted("Controller bindings (restart required)");
  ImGui::Checkbox("Quest 3 / Touch (restart required)", &g_edit.bind_profile_quest3);
  ImGui::Checkbox("Valve Index (restart required)", &g_edit.bind_profile_index);
  ImGui::Checkbox("WMR motion controller (restart required)", &g_edit.bind_profile_wmr);
  ImGui::Checkbox("OpenXR simple controller fallback (restart required)", &g_edit.bind_profile_simple);
  ImGui::Checkbox("Update profile filter live", &g_edit.binding_live_reattach);
  if (ImGui::Button("Refresh profile filter now"))
    InputRequestBindingReload();
  ImGui::TextWrapped(
      "Supported OpenXR profiles are suggested at session creation; the action set is attached once, never rebuilt or reattached live. These switches filter input for each hand's current runtime profile. Edits save immediately but the attached session keeps running them; restart the game before expecting new profile suggestions. The refresh button applies pending profile edits without re-suggesting bindings. The legacy live-update preference is retained for saved-config compatibility only.");
  ImGui::TextWrapped(
      "Simple-controller fallback is an OpenXR profile, not Xbox. Native Xbox/XInput mapping and its binding editor are separate and are not implemented here.");
  // Everyday rows above (movement, turning). The arm rows above bracketed by
  // the "Advanced tuning" label are the advanced group; the toggle rows that
  // follow here (arm swing, reach) read under the same grouping: same panel,
  // same IDs/labels/behavior — grouping only.
  ImGui::Separator();
  ImGui::Checkbox("Swinging the arms asks the game to run", &g_edit.arm_swing_sprint);
    if (g_edit.arm_swing_sprint) {

    ImGui::SliderFloat("Stride hand speed (m/s)", &g_edit.arm_swing_stride_min, .4f, 4,
                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Too fast to be a stride (m/s)", &g_edit.arm_swing_stride_max,
                                  1.f, 12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Other hand still below (m/s)", &g_edit.arm_swing_quiet_max, .1f,
                                  3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Strides before it engages", &g_edit.arm_swing_alternations, 2, 12,
                                "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Run lingers for (ms)", &g_edit.arm_swing_hold_ms, 200, 3000, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
  }
  ImGui::TextWrapped(
      "Alternation is the test: a stride counts only when the other hand was still as it started, and the pattern counts only when consecutive strides swap hands. One hand flapping, a stretch, a clap or a two-handed swing never engages it, because all of those move both hands together. Speeds are head-relative, so walking the room or riding a lift cannot manufacture a gait. The output is one forward amount in the same movement basis as the stick, so native acceleration, stamina, turning and the dash stay the game's.");
  ImGui::Separator();
  ImGui::Checkbox("Thrust the free hand forward to interact", &g_edit.reach_interact);
  if (g_edit.reach_interact) {
    ImGui::SliderFloat("Reach distance (m)", &g_edit.reach_forward_min_m, .2f, 1.5f,
                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Beyond this nothing counts (m)", &g_edit.reach_forward_max_m,
                                  .6f, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Thrust speed (m/s)", &g_edit.reach_speed_min, .3f, 5, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Must be going forward (fraction)", &g_edit.reach_dominance_min,
                                  .2f, .95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Hands must rest for (ms)", &g_edit.reach_settle_ms, 0, 1000, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Repeat interval (ms)", &g_edit.reach_cooldown_ms, 250, 3000, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
  }
  ImGui::TextWrapped(
      "No native prompt flag is bound yet, so the gesture alone cannot decide: untargeted reaches are refused and logged until a native target owner exists. Both hands have to be still first, and the other hand has to stay still through the thrust.");
}
void CombatPanel() {
  Title("Combat & feedback");
  ImGui::TextWrapped(
      "Proving ground: swing at the sparring target, hold your off-hand up as a shield, then move your weapon blade into it. Contact just after raising the guard is classified as a parry.The sparring benchis tracked geometry onlyand appliesno nativedamage;the input previews below are separate.");
  ImGui::Checkbox("Combat proving ground (sparring target)", &g_edit.combat_test_bench);
  ImGui::BeginDisabled(!g_edit.combat_test_bench);
  ImGui::SliderFloat("Sparring target distance (m)", &g_edit.combat_test_target_m, .5f,
                                2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
        ImGui::SliderFloat("Min hand separation (m, 0 = off)", &g_edit.weapon_separation_min_m, 0.f,
                         .5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::Checkbox("Motion combat preference", &g_edit.motion_attacks);
  ImGui::Checkbox("Swing-to-native attack input (preview)", &g_edit.physical_attacks);
  ImGui::TextDisabled("Input preview only;native physical contact damage is not bound.");
  ImGui::BeginDisabled(!g_edit.physical_attacks || !g_edit.motion_attacks);
  ImGui::SliderFloat("Light attack speed (m/s)", &g_edit.swing_attack_speed_min, .5f, 8,
                                "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SliderFloat("Strong attack speed (m/s)", &g_edit.swing_attack_heavy_min, 1.f,
                                12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  
  ImGui::EndDisabled();
  // Everyday rows above. The per-hand thresholds below (+ the grip editor
  // further down) read under one labeled always-visible group: same panel,
  // same IDs/labels/behavior — grouping only. (No collapse, no reorder: the
  // label row is unconditional so the default render still shows every
  // advanced control.)
  ImGui::Separator();
  ImGui::TextUnformatted("Advanced tuning");
  ImGui::Checkbox("Raised shield hand guards, flick parries (preview)",
                             &g_edit.physical_defence);
  if (g_edit.physical_defence) {
    
    ImGui::SliderFloat("Flick speed (m/s)", &g_edit.guard_flick_speed_min, .8f, 6,
                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Parry tap length (ms)", &g_edit.guard_parry_press_ms, 40, 250,
                                "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderInt("Parry repeat interval (ms)", &g_edit.guard_parry_cooldown_ms, 250,
                                2000, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Raise the shield hand for immediate native guard (LB), including while moving. A fast flick requests a bounded parry pulse (LT); until incoming-attack contact is bound, this remains a preview and does not establish native parry outcomes.");
  }
  if (!g_edit.arm_ik_enabled || g_edit.arm_ik_log_only || !g_edit.arm_ik_block_animation)
    ImGui::TextDisabled("Needs fresh committed IK arms with animation ownership.");
  ImGui::Checkbox("Haptic feedback", &g_edit.haptics);
  ImGui::BeginDisabled(!g_edit.haptics);
  ImGui::SliderFloat("Minimum contact speed (m/s)", &g_edit.contact_speed_min, .1f, 5,
                                "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SliderInt("Repeat hit interval (ms)", &g_edit.rehit_window_ms, 50, 2000, "%d",
                              ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::Separator();
  {
    CombatBenchSnapshot bench;
    const bool live = ArmIkCombatBench(bench);
    if (!g_edit.combat_test_bench) {
      ImGui::TextDisabled(
          "Turn on the proving ground, then swing — live blade state appears here.");
    } else if (!live) {
      ImGui::TextDisabled("Waiting for a fresh tracked commit (headset on, arms tracked).");
    } else {
      const int weapon = g_edit.dominant_hand_left ? 0 : 1;
      const int shield = 1 - weapon;
      ImGui::Text("Weapon tip speed  %.2f m/s", (double)bench.tip_speed[weapon]);
      ImGui::Text("Shield tip speed  %.2f m/s", (double)bench.tip_speed[shield]);
      const char *event = "No contact yet — swing through the target ahead of you";
      if (bench.last_event == 1)
        event = "HIT — blade reached the target at speed";
      else if (bench.last_event == 2)
        event = "BLOCK — guard held, blade reached the shield";
      else if (bench.last_event == 3)
        event = "PARRY — newly raised guard, blade reached the shield";
      ImGui::TextWrapped("%s", event);
      if (bench.last_event == 1)
        ImGui::Text("Damage scale  %.2f (target %d)", (double)bench.last_scale, bench.last_target);
      ImGui::Text("Hand separation  %.3f m", (double)bench.hand_separation_m);
      if (bench.hands_too_close) {
        ImGui::PushStyleColor(ImGuiCol_Text, PaletteColor(palette::ember_light));
        ImGui::TextWrapped("Hands too close — weapons would clip. Widen your guard.");
        ImGui::PopStyleColor();
      }
    }
  }
  const auto combat_status = CombatNativeGetStatus();
  if (!combat_status.physical_damage_bound)
    ImGui::TextDisabled(
        "Tracked-contact damage is not bound. Guard/parry input previews do not qualify native outcomes.");
  // Everyday Combat bench state stays where it was (belongs to the proving
  // ground); the grip editor below reads under the same "Advanced tuning"
  // label as the per-hand rows above. Same panel, same IDs/labels/behavior — grouping only.
  ImGui::Separator();
  ImGui::TextWrapped(
      "Support hand: hold grip near the handle to attach; release to detach. Equipment-specific hand placements are stored separately from player calibration.");
  ImGui::Separator();
  static WeaponGripProfile edit;
  static int selected = -1;
  static bool preview = false;
  const auto native = CombatNativeGetStatus();
  if (native.equipment_bound) {
    ImGui::Text("Equipped IDs  Left %d / Right %d", native.equipment.weapon_id[0],
                native.equipment.weapon_id[1]);
    if (ImGui::Button("Edit equipped weapon grip")) {
      WeaponGripClearPreview();
      preview = false;
      selected = native.equipment.weapon_id[g_edit.dominant_hand_left ? 0 : 1];
      edit = {};
      edit.weapon_id = selected;
      edit.dominant_left = g_edit.dominant_hand_left;
      WeaponGripGet(selected, edit.dominant_left, edit);
    }
  } else
    ImGui::TextDisabled("Equip a weapon in gameplay to select its grip profile.");
  if (selected >= 0) {
    ImGui::Text("Editing weapon %d (%s hand)", selected, edit.dominant_left ? "left" : "right");
    bool changed = false;
    changed |= ImGui::SliderFloat3("Primary offset (m)", &edit.primary.position_m.x, -.5f, .5f,
                                   "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat3("Primary rotation", &edit.primary.rotation_deg.x, -180, 180,
                                   "%.1f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::Checkbox("Enable support grip", &edit.support_enabled);
    ImGui::BeginDisabled(!edit.support_enabled);
    changed |= ImGui::SliderFloat3("Support offset (m)", &edit.support.position_m.x, -.5f, .5f,
                                   "%.3f", ImGuiSliderFlags_AlwaysClamp);
    changed |= ImGui::SliderFloat3("Support rotation", &edit.support.rotation_deg.x, -180, 180,
                                   "%.1f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    const bool toggled = ImGui::Checkbox("Preview grip in gameplay", &preview);
    if ((changed || toggled) && preview)
      WeaponGripPreview(edit);
    if (toggled && !preview)
      WeaponGripClearPreview();
    if (preview && ImGui::Button("View preview in gameplay")) {
      // Keep the temporary profile while returning to tracked gameplay . Home
      // brings the editor back ; Close / revert discards it without touching disk .
      g_open.store(false);
      CancelMenuInput();
    }
    if (preview)
      ImGui::TextWrapped("Return with Home to save or cancel the preview.");
    if (ImGui::Button("Save grip")) {
      const auto profile = edit;
      DiagnosticsRequestCommand("Save weapon grip", [profile] {
        const bool ok = WeaponGripSave(profile);
        if (ok)
          WeaponGripClearPreview();
        return ok;
      });
      preview = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to native placement")) {
      const int id = selected;
      const bool left = edit.dominant_left;
      edit = {};
      edit.weapon_id = id;
      edit.dominant_left = left;
      preview = false;
      WeaponGripClearPreview();
      DiagnosticsRequestCommand("Reset weapon grip",
                                [id, left] { return WeaponGripReset(id, left); });
    }
  }
}
void LogsPanel() {
  Title("Logs");
  static ImGuiTextFilter filter;
  filter.Draw("Filter", 260);
  ImGui::SameLine();
  if (ImGui::Button("Export recent logs"))
    DiagnosticsRequestLogExport();
  ImGui::SameLine();
  if (ImGui::Button("Clear view"))
    LogClearRecent();
  static bool follow = true;
  ImGui::Checkbox("Follow new entries", &follow);
  ImGui::BeginChild("Log entries", {0, 0}, ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_HorizontalScrollbar);
  const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4;
  for (const auto &entry : LogRecent())
    if (filter.PassFilter(entry.text.c_str())) {
      if (entry.level == LogLevel::Error)
        ImGui::PushStyleColor(ImGuiCol_Text, PaletteColor(palette::ember_light));
      else if (entry.level == LogLevel::Warn)
        ImGui::PushStyleColor(ImGuiCol_Text, PaletteColor(palette::bone));
      ImGui::TextUnformatted(entry.text.c_str());
      if (entry.level != LogLevel::Info)
        ImGui::PopStyleColor();
    }
  if (follow && at_bottom)
    ImGui::SetScrollHereY(1);
  ImGui::EndChild();
}
void DiagnosticsPanel() {
  Title("Diagnostics");
  ImGui::TextWrapped(
      "Capture a marker immediately after an alignment or pacing problem, then export recent logs.");
  if (ImGui::Button("Write diagnostic marker"))
    DiagnosticsLogMarker();
  ImGui::SameLine();
  if (ImGui::Button("Export recent logs"))
    DiagnosticsRequestLogExport();
  ImGui::Checkbox("Verbose diagnostic logging", &g_edit.verbose_logging);
  ImGui::Separator();
  ImGui::TextUnformatted("CPU timings since launch (nested stages overlap)");
  if (ImGui::Button("Reset CPU timings"))
    g_cpu_frame_timer.Reset();
  const auto cpu = g_cpu_frame_timer.Snapshot();
  for (unsigned i = 0; i < cpu.size(); ++i)
    if (cpu[i].calls)
      ImGui::Text("%s: %.3f ms average / %.3f ms peak", kCpuStageNames[i], cpu[i].AverageMs(),
                  cpu[i].max_ms);
  ImGui::Separator();
  const auto ui = NativeUiGetStatus();
  ImGui::Text(
      "Native UI hook: %s, hidden %llu / %llu visits", ui.installed ? "installed" : "unavailable",
      static_cast<unsigned long long>(ui.hidden), static_cast<unsigned long long>(ui.visits));
  const auto body = BodyVisibilityGetStatus();
  ImGui::Text("Native head geometry: %s, filtered %llu / %llu commands",
              body.installed ? "installed" : "unavailable",
              static_cast<unsigned long long>(body.filtered),
              static_cast<unsigned long long>(body.visits));
  ImGui::Text("Geometry refreshes %llu / refused %llu",
              static_cast<unsigned long long>(body.refreshes),
              static_cast<unsigned long long>(body.refused));
  const auto blur = MotionBlurGetStatus();
  ImGui::Text("Motion blur setting hooks: %s", blur.installed ? "installed" : "unavailable");
  ImGui::Text("Blur reads forced off: %llu / enable requests blocked: %llu",
              static_cast<unsigned long long>(blur.reads),
              static_cast<unsigned long long>(blur.blocked_enables));
  ImGui::TextDisabled("Home: menu     F6: recenter     F7: reload settings     F8: theater     F9: log marker");
  ImGui::TextWrapped(
      "Desktop: arrows/Enter/Esc navigate, mouse points. The F7 and F9 keys can be rebound in Options.");
  ImGui::TextWrapped(
      "Headset: left Menu + right grip (0.5 s) or both sticks (3 s) opens this menu; a quick both-stick tap recenters. "
      "Left stick moves, right stick turns, weapon squeeze/trigger attacks, off-hand squeeze blocks, a flick parries. "
      "Use the left stick and A to navigate/select, or aim and pull the right trigger. B goes back.");
  ImGui::TextWrapped("The game world continues while this menu is open.");
}
// Startup-selected frame-host values, frozen at install time (core.cpp) or
// at this menu's first live-config observation before the game frame host
// exists (headless test). The comparison only shows (and the reminder only
// fires) after a gated row actually differs, never from the seeding.
bool g_startup_valid = false;
bool g_startup_reshade = false;
int g_startup_scale = 100;
void SeedStartupBaseline(const Config& applied) {
  if (g_startup_valid) return;
  // Before the game frame host exists the startup values are whatever the
  // live config already runs (headless test); after it exists they are the
  // frozen install values. Either way the comparison below only fires after
  // a gated row actually changes, never from this seeding.
  bool host = false;
  int scale = 0;
  if (CoreStartupRenderHost(host)) g_startup_reshade = host;
  else g_startup_reshade = applied.reshade_host;
  if (CoreStartupEyeScale(scale)) g_startup_scale = scale;
  else g_startup_scale = applied.render_scale_percent;
  g_startup_valid = true;
}
// Human-readable form of the restart comparison, shared by every close path
// (Close button, toggle, B button, header X). Gated-vs-live: bindings freeze
// at session attach (InputLiveBindingSignature); the render host and eye
// resolution freeze at first swapchain setup or, before that, at this menu's
// first live-config observation. The comparison only shows (and the reminder
// only fires) after a gated row actually differs, never from the seeding.
std::string RestartPendingText() {
  if (!g_startup_valid) return {};
  std::string pending;
  const auto live = InputLiveBindingSignature();
  const auto fresh = DiagnosticsCurrentConfig();
  if (fresh.bind_profile_quest3 != live.quest3 || fresh.bind_profile_index != live.index ||
      fresh.bind_profile_wmr != live.wmr || fresh.bind_profile_simple != live.simple)
    pending += "bindings";
  if (fresh.reshade_host != g_startup_reshade) {
    if (!pending.empty()) pending += ", ";
    pending += "ReShade host";
  }
  if (fresh.render_scale_percent != g_startup_scale) {
    if (!pending.empty()) pending += ", ";
    pending += "eye resolution";
  }
  return pending;
}
bool ModMenuCaptureTextArmed() { return ImGui::GetCurrentContext() && GImGui->LogEnabled; }
void DrawMenu() {
  const auto settings = DiagnosticsSettingsSnapshot();
  SeedStartupBaseline(settings.applied);
  // No draft: every frame starts from the live applied config; the panels
  // mutate it, and the footer pushes any difference through immediately.
  g_edit = settings.applied;
  // DrawMenu must not run before its helpers exist: RestartPendingText and
  // DrawRestartReminder are defined above (anonymous namespace), so the
  // footer comparison and the close-path reminder resolve here.
  const auto size = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos({size.x * .5f, size.y * .5f}, ImGuiCond_Always, {.5f, .5f});
  ImGui::SetNextWindowSize({std::min(1180.f, size.x - 32), std::min(860.f, size.y - 32)},
                           ImGuiCond_Always);
  bool open = true;
  ImGui::Begin("DSVR controls", &open,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoResize);
  if (!open)
    ModMenuClose();
  const char *state = settings.busy ? "Saving settings..."
                                    : g_edit != settings.saved ? "Saving settings..." : "Applied and saved";
  // Brand band : the mod name in the display face , the live apply state on the
  // same line , and an ochre rule . The footer keeps the full sentence .
  {
    const float line_x = ImGui::GetCursorPosX();
    const float content_w = ImGui::GetContentRegionAvail().x;
    if (g_title_font)
      ImGui::PushFont(g_title_font);
    ImGui::PushStyleColor(ImGuiCol_Text, kBone);
    ImGui::TextUnformatted("DSVR");
    ImGui::PopStyleColor();
    if (g_title_font)
      ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosX(line_x + content_w - ImGui::CalcTextSize(state).x);
    ImGui::TextDisabled("%s", state);
    const auto rule = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine({rule.x, rule.y + 3}, {rule.x + content_w, rule.y + 3},
                                        IM_COL32(196, 154, 74, 190), 1.5f);
    ImGui::Dummy({0, 10});
  }
  const float footer = 108;
  ImGui::BeginChild("Navigation", {215, -footer});
  const char *names[] = {"Comfort & body", "Calibration", "Rendering",
                         "Combat & grips", "Diagnostics", "Logs"};
  for (int i = 0; i < 6; ++i)
    if (ImGui::Selectable(names[i], g_panel == i, 0, {0, 40}))
      g_panel = i;
  std::vector<Panel> panels;
  {
    std::lock_guard<std::mutex> lock(g_panel_mutex);
    panels = g_panels;
  }
  for (size_t i = 0; i < panels.size(); ++i)
    if (ImGui::Selectable(panels[i].name.c_str(), g_panel == static_cast<int>(i) + 6, 0, {0, 40}))
      g_panel = static_cast<int>(i) + 6;
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("Panel", {0, -footer});
  ImGui::PushItemWidth(std::max(140.f, ImGui::GetContentRegionAvail().x * .43f));
  switch (g_panel) {
  case 0:
    SettingsPanel();
    break;
  case 1:
    CalibrationPanel();
    break;
  case 2:
    RenderingPanel();
    break;
  case 3:
    CombatPanel();
    break;
  case 4:
    DiagnosticsPanel();
    break;
  case 5:
    LogsPanel();
    break;
  default:
    if (g_panel >= 6 && static_cast<size_t>(g_panel - 6) < panels.size())
        panels[g_panel - 6].draw();
      break;
  }
  ImGui::PopItemWidth();
  ImGui::EndChild();
  ImGui::Separator();
  // No draft buffer: any panel mutation differs from the applied config, so
  // push it straight through (live + saved). Widgets mutate g_edit every
  // frame, so the footer sees the edit on the tick it happens. The live
  // snapshot is re-read for the restart-line comparison below, so the footer
  // line appears on the same frame the gated row actually changes.
  if (g_edit != settings.applied) DiagnosticsRequestConfig(g_edit, true);
  std::string restart_pending;
  {
    // Compare the just-pushed live config against the frozen session values
    // so the line appears on the same tick the gated edit lands.
    const auto live = InputLiveBindingSignature();
    const auto fresh = DiagnosticsCurrentConfig();
    if (fresh.bind_profile_quest3 != live.quest3 ||
        fresh.bind_profile_index != live.index ||
        fresh.bind_profile_wmr != live.wmr ||
        fresh.bind_profile_simple != live.simple) {
      if (!restart_pending.empty()) restart_pending += ", ";
      restart_pending += "bindings";
    }
    if (g_startup_valid && fresh.reshade_host != g_startup_reshade) {
      if (!restart_pending.empty()) restart_pending += ", ";
      restart_pending += "ReShade host";
    }
    if (g_startup_valid && fresh.render_scale_percent != g_startup_scale) {
      if (!restart_pending.empty()) restart_pending += ", ";
      restart_pending += "eye resolution";
    }
    if (!restart_pending.empty())
      ImGui::TextWrapped("Restart needed: %s (restart the game to apply).",
                         restart_pending.c_str());
  }
  if (ImGui::Button("Close")) {
    if (!restart_pending.empty() && restart_pending != g_restart_ack) {
      // Restart-gated edits landed but cannot apply until the game restarts.
      // Hold the menu open with an explicit reminder instead of silently
      // dropping the change; restarting itself stays the operator's call.
      g_restart_modal = true;
    } else {
      ModMenuClose();
    }
  }
  ImGui::TextDisabled("Changes apply the moment you tick them and save automatically.");
  if (g_restart_modal) ImGui::OpenPopup("Restart needed");
  if (ImGui::BeginPopupModal("Restart needed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("Restart the game to apply: %s.",
                       restart_pending.empty() ? g_restart_ack.c_str() : restart_pending.c_str());
    ImGui::TextWrapped("Everything else already applied. Restarting is up to you.");
    if (ImGui::Button("Keep playing")) {
      g_restart_ack = restart_pending;
      g_restart_modal = false;
      ImGui::CloseCurrentPopup();
      if (g_close_requested) {
        g_close_requested = false;
        ModMenuClose();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close menu anyway")) {
      g_restart_ack = restart_pending;
      g_restart_modal = false;
      ImGui::CloseCurrentPopup();
      ModMenuClose();
    }
    ImGui::EndPopup();
  }
  ImGui::TextWrapped("%s", DiagnosticsSettingsStatus().c_str());
  ImGui::End();
}
}  // namespace
void ModMenuToggle() {
  if (g_open.exchange(true)) {
    // Genuine close (not an open): same restart reminder as the Close button.
    const auto pending = RestartPendingText();
    if (!pending.empty() && pending != g_restart_ack) {
      g_open.store(true);
      g_restart_modal = true;
      g_close_requested = true;
      return;
    }
    ModMenuClose();
  } else {
    g_restart_ack.clear();
  }
}
void ModMenuClose() {
  const auto pending = RestartPendingText();
  if (g_open.load() && !pending.empty() && pending != g_restart_ack && !g_restart_modal) {
    // Restart-gated edits landed but cannot apply until the game restarts.
    // Hold the menu open with an explicit reminder instead of silently
    // dropping the change; restarting itself stays the operator's call.
    g_restart_modal = true;
    return;
  }
  g_open.store(false);
  g_restart_modal = false;
  WeaponGripClearPreview();
  CancelMenuInput();
}
// Test-only: acknowledge the restart reminder and close, matching the
// popup's "Close menu anyway" button for headless coverage.
void ModMenuCloseForTest() {
  g_restart_ack = RestartPendingText();
  g_restart_modal = false;
  g_close_requested = false;
  g_open.store(false);
  WeaponGripClearPreview();
  CancelMenuInput();
}
bool ModMenuVisible() { return g_open.load() || CalibrationGetStatus().screen_visible; }
bool ModMenuSettingsOpen() { return g_open.load(); }
bool ModMenuCapturesInput() { return g_open.load() || CalibrationCapturesInput(); }
void ModMenuControllerInput(float x, float y, bool select, bool back,
                            bool prev_panel, bool next_panel) {
  std::lock_guard<std::mutex> lock(g_input_mutex);
  g_controller = {std::isfinite(x) ? std::clamp(x, -1.f, 1.f) : 0,
                  std::isfinite(y) ? std::clamp(y, -1.f, 1.f) : 0, select, back,
                  prev_panel, next_panel};
}
bool ModMenuRegisterPanel(const char *id, const char *name, ModMenuPanel draw) {
  if (!id || !*id || !name || !*name || !draw)
    return false;
  std::lock_guard<std::mutex> lock(g_panel_mutex);
  if (g_panels.size() >= 16)
    return false;
  for (const auto &panel : g_panels)
    if (panel.id == id)
      return false;
  g_panels.push_back({id, name, draw});
  return true;
}
bool ModMenuRenderTexture(ID3D11Device *device, ID3D11Texture2D *image,
                          bool clear_transparent, const char* render_status) {
  if (!device || !image) return false;
  ComPtr<ID3D11Device> owner;
  image->GetDevice(&owner);
  if (owner.Get() != device)
    return false;
  D3D11_TEXTURE2D_DESC desc{};
  image->GetDesc(&desc);
  const bool menu_visible = ModMenuVisible() || render_status;
  const MenuPointerState pointer = XrMenuPointerInput(desc.Width, desc.Height);
  // The XR owner passes a separate transparent target; desktop presentation
  // passes the game's backbuffer. Never leave a stale pointer or panel in either.
  if (!menu_visible) {
    g_pointer_smooth.Reset();
    return false;
  }
  ImGuiContext *previous = ImGui::GetCurrentContext();
  if (!Initialize(device)) {
    ImGui::SetCurrentContext(previous);
    return false;
  }
  ImGui::SetCurrentContext(g_gui);
  D3D11_RENDER_TARGET_VIEW_DESC view{};
  view.Format = desc.Format;
  if (view.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (view.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
    view.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  view.ViewDimension =
      desc.SampleDesc.Count > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
  ComPtr<ID3D11RenderTargetView> target;
  if (FAILED(device->CreateRenderTargetView(image, &view, &target))) {
    ImGui::SetCurrentContext(previous);
    return false;
  }
  FeedInput(desc.Width, desc.Height, pointer);
  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();
  const auto status = CalibrationGetStatus();
  if (render_status) {
    ImGui::SetNextWindowPos(ImVec2(desc.Width*.12f, desc.Height*.30f));
    ImGui::SetNextWindowSize(ImVec2(desc.Width*.76f, desc.Height*.40f));
    ImGui::Begin("Restoring the view", nullptr, ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(2.0f);
    ImGui::TextUnformatted("Restoring the view");
    ImGui::Spacing();
    ImGui::TextWrapped("%s", render_status);
    ImGui::End();
  } else if (menu_visible) {
    if (status.screen_visible) CalibrationScreen(status);
    else DrawMenu();
  }
  // Popups live inside the menu window: close intent must wait for the
  // operator's choice, otherwise one frame can both request the popup and
  // observe the menu as closed.
  if (!render_status && menu_visible && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) && !ImGui::GetIO().WantTextInput) {

    if (!status.screen_visible) {
      const auto pending = RestartPendingText();
      if (!pending.empty() && pending != g_restart_ack && !g_restart_modal) {
        g_restart_modal = true;
      } else if (!g_restart_modal) {
        ModMenuClose();
      }
    }
    // On the calibration screen B does nothing: it used to cancel setup, // so a stray back press
    // silently killed the capture and triggers then did nothing until recalibration was reopened
    // (live report) . The explicit on-screen Cancel button remains the only cancellation.
  }
  // Deferred close: the toggle/B-button paths set this when the operator has
  // acknowledged the reminder but the modal frame was still open.
  if (!render_status && menu_visible && g_close_requested && !g_restart_modal) {
    g_close_requested = false;
    if (g_open.load()) ModMenuClose();
  }
  ImGui::Render();
  ID3D11RenderTargetView *saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
  ID3D11DepthStencilView *depth = nullptr;
  ID3D11Predicate *predicate = nullptr;
  BOOL predicate_value = FALSE;
  g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, &depth);
  g_context->GetPredication(&predicate, &predicate_value);
  g_context->SetPredication(nullptr, FALSE);
  if (clear_transparent || status.screen_visible) {
    const float color[] = {0, 0, 0, 0};
    g_context->ClearRenderTargetView(target.Get(), color);
  }
  ID3D11RenderTargetView *current = target.Get();
  g_context->OMSetRenderTargets(1, &current, nullptr);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  g_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, depth);
  g_context->SetPredication(predicate, predicate_value);
  for (auto *value : saved)
    if (value)
      value->Release();
  if (depth)
    depth->Release();
  if (predicate)
    predicate->Release();
  ImGui::SetCurrentContext(previous);
  return true;
}
void ModMenuShutdown() {
  if (!g_gui || g_thread != GetCurrentThreadId())
    return;
  if (g_window && IsWindow(g_window) &&
      reinterpret_cast<WNDPROC>(GetWindowLongPtrW(g_window, GWLP_WNDPROC)) == MenuWindowProc)
    SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original));
  ImGuiContext *previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(g_gui);
  ImGui_ImplDX11_Shutdown();
  ImGui::DestroyContext(g_gui);
  ImGui::SetCurrentContext(previous == g_gui ? nullptr : previous);
  g_gui = nullptr;
  g_context.Reset();
  g_device.Reset();
  g_window = nullptr;
  g_thread = 0;
  g_open.store(false);
}
} // namespace dsvr
