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
#include "../common/logging.h"
#include "calibration.h"
#include "body_visibility.h"
#include "cpu_frame_timer.h"
#include "diagnostics.h"
#include "xr.h"
#include "input.h"
#include "weapon_grip.h"
#include "arm_ik.h"
#include "combat_native.h"
#include "native_ui.h"
#include "motion_blur.h"
namespace dsvr {
// Test seam (declared in tests/test_mod_menu.cpp): the fixture arms
// probe_disabled_item, SettingsPanel runs one probe item inside the gated
// span, and records whether ImGuiItemFlags_Disabled covered it here.
// Production never arms it, so the probe item never renders there.
bool probe_disabled_item = false, item_was_disabled = false;
namespace {
using Microsoft::WRL::ComPtr;
// Ochre gold : the one accent the whole menu shares ( titles , rules , selection ) .
constexpr ImVec4 kGold{.85f, .68f, .33f, 1};
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
Config g_draft, g_draft_base;
bool g_draft_ready = false, g_dirty = false;
int g_panel = 0;
ImFont *g_title_font = nullptr;
bool g_pointer_mode = true;
ImVec2 g_navigation_pointer{};
MenuPointerSmoother g_pointer_smooth;
// The beam lives in the panel ' s own pixels: the spatial menu is a compositor
// quad, so the panel texture is the only surface the pointer can be drawn on.
bool g_beam_valid = false;
ImVec2 g_beam_from{}, g_beam_to{};
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
  // Ash - black panels , hairline ochre rules , parchment ink : the menu should read
  // as part of the game rather than as a debug overlay floating over it .
  style.WindowRounding = 2;
  style.FrameRounding = 2;
  style.GrabRounding = 2;
  style.ScrollbarRounding = 2;
  style.WindowBorderSize = 1;
  style.FrameBorderSize = 1;
  style.ChildBorderSize = 0;
  style.ScrollbarSize = 16;
  style.Colors[ImGuiCol_WindowBg] = {.043f, .041f, .039f, .98f};
  style.Colors[ImGuiCol_ChildBg] = {0, 0, 0, 0};
  style.Colors[ImGuiCol_PopupBg] = {.065f, .061f, .055f, 1};
  style.Colors[ImGuiCol_TitleBg] = {.065f, .061f, .055f, 1};
  style.Colors[ImGuiCol_TitleBgActive] = {.10f, .088f, .065f, 1};
  style.Colors[ImGuiCol_TitleBgCollapsed] = {.065f, .061f, .055f, 1};
  style.Colors[ImGuiCol_Border] = {.30f, .25f, .16f, .75f};
  style.Colors[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
  style.Colors[ImGuiCol_Text] = {.87f, .84f, .76f, 1};
  style.Colors[ImGuiCol_TextDisabled] = {.55f, .53f, .47f, 1};
  style.Colors[ImGuiCol_TextSelectedBg] = {.30f, .24f, .14f, .5f};
  style.Colors[ImGuiCol_FrameBg] = {.098f, .094f, .088f, 1};
  style.Colors[ImGuiCol_FrameBgHovered] = {.15f, .14f, .12f, 1};
  style.Colors[ImGuiCol_FrameBgActive] = {.19f, .17f, .14f, 1};
  style.Colors[ImGuiCol_Button] = {.12f, .115f, .108f, 1};
  style.Colors[ImGuiCol_ButtonHovered] = {.23f, .19f, .12f, 1};
  style.Colors[ImGuiCol_ButtonActive] = {.34f, .27f, .15f, 1};
  style.Colors[ImGuiCol_Header] = {.20f, .17f, .11f, 1};
  style.Colors[ImGuiCol_HeaderHovered] = {.30f, .24f, .14f, 1};
  style.Colors[ImGuiCol_HeaderActive] = {.38f, .30f, .16f, 1};
  style.Colors[ImGuiCol_Separator] = {.28f, .23f, .15f, .85f};
  style.Colors[ImGuiCol_CheckMark] = {.87f, .68f, .33f, 1};
  style.Colors[ImGuiCol_SliderGrab] = {.78f, .60f, .28f, 1};
  style.Colors[ImGuiCol_SliderGrabActive] = {1, .80f, .43f, 1};
  style.Colors[ImGuiCol_PlotHistogram] = {.78f, .60f, .28f, 1};
  style.Colors[ImGuiCol_ScrollbarBg] = {.05f, .048f, .045f, 1};
  style.Colors[ImGuiCol_ScrollbarGrab] = {.16f, .15f, .13f, 1};
  style.Colors[ImGuiCol_ScrollbarGrabHovered] = {.26f, .22f, .15f, 1};
  style.Colors[ImGuiCol_ScrollbarGrabActive] = {.36f, .29f, .17f, 1};
  style.Colors[ImGuiCol_NavCursor] = {1, .80f, .43f, 1};
  style.Colors[ImGuiCol_NavHighlight] = {1, .80f, .43f, 1};
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
  g_original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
  SetLastError(0);
  const auto previous =
      SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MenuWindowProc));
  if (previous)
    g_original = reinterpret_cast<WNDPROC>(previous);
  DSVR_LOG("Menu: Home opens DSVR controls; controller shortcut is left Menu + right grip");
  return true;
}
void FeedInput(unsigned width, unsigned height) {
  auto &io = ImGui::GetIO();
  const MenuPointerState pointer = XrMenuPointerInput(width, height);
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
  g_beam_valid = pointing && pointer.beam_valid;
  if (g_beam_valid) {
    g_beam_from = {pointer.beam_x, pointer.beam_y};
    g_beam_to = {px, py};
  }
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
  io.MouseDrawCursor = pointing;
}
// The pointer ' s tail is the aiming hand s own apparent position on the panel, // seen from the
// head , so the drawn segment is the trace of the ray the user reads : it keeps pointing back at
// the controller as the anchored panel drifts . // Drawn as a tapered , translucent laser rather
// than a solid rule so it orients the cursor without hiding the control it is about . Drawn on the
// background list so it sits under every widget , never over a control .
void DrawPointerBeam() {
  if (!g_beam_valid)
    return;
  auto *list = ImGui::GetBackgroundDrawList();
  if (!list)
    return;
  const float dx = g_beam_to.x - g_beam_from.x, dy = g_beam_to.y - g_beam_from.y;
  const float len = std::hypot(dx, dy);
  if (!(len > 8.0f))
    return;
  // a tail on top of the cursor draws nothing honest
  const float ux = dx / len, uy = dy / len, px = -uy, py = ux;
  const float tail = 2.6f, tip = 0.9f;
  const ImU32 halo = IM_COL32(214, 172, 96, 34), core = IM_COL32(240, 206, 140, 120);
  list->PathClear();
  list->PathLineTo({g_beam_from.x + px * tail * 2.2f, g_beam_from.y + py * tail * 2.2f});
  list->PathLineTo({g_beam_to.x + px * tip * 2.2f, g_beam_to.y + py * tip * 2.2f});
  list->PathLineTo({g_beam_to.x - px * tip * 2.2f, g_beam_to.y - py * tip * 2.2f});
  list->PathLineTo({g_beam_from.x - px * tail * 2.2f, g_beam_from.y - py * tail * 2.2f});
  list->PathFillConvex(halo);
  list->PathClear();
  list->PathLineTo({g_beam_from.x + px * tail, g_beam_from.y + py * tail});
  list->PathLineTo({g_beam_to.x + px * tip, g_beam_to.y + py * tip});
  list->PathLineTo({g_beam_to.x - px * tip, g_beam_to.y - py * tip});
  list->PathLineTo({g_beam_from.x - px * tail, g_beam_from.y - py * tail});
  list->PathFillConvex(core);
  list->AddCircle(g_beam_to, 5.0f, IM_COL32(240, 206, 140, 110), 20, 1.2f);
  list->AddCircleFilled(g_beam_to, 1.6f, IM_COL32(240, 206, 140, 150));
}
void Title(const char *text) {
  if (g_title_font)
    ImGui::PushFont(g_title_font);
  ImGui::PushStyleColor(ImGuiCol_Text, kGold);
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  if (g_title_font)
    ImGui::PopFont();
}
void CalibrationScreen(const CalibrationStatus &status) {
  const auto size = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos({0, 0});
  ImGui::SetNextWindowSize(size);
  ImGui::Begin("DSVR calibration", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings);
  const float content = std::min(860.f, std::max(320.f, size.x - 64));
  ImGui::SetCursorPos(
      {std::max(24.f, (size.x - content) * .5f), std::max(20.f, (size.y - 600) * .5f)});
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + content);
  Title("Set up your body");
  ImGui::TextWrapped(
      "Three quick steps: pick how you play below, hold your arms out, then squeeze the trigger for one second. That is all.");
  auto cfg = DiagnosticsCurrentConfig();
  int play_position = cfg.play_style == "seated" ? 1 : cfg.play_style == "roomscale" ? 2 : 0;
  ImGui::SetNextItemWidth(std::max(160.f, content - 160.f));
  if (ImGui::Combo("How you play", &play_position, "Standing\0Seated\0Walking around\0")) {
    cfg.play_style = play_position == 1 ? "seated" : play_position == 2 ? "roomscale" : "standing";
    DiagnosticsRequestConfig(cfg, true);
  }
  if (play_position == 1)
    ImGui::TextWrapped("Seated: sit how you normally sit. The game keeps your normal height.");
  else if (play_position == 2)
    ImGui::TextWrapped("Walking around: you can walk in your room. Stay inside your play area.");
  else
    ImGui::TextWrapped("Standing: stand still in one spot and turn with the stick.");
  ImGui::Spacing();
  const auto origin = ImGui::GetCursorScreenPos();
  auto *draw = ImGui::GetWindowDrawList();
  const ImU32 ink = IM_COL32(238, 238, 232, 255), accent = IM_COL32(242, 186, 90, 255);
  const float cx = origin.x + content * .5f;
  draw->AddCircle({cx, origin.y + 28}, 20, ink, 32, 3);
  draw->AddLine({cx, origin.y + 52}, {cx, origin.y + 148}, ink, 3);
  draw->AddLine({cx - content * .25f, origin.y + 74}, {cx + content * .25f, origin.y + 74}, accent,
                4);
  draw->AddCircleFilled({cx - content * .25f, origin.y + 74}, 6, accent);
  draw->AddCircleFilled({cx + content * .25f, origin.y + 74}, 6, accent);
  ImGui::Dummy({content, 168});
  ImGui::TextWrapped(
      "Look straight ahead. Pick one pose and hold it: arms straight out like a T, or relaxed down and out like an A. Keep your wrists straight and stay still. First let go of the trigger, then squeeze and hold it for one second.");
  ImGui::Spacing();
  ImGui::TextWrapped("%s", status.instruction);
  if (status.pose_hint && *status.pose_hint) {
    ImGui::PushStyleColor(ImGuiCol_Text, {1.f, .82f, .45f, 1});
    ImGui::TextWrapped("%s", status.pose_hint);
    ImGui::PopStyleColor();
  }
  ImGui::ProgressBar(status.hold_progress, {content, 12}, "");
  ImGui::TextWrapped(
      "You only do this once each time you start. Dying or loading will not erase it.");
  if (!CalibrationCurrent().floor_height_measured)
    ImGui::TextWrapped(
        "A floor reference will be used when available. Without one, the previous calibration is retained; manual height is available under Calibration in the mod menu.");
  if (status.save_failed)
    ImGui::TextWrapped("Could not save your measurements. Try again, or keep the old ones.");
  if (status.can_cancel && ImGui::Button("Skip - keep my old measurements")) {
    CalibrationCancel();
    CancelMenuInput();
  }
  ImGui::TextWrapped(
      "Your controller buttons do nothing in the game while this screen is up. The game keeps running, so stand somewhere safe.");
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  ImGui::End();
}
void CalibrationPanel() {
  Title("Calibration");
  const auto profile = CalibrationCurrent();
  const auto status = CalibrationGetStatus();
  ImGui::TextWrapped(
      "Setup opens once each launch, after gameplay loads. Deaths and loads keep the same measurements. "
      "Use Recalibrate to replace them during this session.");
  if (profile.valid) {
    ImGui::Text("Eye height  %.2f m%s", profile.eye_height_m,
                profile.floor_height_measured ? " (measured)" : " (setup value)");
    ImGui::Text("Estimated arm span  %.2f m", profile.arm_span_m);
    ImGui::Text("Estimated left / right reach  %.2f / %.2f m", profile.arm_reach_m[0],
                profile.arm_reach_m[1]);
    ImGui::Text("Saved profile revision  %llu", static_cast<unsigned long long>(profile.revision));
  }
  if (ImGui::Button("Recalibrate...")) {
    CalibrationRequest();
    ModMenuClose();
  }
  ImGui::SameLine();
  if (ImGui::Button("Recenter view")) {
    CalibrationRecenter();
    XrRecenterSanctuary();
  }
  ImGui::TextWrapped(
      "Recenter aligns the view and tracking origin. It keeps your reach and wrist calibration.");
  int position = g_draft.play_style == "seated" ? 1 : g_draft.play_style == "roomscale" ? 2 : 0;
  if (ImGui::Combo("Play position", &position, "Standing\0Seated\0Roomscale\0")) {
    g_draft.play_style = position == 1 ? "seated" : position == 2 ? "roomscale" : "standing";
    g_dirty = true;
  }
  ImGui::TextWrapped(
      "Calibration supplies eye height. Seated lift raises the view without changing measured reach or anatomical scale.");
  int fit = g_draft.embodiment_fit == "arm" ? 1 : 0;
  if (ImGui::Combo("Body fit anchored on", &fit,
                   "Native head bone (default)\0Native arm reach\0")) {
    g_draft.embodiment_fit = fit == 1 ? "arm" : "head";
    g_dirty = true;
  }
  ImGui::TextWrapped(
      "Head fit sets one automatic scale for the whole world and player so the native Head bone lands on your eye height: the camera sits in the avatar's own head and the body takes the space your body does. Arm fit instead matches your measured hand reach exactly, and the head may sit off the head bone. Either way bones and collision are not scaled, and independent avatar mesh resizing is not yet verified.");
  ImGui::Separator();
  if (g_draft.play_style == "seated") {
    g_dirty |= ImGui::SliderFloat("Seated height offset (m)", &g_draft.seated_height_offset_m,
                                  0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped("Raise the seated viewpoint to a comfortable standing height. Your saved measurements stay unchanged.");
  }
  g_dirty |= ImGui::Checkbox("Override calibrated eye height", &g_draft.eye_height_override);
  if (g_draft.eye_height_override) {
    g_dirty |= ImGui::SliderFloat("Manual eye height (m)", &g_draft.eye_height, .5f, 2.5f, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Manual placement only. Your saved body measurements and world scale stay unchanged. Turn off the override to return to calibration.");
  } else if (!profile.valid)
    ImGui::TextDisabled("Using fallback height until calibration is available.");
  g_dirty |= ImGui::SliderFloat("World / player scale trim", &g_draft.world_scale, .5f, 2.f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  g_dirty |= ImGui::SliderFloat("Avatar size (x)", &g_draft.avatar_scale, .6f, 1.6f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(
      "Avatar size multiplies the same single applied ratio as the trim above, so the head, hands, roomscale and the IK body never separate. It is not an independent mesh resize: bones, colliders and other characters are untouched.");
  g_dirty |= ImGui::SliderFloat("Wrist roll trim (degrees)", &g_draft.grip_roll_correction_deg, -45,
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
      "Theater opens automatically for the title screen, DSVR menu and calibration. Native game menus are not reliably detected yet; use F8 to toggle theater for those.");
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
    ImGui::Text("Effective target  %d FPS    Runtime cadence  %.1f Hz", rate.effective_fps,
                rate.runtime_hz);
    if (rate.runtime_throttled)
      ImGui::TextWrapped(
          "The runtime is throttling app frames. The native target stays at its stable cadence.");
  }
  static const int fps_values[] = {60, 72, 80, 90, 120, 144, 240};
  static const char *fps_labels[] = {"60", "72", "80", "90", "120", "144", "240"};
  int fps_index = 0;
  for (int i = 0; i < 7; ++i)
    if (g_draft.frame_limit_fps == fps_values[i])
      fps_index = i;
  if (ImGui::Combo("FPS fallback", &fps_index, fps_labels, 7)) {
    g_draft.frame_limit_fps = fps_values[fps_index];
    g_dirty = true;
  }
  ImGui::TextWrapped(
      "Fallback only: live runtime refresh overrides this value. Native pacing is not physics or simulation-speed qualification.");
  ImGui::Separator();
  g_dirty |= ImGui::Checkbox("Use ReShade D3D11 host (restart required)", &g_draft.reshade_host);
  ImGui::TextWrapped(
      "Present/DXGI is the default compatibility path. ReShade avoids the Present hook but needs DSVR-ReShade.addon64 and has no automatic fallback; restart after changing hosts.");
  ImGui::TextWrapped(
      "Eye-buffer resolution applies after restarting the game. The native game resolution also affects rendering cost. The presets only change eye-buffer scale; choose the highest scale that holds your frame target.");
  g_dirty |= ImGui::SliderInt("Eye resolution (%)", &g_draft.render_scale_percent, 50, 200, "%d",
                              ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::Button("Performance (70%)")) {
    g_draft.render_scale_percent = 70;
    g_dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Balanced (85%)")) {
    g_draft.render_scale_percent = 85;
    g_dirty = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Quality (100%)")) {
    g_draft.render_scale_percent = 100;
    g_dirty = true;
  }
  g_dirty |= ImGui::Checkbox("Comfort vignette", &g_draft.comfort_vignette);
  ImGui::BeginDisabled(!g_draft.comfort_vignette);
  g_dirty |= ImGui::SliderFloat("Vignette strength", &g_draft.vignette_strength, 0, 1, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  g_dirty |= ImGui::SliderFloat("Field of view (comfort)", &g_draft.fov_scale, 0.7f, 1.3f, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(
      "1.00 matches your headset. Lower narrows the view if turning feels queasy; higher widens it. Changes apply immediately and save automatically.");
  g_dirty |=
      ImGui::SliderFloat("Theater screen width (horizontal FOV)", &g_draft.mono_screen_fov_deg, 30,
                         110, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
  ImGui::TextWrapped(
      "Wider shows more of the game; narrower makes it feel like a smaller cinema screen. Aspect ratio is preserved.");
  g_dirty |= ImGui::SliderFloat("Theater distance (m)", &g_draft.menu_distance_m, 1, 4, "%.2f",
                                ImGuiSliderFlags_AlwaysClamp);
  if (ImGui::Button("Toggle theater"))
    XrToggleSanctuary();
}
void SettingsPanel() {
  Title("Comfort & body");
  const auto body = BodyVisibilityGetStatus();
  ImGui::BeginDisabled(!body.installed);
  g_dirty |= ImGui::Checkbox("Hide face, headgear and hair", &g_draft.hide_head);
  g_dirty |= ImGui::Checkbox("Hide torso and legs (arms and weapon stay)", &g_draft.hide_torso_and_legs);
  ImGui::EndDisabled();
  if (!body.installed)
    ImGui::TextWrapped("Native head geometry filter unavailable.");
  else {
    ImGui::TextWrapped(
        "Tick either row to change it immediately;untick to bring the part back. "
        "The menu view always shows the whole body, so the change appears the moment the menu closes. "
        "Arms and held weapons are never filtered.");
    ImGui::TextDisabled("Filtered %llu policy refreshes %llu refused %llu",
                        static_cast<unsigned long long>(body.filtered),
                        static_cast<unsigned long long>(body.refreshes),
                        static_cast<unsigned long long>(body.refused));
  }
  ImGui::BeginDisabled();
  ImGui::Checkbox("Forearms, hands and held equipment only", &g_draft.forearm_only);
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "Forearm visibility is unavailable while the native mesh filter is being replaced. "
      "The previous bone-collapse method stretched the upper arms and has been removed.");
  g_dirty |= ImGui::Checkbox("Hide alternate player model", &g_draft.hide_second_model);
  const auto ui = NativeUiGetStatus();
  ImGui::BeginDisabled(!ui.installed);
  g_dirty |= ImGui::Checkbox("Hide native UI in immersive VR", &g_draft.hide_ui_in_vr);
  ImGui::EndDisabled();
  if (!ui.installed)
    ImGui::TextDisabled("Native UI hook unavailable for this session.");
  ImGui::Separator();
  static const char *movement_modes[] = {"Head (default)", "Controller hand"};
  int movement_mode = static_cast<int>(g_draft.movement_orientation);
  if (ImGui::Combo("Left stick oriented by", &movement_mode, movement_modes, 2)) {
    g_draft.movement_orientation = static_cast<MovementOrientation>(movement_mode);
    g_dirty = true;
  }
  static const char *movement_controllers[] = {"Non-dominant hand", "Left hand", "Right hand"};
  int movement_controller = static_cast<int>(g_draft.movement_controller);
  ImGui::BeginDisabled(g_draft.movement_orientation != MovementOrientation::Controller);
  if (ImGui::Combo("Controller hand", &movement_controller, movement_controllers, 3)) {
    g_draft.movement_controller = static_cast<MovementController>(movement_controller);
    g_dirty = true;
  }
  ImGui::EndDisabled();
  ImGui::TextWrapped(
      "Smooth locomotion always has one live steering reference: your head, or the selected hand's aim. If that hand leaves view or points straight up/down with no stable heading, movement continues head-oriented instead of going dead. A changed reference takes effect when the stick is next centred.");
  g_dirty |= ImGui::Checkbox("Left hand dominant", &g_draft.dominant_hand_left);
  g_dirty |= ImGui::Checkbox("Snap turning", &g_draft.snap_turn);
  ImGui::BeginDisabled(!g_draft.snap_turn);
  g_dirty |= ImGui::SliderInt("Snap angle", &g_draft.snap_turn_degrees, 15, 90, "%d",
                              ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  g_dirty |= ImGui::Checkbox("Smooth turning", &g_draft.smooth_turn);
  ImGui::BeginDisabled(!g_draft.smooth_turn);
  if (probe_disabled_item) {
    // Probe button inside the same BeginDisabled span: LastItemData still
    // describes it here (ItemAdd ran inside Button), so its ItemFlags carry
    // this span's disabled state. A disabled Button returns false and never
    // changes state; production never arms this.
    ImGui::Button("##probe");
    item_was_disabled =
        (GImGui->LastItemData.ItemFlags & ImGuiItemFlags_Disabled) != 0;
    probe_disabled_item = false;
  }
  g_dirty |= ImGui::SliderFloat("Turn speed (degrees/s)", &g_draft.smooth_turn_speed_dps, 30, 360,
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
  g_dirty |= ImGui::Checkbox("Track native arms", &g_draft.arm_ik_enabled);
  g_dirty |= ImGui::Checkbox("Observe arm targets without applying them", &g_draft.arm_ik_log_only);
  g_dirty |=
      ImGui::Checkbox("Native animation cannot move IK bodyparts", &g_draft.arm_ik_block_animation);
  ImGui::Separator();
  ImGui::TextUnformatted("Controller bindings");
  g_dirty |= ImGui::Checkbox("Quest 3 / Touch", &g_draft.bind_profile_quest3);
  g_dirty |= ImGui::Checkbox("Valve Index", &g_draft.bind_profile_index);
  g_dirty |= ImGui::Checkbox("WMR motion controller", &g_draft.bind_profile_wmr);
  g_dirty |= ImGui::Checkbox("OpenXR simple controller fallback", &g_draft.bind_profile_simple);
  g_dirty |= ImGui::Checkbox("Update profile filter live", &g_draft.binding_live_reattach);
  if (ImGui::Button("Refresh profile filter now"))
    InputRequestBindingReload();
  ImGui::TextWrapped(
      "Supported OpenXR profiles are suggested at session creation; the action set is attached once, never rebuilt or reattached live. These switches filter input for each hand's current runtime profile. Apply or Save keeps the attached session live; restart the game before expecting new profile suggestions. The refresh button applies pending profile edits without re-suggesting bindings. The legacy live-update preference is retained for saved-config compatibility only.");
  ImGui::TextWrapped(
      "Simple-controller fallback is an OpenXR profile, not Xbox. Native Xbox/XInput mapping and its binding editor are separate and are not implemented here.");
  // Everyday rows above (movement, turning). The arm rows above bracketed by
  // the "Advanced tuning" label are the advanced group; the toggle rows that
  // follow here (arm swing, reach) read under the same grouping: same panel,
  // same IDs/labels/behavior — grouping only.
  ImGui::Separator();
  g_dirty |= ImGui::Checkbox("Swinging the arms asks the game to run", &g_draft.arm_swing_sprint);
  if (g_draft.arm_swing_sprint) {
    g_dirty |= ImGui::SliderFloat("Stride hand speed (m/s)", &g_draft.arm_swing_stride_min, .4f, 4,
                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderFloat("Too fast to be a stride (m/s)", &g_draft.arm_swing_stride_max,
                                  1.f, 12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderFloat("Other hand still below (m/s)", &g_draft.arm_swing_quiet_max, .1f,
                                  3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderInt("Strides before it engages", &g_draft.arm_swing_alternations, 2, 12,
                                "%d", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderInt("Run lingers for (ms)", &g_draft.arm_swing_hold_ms, 200, 3000, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
  }
  ImGui::TextWrapped(
      "Alternation is the test: a stride counts only when the other hand was still as it started, and the pattern counts only when consecutive strides swap hands. One hand flapping, a stretch, a clap or a two-handed swing never engages it, because all of those move both hands together. Speeds are head-relative, so walking the room or riding a lift cannot manufacture a gait. The output is one forward amount in the same movement basis as the stick, so native acceleration, stamina, turning and the dash stay the game's.");
  ImGui::Separator();
  g_dirty |= ImGui::Checkbox("Thrust the free hand forward to interact", &g_draft.reach_interact);
  if (g_draft.reach_interact) {
    g_dirty |= ImGui::SliderFloat("Reach distance (m)", &g_draft.reach_forward_min_m, .2f, 1.5f,
                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderFloat("Beyond this nothing counts (m)", &g_draft.reach_forward_max_m,
                                  .6f, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderFloat("Thrust speed (m/s)", &g_draft.reach_speed_min, .3f, 5, "%.2f",
                                  ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderFloat("Must be going forward (fraction)", &g_draft.reach_dominance_min,
                                  .2f, .95f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderInt("Hands must rest for (ms)", &g_draft.reach_settle_ms, 0, 1000, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderInt("Repeat interval (ms)", &g_draft.reach_cooldown_ms, 250, 3000, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
  }
  ImGui::TextWrapped(
      "No native prompt flag is bound yet, so the gesture alone cannot decide: untargeted reaches are refused and logged until a native target owner exists. Both hands have to be still first, and the other hand has to stay still through the thrust.");
}
void CombatPanel() {
  Title("Combat & feedback");
  ImGui::TextWrapped(
      "Proving ground: swing at the sparring target, hold your off-hand up as a shield, then move your weapon blade into it. Contact just after raising the guard is classified as a parry.The sparring benchis tracked geometry onlyand appliesno nativedamage;the input previews below are separate.");
  g_dirty |= ImGui::Checkbox("Combat proving ground (sparring target)", &g_draft.combat_test_bench);
  ImGui::BeginDisabled(!g_draft.combat_test_bench);
  g_dirty |= ImGui::SliderFloat("Sparring target distance (m)", &g_draft.combat_test_target_m, .5f,
                                2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  g_dirty |=
      ImGui::SliderFloat("Min hand separation (m, 0 = off)", &g_draft.weapon_separation_min_m, 0.f,
                         .5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  g_dirty |= ImGui::Checkbox("Motion combat preference", &g_draft.motion_attacks);
  g_dirty |= ImGui::Checkbox("Swing-to-native attack input (preview)", &g_draft.physical_attacks);
  ImGui::TextDisabled("Input preview only;native physical contact damage is not bound.");
  ImGui::BeginDisabled(!g_draft.physical_attacks || !g_draft.motion_attacks);
  g_dirty |= ImGui::SliderFloat("Light attack speed (m/s)", &g_draft.swing_attack_speed_min, .5f, 8,
                                "%.2f", ImGuiSliderFlags_AlwaysClamp);
  g_dirty |= ImGui::SliderFloat("Strong attack speed (m/s)", &g_draft.swing_attack_heavy_min, 1.f,
                                12, "%.2f", ImGuiSliderFlags_AlwaysClamp);
  g_dirty |= ImGui::SliderInt("Attack repeat interval (ms)", &g_draft.swing_attack_cooldown_ms, 150,
                              2000, "%d", ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  // Everyday rows above. The per-hand thresholds below (+ the grip editor
  // further down) read under one labeled always-visible group: same panel,
  // same IDs/labels/behavior — grouping only. (No collapse, no reorder: the
  // label row is unconditional so the default render still shows every
  // advanced control.)
  ImGui::Separator();
  ImGui::TextUnformatted("Advanced tuning");
  g_dirty |= ImGui::Checkbox("Raised shield hand guards, flick parries (preview)",
                             &g_draft.physical_defence);
  if (g_draft.physical_defence) {
    g_dirty |= ImGui::SliderInt("Guard settle (ms)", &g_draft.guard_settle_ms, 0, 500, "%d",
                                ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderFloat("Flick speed (m/s)", &g_draft.guard_flick_speed_min, .8f, 6,
                                  "%.2f", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderInt("Parry tap length (ms)", &g_draft.guard_parry_press_ms, 40, 250,
                                "%d", ImGuiSliderFlags_AlwaysClamp);
    g_dirty |= ImGui::SliderInt("Parry repeat interval (ms)", &g_draft.guard_parry_cooldown_ms, 250,
                                2000, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextWrapped(
        "Hold the shield hand up and still to request native guard (held LB). A short flick back to rest requests parry (a separate LT pulse, not a tap of LB). Native guard windows, chip damage and parry judgement stay the game's and remain unqualified.");
  }
  if (!g_draft.arm_ik_enabled || g_draft.arm_ik_log_only || !g_draft.arm_ik_block_animation)
    ImGui::TextDisabled("Needs fresh committed IK arms with animation ownership.");
  g_dirty |= ImGui::Checkbox("Haptic feedback", &g_draft.haptics);
  ImGui::BeginDisabled(!g_draft.haptics);
  g_dirty |= ImGui::SliderFloat("Minimum contact speed (m/s)", &g_draft.contact_speed_min, .1f, 5,
                                "%.2f", ImGuiSliderFlags_AlwaysClamp);
  g_dirty |= ImGui::SliderInt("Repeat hit interval (ms)", &g_draft.rehit_window_ms, 50, 2000, "%d",
                              ImGuiSliderFlags_AlwaysClamp);
  ImGui::EndDisabled();
  ImGui::Separator();
  {
    CombatBenchSnapshot bench;
    const bool live = ArmIkCombatBench(bench);
    if (!g_draft.combat_test_bench) {
      ImGui::TextDisabled(
          "Turn on the proving ground, Apply, then swing — live blade state appears here.");
    } else if (!live) {
      ImGui::TextDisabled("Waiting for a fresh tracked commit (headset on, arms tracked).");
    } else {
      const int weapon = g_draft.dominant_hand_left ? 0 : 1;
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
        ImGui::PushStyleColor(ImGuiCol_Text, {1.f, .45f, .4f, 1});
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
      selected = native.equipment.weapon_id[g_draft.dominant_hand_left ? 0 : 1];
      edit = {};
      edit.weapon_id = selected;
      edit.dominant_left = g_draft.dominant_hand_left;
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
        ImGui::PushStyleColor(ImGuiCol_Text, {1, .6f, .53f, 1});
      else if (entry.level == LogLevel::Warn)
        ImGui::PushStyleColor(ImGuiCol_Text, {1, .79f, .4f, 1});
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
  g_dirty |= ImGui::Checkbox("Verbose diagnostic logging", &g_draft.verbose_logging);
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
  ImGui::TextDisabled("Home: open / close     F6: recenter     F8: theater");
  ImGui::TextWrapped(
      "VR shortcut: hold left Menu + right grip. Use the left stick and A to navigate/select, or aim and pull the right trigger. B goes back.");
  ImGui::TextWrapped("The game world continues while this menu is open.");
}
// Restart-pending baseline for the render fields: reshade_host and
// render_scale_percent have no frozen attach-time snapshot (unlike the
// binding signature), so the first values this menu process draws are the
// session's startup values. Comparing the applied config against them
// shows the restart line only after one of those fields actually lands.
bool g_render_baseline_valid = false;
bool g_render_baseline_reshade = false;
int g_render_baseline_scale = 100;
bool ModMenuCaptureTextArmed() { return ImGui::GetCurrentContext() && GImGui->LogEnabled; }
void DrawMenu() {
  const auto settings = DiagnosticsSettingsSnapshot();
  if (!g_render_baseline_valid) {
    g_render_baseline_reshade = settings.applied.reshade_host;
    g_render_baseline_scale = settings.applied.render_scale_percent;
    g_render_baseline_valid = true;
  }
  if (!g_draft_ready || g_draft == g_draft_base || g_draft == settings.applied) {
    g_draft = g_draft_base = settings.applied;
    g_draft_ready = true;
    g_dirty = false;
  }
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
                                    : g_draft != settings.saved ? "Saving settings..." : "Applied and saved";
  // Brand band : the mod name in the display face , the live apply state on the
  // same line , and an ochre rule . The footer keeps the full sentence .
  {
    const float line_x = ImGui::GetCursorPosX();
    const float content_w = ImGui::GetContentRegionAvail().x;
    if (g_title_font)
      ImGui::PushFont(g_title_font);
    ImGui::PushStyleColor(ImGuiCol_Text, kGold);
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
  // Coalesce rapid edits on the diagnostic writer. Apply immediately, persist
  // automatically, and keep edits usable while a previous save is in flight.
  if (g_draft != settings.applied && DiagnosticsRequestConfig(g_draft, true)) {
    g_draft_base = g_draft;
    g_dirty = false;
  }
  if (ImGui::Button("Close"))
    ModMenuClose();
  ImGui::TextDisabled("Changes apply immediately and save automatically.");
  std::string restart_pending;
  {
    // Applied-vs-attached restart comparison: bindings freeze at session
    // attach (InputLiveBindingSignature); the render fields freeze at this
    // menu's first draw (see g_render_baseline_* above DrawMenu).
    const auto live = InputLiveBindingSignature();
    if (settings.applied.bind_profile_quest3 != live.quest3 ||
        settings.applied.bind_profile_index != live.index ||
        settings.applied.bind_profile_wmr != live.wmr ||
        settings.applied.bind_profile_simple != live.simple) {
      if (!restart_pending.empty()) restart_pending += ", ";
      restart_pending += "bindings";
    }
    if (g_render_baseline_valid &&
        settings.applied.reshade_host != g_render_baseline_reshade) {
      if (!restart_pending.empty()) restart_pending += ", ";
      restart_pending += "ReShade host";
    }
    if (g_render_baseline_valid &&
        settings.applied.render_scale_percent != g_render_baseline_scale) {
      if (!restart_pending.empty()) restart_pending += ", ";
      restart_pending += "eye resolution";
    }
    if (!restart_pending.empty())
      ImGui::TextWrapped("Restart needed: %s (restart the game to apply).",
                         restart_pending.c_str());
  }
  ImGui::TextWrapped("%s", DiagnosticsSettingsStatus().c_str());
  ImGui::End();
}
} // namespace
// namespace
void ModMenuToggle() {
  if (g_open.exchange(true))
    ModMenuClose();
}
void ModMenuClose() {
  g_open.store(false);
  WeaponGripClearPreview();
  CancelMenuInput();
}
bool ModMenuVisible() { return g_open.load() || CalibrationGetStatus().screen_visible; }
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
void ModMenuRenderTexture(ID3D11Device *device, ID3D11Texture2D *image) {
  if (!device || !image || !ModMenuVisible()) {
    g_draft_ready = false;
    return;
  }
  ComPtr<ID3D11Device> owner;
  image->GetDevice(&owner);
  if (owner.Get() != device)
    return;
  ImGuiContext *previous = ImGui::GetCurrentContext();
  if (!Initialize(device)) {
    ImGui::SetCurrentContext(previous);
    return;
  }
  ImGui::SetCurrentContext(g_gui);
  D3D11_TEXTURE2D_DESC desc{};
  image->GetDesc(&desc);
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
    return;
  }
  FeedInput(desc.Width, desc.Height);
  ImGui_ImplDX11_NewFrame();
  ImGui::NewFrame();
  const auto status = CalibrationGetStatus();
  if (status.screen_visible)
    CalibrationScreen(status);
  else
    DrawMenu();
  DrawPointerBeam();
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) && !ImGui::GetIO().WantTextInput) {

    if (!status.screen_visible)
      ModMenuClose();
    // On the calibration screen B does nothing: it used to cancel setup, // so a stray back press
    // silently killed the capture and triggers then did nothing until recalibration was reopened
    // (live report) . The explicit on-screen Cancel button remains the only cancellation.
  }
  ImGui::Render();
  ID3D11RenderTargetView *saved[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
  ID3D11DepthStencilView *depth = nullptr;
  ID3D11Predicate *predicate = nullptr;
  BOOL predicate_value = FALSE;
  g_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, saved, &depth);
  g_context->GetPredication(&predicate, &predicate_value);
  g_context->SetPredication(nullptr, FALSE);
  if (status.screen_visible) {
    const float color[] = {.055f, .065f, .075f, 1};
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
