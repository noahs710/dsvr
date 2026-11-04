// DSVR Launcher: a focused setup and launch window (no console needed).
// Double-click friendly: auto-detects the game, checks the build, edits
// DSVR.toml, installs/uninstalls with MessageBox confirmations. All heavy
// logic lives in setup.cpp (shared with the CLI).
//
// Layout is a fixed grid in 96-DPI logical units, scaled by the real display
// DPI: the process declares itself DPI-aware at startup, so text renders sharp
// instead of being bitmap-stretched by the desktop compositor. Anything that
// should follow the window width registers a right/bottom margin in Add()
// instead of a fixed size, and WM_SIZE replays every placement.

#include "gui.h"

#include <windows.h>

#include <algorithm>
#include <commdlg.h>
#include <shellapi.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "../common/config.h"
#include "../common/ui_palette.h"
#include "../common/version.h"
#include "../core/user_presets.h"
#include "launch.h"
#include "setup_internal.h"

namespace {

// Action ids first, then one contiguous block of setting ids: WM_COMMAND uses
// that range to tell a user edit from a programmatic control update.
enum {
  CTL_GAME_EDIT = 101,
  CTL_BROWSE = 102,
  CTL_DETECT = 103,
  CTL_STATUS = 104,
  CTL_LOG = 105,
  CTL_VERIFY = 106,
  CTL_DOCTOR = 107,
  CTL_INSTALL = 108,
  CTL_UNINSTALL = 109,
  CTL_SAVE = 110,
  CTL_OPEN_FOLDER = 111,
  CTL_PRESET_COMFORT = 112,
  CTL_PRESET_BALANCED = 113,
  CTL_PRESET_SHARPER = 114,
  CTL_PRESET_MATCH = 120,
  CTL_NAV_GAME = 123,
  CTL_NAV_SETTINGS = 124,
  CTL_NAV_ACTIVITY = 125,
  CTL_RESET_SAFE = 126,
  CTL_DEVMODE = 127,  // Ctrl+D accelerator: not a control, never a setting
  CTL_DEL_CFG = 117,
  CTL_CLEAR_LOG = 118,
  CTL_LAUNCH = 119,
  CTL_NAV_HOME = 151,
  CTL_LAUNCH_NATIVE = 152,
    CTL_DOWNLOAD_TPUP = 153,
    CTL_TEXTURE_SHARE = 154,
    CTL_OPEN_TPUP = 155,
    CTL_ADVANCED_TOGGLE = 157,
    CTL_ADVANCED_OPEN = 158,
    CTL_ADVANCED_RELOAD = 159,
    CTL_ADVANCED_INDEX = 160,

  CTL_VR = 130,  // legacy id: VR now lives in the CTL_MODE combo; id kept for layout
  CTL_VIGNETTE = 131,
  CTL_SNAP = 132,
  CTL_HAPTICS = 133,
  CTL_MOTION = 134,
  CTL_SCALE = 135,
  CTL_SNAPDEG = 136,
  CTL_HAND = 137,
  CTL_STYLE = 138,
  CTL_CHAIN = 139,
  CTL_MOVE_MODE = 141,
  CTL_ARM = 144,  // 142-143 retired with the merged steering/body-fit controls
  CTL_MODE = 145,  // Play / Observe / Combat-trace: one startup mode
    CTL_DEBUGCAP = 146,
    CTL_DEVTOGGLE = 147,  // visible Advanced-tuning checkbox (same action as Ctrl+D)
    CTL_VIEW = 148,       // Immersive (default) / Theater during gameplay
    CTL_BODY = 149,       // how much of the player's own body the camera sees
    CTL_FPS_UNLOCK = 150,  // action id, deliberately above kLastSetting
  };
constexpr int kFirstSetting = CTL_VR;
constexpr int kLastSetting = CTL_BODY;

HWND g_game_edit = nullptr, g_status = nullptr, g_log = nullptr, g_native_launch = nullptr;
HWND g_tpup_button = nullptr, g_texture_button = nullptr, g_tpup_open = nullptr, g_texture_status = nullptr;
HWND g_advanced_toggle = nullptr, g_advanced_open = nullptr, g_advanced_reload = nullptr,
     g_advanced_summary = nullptr;
bool g_advanced_expanded = false;
std::wstring g_tpup_folder;
HWND g_vignette = nullptr, g_snap = nullptr;
HWND g_haptics = nullptr, g_motion = nullptr;
HWND g_scale = nullptr, g_snapdeg = nullptr;
HWND g_hand = nullptr, g_style = nullptr, g_chain = nullptr;

HWND g_move_mode = nullptr;
HWND g_arm = nullptr;
HWND g_mode = nullptr;
HWND g_view = nullptr;
HWND g_body = nullptr;
HWND g_debugcap = nullptr;
HWND g_devtoggle = nullptr;
HWND g_del_cfg = nullptr;
HWND g_fps_unlock = nullptr;
HWND g_main = nullptr;
HFONT g_font = nullptr, g_title_font = nullptr;
HFONT g_section_font = nullptr;
HFONT g_hint_font = nullptr;
std::wstring g_exe;  // resolved exe path (empty = not found)
std::wstring g_status_base;  // status text without the unsaved-changes suffix
unsigned g_dpi = 96;
bool g_dirty = false;      // controls differ from what is on disk
bool g_suppress = false;   // a programmatic control update is in flight
std::wstring g_read_notes;  // fields ReadControls could not apply
HWND g_brief = nullptr, g_launch = nullptr, g_heading = nullptr;
HWND g_install = nullptr;
HWND g_nav_home = nullptr, g_nav_game = nullptr, g_nav_settings = nullptr, g_nav_activity = nullptr;
enum class SetupPage { Home, Game, Settings, Help, Shared };
SetupPage g_page = SetupPage::Home;
int g_headset_hz = 0;  // last-known panel rate, refreshed on RefreshGame
bool g_dev_mode = false;  // Ctrl+D developer view; persisted in the file
std::vector<HWND> g_dev_controls;  // tuning internals, hidden unless dev
dsvr::Config g_disk_config{};
bool g_path_resolved = false;
std::wstring g_verified_exe;
bool g_verified_ok = false;
int g_scroll_y = 0;
WNDPROC g_checkbox_proc = nullptr;
HWND g_top = nullptr;  // main window, set first in WM_CREATE for resizing
constexpr int kClientWidth = 840, kContentHeightDev = 1028;
// The heading, Launch button and page tabs are chrome: they stay pinned at the
// top while the page scrolls, so a long page can never hide the way back out.
// Every page's content begins at kChromeBottom and never enters that band.
constexpr int kChromeBottom = 76;
// The developer block is one contiguous band (advanced tuning rows). In the
// simple view every stored row at or below it shifts up by exactly kDevH and
// the window shrinks by the same amount: nothing is left as a blank gap.
constexpr int kDevTop = 490;
constexpr int kDevH = 236;
constexpr int kDevBottom = kDevTop + kDevH;  // 726: first post-block row
// Stored (dev-mode) row -> on-screen row for the current view.
int DisplayY(int stored_y) {
  return stored_y;
}
// The scrollable content height follows the view: the simplified default is
// shorter, developer mode reveals the full tuning stack.
int PageY(SetupPage page, int y, int id) {
  switch (page) {
    case SetupPage::Home: return y;
    case SetupPage::Game: return y;
    case SetupPage::Settings: return y - 214;
    case SetupPage::Help: return id == CTL_DOCTOR ? 100 : y >= 900 ? y - 800 : y;
    case SetupPage::Shared: return y;
  }
  return y;
}

int ContentHeight() {
  switch (g_page) {
    case SetupPage::Home: return 260;
    case SetupPage::Game: return 500;
    case SetupPage::Settings: return kContentHeightDev - 214 + (g_advanced_expanded ? 450 : 0);
    case SetupPage::Help: return 260;
    case SetupPage::Shared: return 260;
  }
  return 260;
}

constexpr COLORREF ToColorRef(dsvr::palette::Rgb8 color) {
  return RGB(color.r, color.g, color.b);
}
constexpr COLORREF kBackdrop = ToColorRef(dsvr::palette::background);
constexpr COLORREF kField = ToColorRef(dsvr::palette::surface);
constexpr COLORREF kRaised = ToColorRef(dsvr::palette::raised);
constexpr COLORREF kEdge = ToColorRef(dsvr::palette::border);
constexpr COLORREF kInk = ToColorRef(dsvr::palette::text);
constexpr COLORREF kMuted = ToColorRef(dsvr::palette::muted);
constexpr COLORREF kBone = ToColorRef(dsvr::palette::bone);
constexpr COLORREF kMoss = ToColorRef(dsvr::palette::moss);
constexpr COLORREF kMossDim = ToColorRef(dsvr::palette::moss_dark);
constexpr COLORREF kOnAccent = ToColorRef(dsvr::palette::ink);
constexpr COLORREF kEmber = ToColorRef(dsvr::palette::ember);
constexpr COLORREF kEmberDeep = ToColorRef(dsvr::palette::ember_dark);
// Statics created by AddHint get an id in this block: the colour handler reads
// the id to tell a quiet explanation line from a field label.
constexpr int kHintBase = 900;
HBRUSH g_backdrop_brush = nullptr, g_field_brush = nullptr;
std::vector<HWND> g_combos, g_toggles;
int g_hint_id = 0;

// A drawn group frame. BS_GROUPBOX etches a light rectangle, which would be the
// one bright thing on a black window.
struct Frame {
  int x, y, w, h, right, bottom;
  std::wstring title;
  SetupPage page;
};
std::vector<Frame> g_frames;

bool InList(const std::vector<HWND>& list, HWND hwnd) {
  for (HWND item : list) if (item == hwnd) return true;
  return false;
}

void Relayout(HWND hwnd);
void SetPage(HWND window, SetupPage page);
void ApplyDevVisibility();
void UpdateBrief();
void ApplySafeDefaults(HWND parent);
// Stored (dev-mode) frame box -> on-screen box for the current view: frames
// wholly below the developer block shift up with their rows; the one frame
// spanning the block shrinks instead of leaving a blank band.
Frame DisplayFrame(const Frame& f);

// Logical -> physical pixels for the current display.
int S(int logical) { return MulDiv(logical, (int)g_dpi, 96); }

// A control whose box is replayed on resize. right/bottom >= 0 replace the
// fixed width/height with a margin from that edge, in logical units.
struct Placement {
  HWND hwnd;
  int x, y, w, h;
  int right, bottom;
  SetupPage page;
};
std::vector<Placement> g_placements;

void GuiLog(const std::wstring& line) {
  if (!g_log) return;
  int len = GetWindowTextLengthW(g_log);
  // A long Doctor + install session can outgrow the edit control; keep the
  // newest half so the lines a support request needs never scroll away.
  if (len > 60000) {
    SendMessageW(g_log, EM_SETSEL, 0, (WPARAM)(len / 2));
    SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)L"");
    len = GetWindowTextLengthW(g_log);
  }
  SendMessageW(g_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
  std::wstring with_nl = line + L"\r\n";
  SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)with_nl.c_str());
}

void SetStatus(const std::wstring& text) {
  g_status_base = text;
  std::wstring full = g_status_base;
  if (g_headset_hz > 0)
    full += L" Last reported HMD refresh " + std::to_wstring(g_headset_hz) +
            L" Hz; DSR-FPS-Unlock applies it on next launch.";
  else
    full += L" Frame rate is automatic; no preset cap is used while refresh is unknown.";
  if (g_status) SetWindowTextW(g_status, full.c_str());
  UpdateBrief();
}

  // Instant-apply: every control writes the toml on change, so there is no
// dirty state and no Save button. These stay as no-ops for call sites.
void MarkDirty() {}
void ClearDirty() { g_dirty = false; }

setup::LogFn MakeLog() { return [](const std::wstring& s) { GuiLog(s); }; }

setup::ConfirmFn MakeConfirm(HWND parent) {
  return [parent](const wchar_t* title, const std::wstring& text,
                  bool default_yes, bool destructive) {
    UINT flags = MB_YESNO | (destructive ? MB_ICONWARNING : MB_ICONQUESTION);
    flags |= (default_yes && !destructive) ? MB_DEFBUTTON1 : MB_DEFBUTTON2;
    return MessageBoxW(parent, text.c_str(), title, flags) == IDYES;
  };
}

std::wstring GetText(HWND hwnd) {
  int n = GetWindowTextLengthW(hwnd);
  std::wstring s((size_t)(n > 0 ? n : 0) + 1, L'\0');
  GetWindowTextW(hwnd, s.data(), n + 1);
  s.resize((size_t)(n > 0 ? n : 0));
  return s;
}

void SetText(HWND hwnd, const std::wstring& s) {
  SetWindowTextW(hwnd, s.c_str());
}

bool Checked(HWND hwnd) {
  return SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
void SetChecked(HWND hwnd, bool v) {
  SendMessageW(hwnd, BM_SETCHECK, v ? BST_CHECKED : BST_UNCHECKED, 0);
}

std::wstring ConfigPathForGame() {
  if (g_exe.empty()) return {};
  return setup::JoinPath(setup::DirNameOf(g_exe), L"DSVR.toml");
}


std::wstring AdvancedSettingsIndex() {
  struct Entry { const wchar_t* section; const wchar_t* label; const wchar_t* key; };
  static constexpr Entry entries[] = {
    {L"Launcher and VR", L"Proxy loaded alongside DSVR", L"chain_proxy"},
    {L"Launcher and VR", L"Start in VR", L"vr_enabled"},
    {L"Launcher and VR", L"Use ReShade host (restart required)", L"reshade_host"},
    {L"Launcher and VR", L"Image resolution scale", L"render_scale_percent"},
    {L"Launcher and VR", L"Keep the desktop image updating", L"desktop_present"},
    {L"Launcher and VR", L"Comfort vignette", L"comfort_vignette"},
    {L"Launcher and VR", L"Vignette strength", L"vignette_strength"},
    {L"Launcher and VR", L"Field-of-view comfort scale", L"fov_scale"},
    {L"Launcher and VR", L"Snap turning", L"snap_turn"},
    {L"Launcher and VR", L"Snap-turn angle", L"snap_turn_degrees"},
    {L"Launcher and VR", L"Smooth turning", L"smooth_turn"},
    {L"Launcher and VR", L"Smooth-turn speed", L"smooth_turn_speed_dps"},
    {L"Launcher and VR", L"Head-tracked view", L"headlook_enabled"},
    {L"Launcher and VR", L"Flat screen during gameplay", L"gameplay_theater"},
    {L"Launcher and VR", L"Mono virtual screen width", L"mono_screen_fov_deg"},
    {L"Launcher and VR", L"Spatial menu panel", L"theater_override"},
    {L"Launcher and VR", L"Floor grid in theater", L"theater_floor_grid"},
    {L"Launcher and VR", L"Menu distance", L"menu_distance_m"},
    {L"Launcher and VR", L"Menu text viewing angle", L"menu_reading_fov_deg"},
    {L"Launcher and VR", L"Disable motion blur while installed", L"force_disable_motion_blur"},
    {L"Launcher and VR", L"Experimental stereo rendering (currently refused)", L"stereo_enabled"},
    {L"Launcher and VR", L"Experimental alternating-eye rendering", L"afr_enabled"},
    {L"Launcher and VR", L"Spatial anti-aliasing (Off/FXAA)", L"aa_mode"},
    {L"Launcher and VR", L"CAS sharpening", L"cas_enabled"},
    {L"Launcher and VR", L"CAS strength", L"cas_strength"},
    {L"Controls and movement", L"Weapon hand", L"dominant_hand"},
    {L"Controls and movement", L"Movement direction reference", L"movement_orientation"},
    {L"Controls and movement", L"Controller used for movement direction", L"movement_controller"},
    {L"Controls and movement", L"Standing, seated, or room-scale play", L"play_style"},
    {L"Controls and movement", L"Arm-swing running", L"arm_swing_sprint"},
    {L"Controls and movement", L"Minimum arm-swing speed", L"arm_swing_stride_min"},
    {L"Controls and movement", L"Maximum arm-swing speed", L"arm_swing_stride_max"},
    {L"Controls and movement", L"Other hand quiet-speed limit", L"arm_swing_quiet_max"},
    {L"Controls and movement", L"Hand alternations before running", L"arm_swing_alternations"},
    {L"Controls and movement", L"Run linger time", L"arm_swing_hold_ms"},
    {L"Controls and movement", L"Reach-to-interact (experimental)", L"reach_interact"},
    {L"Controls and movement", L"Minimum reach distance", L"reach_forward_min_m"},
    {L"Controls and movement", L"Maximum reach distance", L"reach_forward_max_m"},
    {L"Controls and movement", L"Minimum reach speed", L"reach_speed_min"},
    {L"Controls and movement", L"Forward reach-direction requirement", L"reach_dominance_min"},
    {L"Controls and movement", L"Quiet-hand speed limit", L"reach_quiet_max"},
    {L"Controls and movement", L"Hands-still settle time", L"reach_settle_ms"},
    {L"Controls and movement", L"Interaction button hold time", L"reach_press_ms"},
    {L"Controls and movement", L"Reach interaction cooldown", L"reach_cooldown_ms"},
    {L"Controls and movement", L"Keyboard shortcuts", L"hotkeys_enabled"},
    {L"Controls and movement", L"OpenXR controller input", L"xr_input_enabled"},
    {L"Controls and movement", L"Quest 3 controller bindings", L"bind_profile_quest3"},
    {L"Controls and movement", L"Index controller bindings", L"bind_profile_index"},
    {L"Controls and movement", L"Windows Mixed Reality bindings", L"bind_profile_wmr"},
    {L"Controls and movement", L"Basic controller fallback bindings", L"bind_profile_simple"},
    {L"Controls and movement", L"Reconnect controller bindings during a session", L"binding_live_reattach"},
    {L"Controls and movement", L"Log marker shortcut key code", L"log_marker_vk"},
    {L"Controls and movement", L"Reload-settings shortcut key code", L"config_reload_vk"},
    {L"Camera and body", L"Use manual eye height", L"eye_height_override"},
    {L"Camera and body", L"Manual eye height", L"eye_height"},
    {L"Camera and body", L"Seated view-height offset", L"seated_height_offset_m"},
    {L"Camera and body", L"World and avatar scale", L"world_scale"},
    {L"Camera and body", L"Avatar scale trim", L"avatar_scale"},
    {L"Camera and body", L"Camera forward offset", L"forward_offset_m"},
    {L"Camera and body", L"Head clearance", L"eye_clearance_m"},
    {L"Camera and body", L"Hide the full player model", L"hide_player_model"},
    {L"Camera and body", L"Hide head and headgear", L"hide_head"},
    {L"Camera and body", L"Hide torso and legs", L"hide_torso_and_legs"},
    {L"Camera and body", L"Show forearms only", L"forearm_only"},
    {L"Camera and body", L"Hide game interface in VR", L"hide_ui_in_vr"},
    {L"Camera and body", L"Hide the secondary player model", L"hide_second_model"},
    {L"Camera and body", L"Avatar fit anchor", L"embodiment_fit"},
    {L"Combat and haptics", L"Motion-driven attacks", L"motion_attacks"},
    {L"Combat and haptics", L"Legacy swing-speed threshold", L"swing_speed_min"},
    {L"Combat and haptics", L"Legacy swing-damage multiplier", L"swing_damage_gain"},
    {L"Combat and haptics", L"Allow thrust attacks", L"thrust_enabled"},
    {L"Combat and haptics", L"Legacy thrust-speed threshold", L"thrust_speed_min"},
    {L"Combat and haptics", L"Controller vibration", L"haptics"},
    {L"Combat and haptics", L"Follow controllers with avatar arms", L"arm_ik_enabled"},
    {L"Combat and haptics", L"Observe arm tracking without applying it", L"arm_ik_log_only"},
    {L"Combat and haptics", L"Prevent native animation moving tracked body parts", L"arm_ik_block_animation"},
    {L"Combat and haptics", L"Weapon grip roll adjustment", L"grip_roll_correction_deg"},
    {L"Combat and haptics", L"Minimum contact speed", L"contact_speed_min"},
    {L"Combat and haptics", L"Minimum time between hits on one enemy", L"rehit_window_ms"},
    {L"Combat and haptics", L"Physical swing attack input (unqualified)", L"physical_attacks"},
    {L"Combat and haptics", L"Light-swing speed threshold", L"swing_attack_speed_min"},
    {L"Combat and haptics", L"Strong-swing speed threshold", L"swing_attack_heavy_min"},

    {L"Combat and haptics", L"Physical shield defense input (unqualified)", L"physical_defence"},
    {L"Combat and haptics", L"Shield-hand settle time", L"guard_settle_ms"},
    {L"Combat and haptics", L"Shield flick speed threshold", L"guard_flick_speed_min"},
    {L"Combat and haptics", L"Parry button hold time", L"guard_parry_press_ms"},
    {L"Combat and haptics", L"Parry cooldown", L"guard_parry_cooldown_ms"},
    {L"Combat and haptics", L"Offline combat test bench (no native damage)", L"combat_test_bench"},
    {L"Combat and haptics", L"Test target distance", L"combat_test_target_m"},
    {L"Combat and haptics", L"Minimum separation between hands", L"weapon_separation_min_m"},
    {L"Combat and haptics", L"Weapon mass table file", L"weapon_mass_table_path"},
    {L"Combat and haptics", L"Weapon torque response", L"torque_response"},
    {L"Combat and haptics", L"Support-hand half-sword reach", L"halfsword_envelope_m"},
    {L"Combat and haptics", L"Deflection effect strength", L"deflect_amplitude"},
    {L"Combat and haptics", L"Deflection effect duration", L"deflect_duration_ms"},
    {L"Diagnostics and safety", L"Detailed diagnostic logging", L"verbose_logging"},
    {L"Diagnostics and safety", L"Desktop observer mode (startup only)", L"observer_only"},
    {L"Diagnostics and safety", L"Combat trace mode (startup only)", L"combat_trace_only"},
    {L"Diagnostics and safety", L"Autonomous VR session telemetry (startup only)", L"autonomous_session_capture"},
    {L"Diagnostics and safety", L"Show developer controls in Setup", L"dev_mode"},
    {L"Diagnostics and safety", L"Confirm offline-only use", L"offline_only_ack"},
    {L"Diagnostics and safety", L"Original motion-blur value (Setup-managed)", L"motion_blur_original"},
  };
  std::wstring text = L"All settings saved in DSVR.toml. The values are shown in that file; edit it, save, then choose Reload from file.\r\n";
  const wchar_t* section = nullptr;
  for (const auto& entry : entries) {
    if (!section || wcscmp(section, entry.section) != 0) {
      section = entry.section;
      text += L"\r\n" + std::wstring(section) + L"\r\n";
    }
    text += std::wstring(entry.label) + L"  (" + entry.key + L")\r\n";
  }
  return text;
}

// Installation and version status do not prove headset or runtime readiness.
void UpdateBrief() {
  if (!g_brief) return;
  const auto st = g_exe.empty() ? setup::InstallState{} : setup::InstallStateOf(g_exe);
  const bool verified = g_path_resolved && g_verified_exe == g_exe && g_verified_ok;
  const wchar_t* action = !g_path_resolved ? L"Find game" :
      st.partial() ? L"Repair setup" : !st.installed() ? L"Set up DSVR" :
      !verified ? L"Verify game" : L"Launch game";
  const std::wstring state = !g_path_resolved ?
      L"Find Dark Souls Remastered in Game & mods to get started." :
      st.partial() ? L"DSVR setup is incomplete. Repair it in Game & mods." :
      st.installed() ? L"DSVR is installed. Play offline. Headset and runtime have not been checked." :
      L"DSVR is not installed. Set it up in Game & mods.";
  const std::wstring version = !g_path_resolved ? L"" : verified ?
      L"Game version verified." : g_verified_exe == g_exe && !g_verified_ok ?
      L"Game version check failed. Do not install on an unsupported version." :
      L"Game version not verified.";
  SetWindowTextW(g_brief, (state + L"\r\n" + version).c_str());
  if (g_launch) {
    SetWindowTextW(g_launch, action);
    EnableWindow(g_launch, TRUE);
  }
  if (g_native_launch)
    EnableWindow(g_native_launch, g_path_resolved && !st.partial() && !st.installed());
  if (g_install) EnableWindow(g_install, verified);
}

void PopulateControls(const dsvr::Config& c) {
  // Writing a control sends its change notification, which would otherwise
  // read back as a user edit and re-save in a loop.
  g_suppress = true;
  g_disk_config = c;
  // The startup-mode combo is the one VR question: Play, Observer, or
  // Combat-trace. Observer modes force VR off.
  SendMessageW(g_mode, CB_SETCURSEL,
               c.combat_trace_only ? 2 : c.observer_only ? 1 : 0, 0);
  SetChecked(g_vignette, c.comfort_vignette);
  SetChecked(g_snap, c.snap_turn);
  SetChecked(g_haptics, c.haptics);
  SetChecked(g_motion, c.motion_attacks);
  SetText(g_scale, std::to_wstring(c.render_scale_percent));
  SetText(g_snapdeg, std::to_wstring(c.snap_turn_degrees));
  // Eye height belongs to calibration/the in-game manual override only.
  SendMessageW(g_hand, CB_SETCURSEL, c.dominant_hand_left ? 1 : 0, 0);
  // One steering combo: head, or a controller hand with head fallback.
    SendMessageW(g_move_mode, CB_SETCURSEL,
                 c.movement_orientation != dsvr::MovementOrientation::Controller ? 0 :
               c.movement_controller == dsvr::MovementController::Left ? 2 :
                 c.movement_controller == dsvr::MovementController::Right ? 3 : 1, 0);
   // Body fit is fixed (head height + arm reach): no control to populate.
  // Arm tracking is two config fields with one honest three-state control:
  // off, enabled-but-observe-only (the shipped default), and driving the model.
  SendMessageW(g_arm, CB_SETCURSEL, !c.arm_ik_enabled ? 0 : c.arm_ik_log_only ? 1 : 2,
               0);
  
  int style_idx = 0;
  if (c.play_style == "seated") style_idx = 1;
  if (c.play_style == "roomscale") style_idx = 2;
  SendMessageW(g_style, CB_SETCURSEL, style_idx, 0);
  SetText(g_chain, dsvr::WideFromUtf8(c.loader_chain_proxy));
  SetChecked(g_debugcap, c.autonomous_session_capture);
  // One gameplay-view control covers both config fields it owns. Show theater
  // whenever either field produces it, so the GUI never claims immersive while
  // the file would still render the flat screen.
  SendMessageW(g_view, CB_SETCURSEL,
               (c.gameplay_theater || !c.headlook_enabled) ? 1 : 0, 0);
  // Body visibility is three config fields with one honest four-state control.
  // A file that hides the head but not the torso still reads as "hide head",
  // so the GUI never invents a combination the file does not have.
  SendMessageW(g_body, CB_SETCURSEL,
               c.hide_player_model ? 3 :
               (c.hide_head && c.hide_torso_and_legs) ? 2 :
               c.hide_head ? 1 : 0, 0);
  SetChecked(g_devtoggle, c.dev_mode);
  // Developer view follows the file: the default window is simplified.
  g_dev_mode = c.dev_mode;
  ApplyDevVisibility();
  g_suppress = false;
  ClearDirty();
    UpdateBrief();
}

dsvr::Config ReadControls(dsvr::Config base) {
  g_read_notes.clear();
  // The startup-mode combo is the one VR question. Observer modes are
  // startup-only desktop (no XR, gameplay writes or injected input);
  // combat-only implies observer. Observer and VR are mutually exclusive
  // (ConfigValid refuses the mix): observer wins, VR is forced off.
  {
    const int mode = g_mode ? (int)SendMessageW(g_mode, CB_GETCURSEL, 0, 0) : 0;
    base.vr_enabled = mode <= 0;
    base.observer_only = mode >= 1;
    base.combat_trace_only = mode >= 2;
  }
  base.comfort_vignette = Checked(g_vignette);
  base.snap_turn = Checked(g_snap);
  base.haptics = Checked(g_haptics);
  base.motion_attacks = Checked(g_motion);
  // An empty or non-positive field keeps the value already in the file, and the
  // log says so instead of silently disagreeing with what the user typed.
  const std::wstring scale_text = GetText(g_scale);
  if (!scale_text.empty()) {
    const int scale = _wtoi(scale_text.c_str());
    if (scale > 0)
      base.render_scale_percent = scale;
    else
      g_read_notes = L"Render %: \"" + scale_text +
                     L"\" is not a positive number -- kept the previous value.";
  } else {
    g_read_notes = L"Render %: empty -- kept the previous value.";
  }
  const std::wstring deg_text = GetText(g_snapdeg);
  if (!deg_text.empty()) {
    const int deg = _wtoi(deg_text.c_str());
    if (deg > 0)
      base.snap_turn_degrees = deg;
    else
      g_read_notes += std::wstring(g_read_notes.empty() ? L"" : L"\n") + L"Snap deg: \"" +
                      deg_text + L"\" is not a positive number -- kept the previous value.";
  } else {
    g_read_notes += std::wstring(g_read_notes.empty() ? L"" : L"\n") +
                    L"Snap deg: empty -- kept the previous value.";
  }
  // Preserve the existing calibrated/manual height policy from the file.
  base.dominant_hand_left =
      SendMessageW(g_hand, CB_GETCURSEL, 0, 0) == 1;
  {
    // One steering combo: head, or a controller hand with head fallback.
    const int mode = (int)SendMessageW(g_move_mode, CB_GETCURSEL, 0, 0);
      if (mode <= 0) {
        base.movement_orientation = dsvr::MovementOrientation::Head;
      base.movement_controller = dsvr::MovementController::NonDominant;
    } else {
        base.movement_orientation = dsvr::MovementOrientation::Controller;
        base.movement_controller =
            mode == 2 ? dsvr::MovementController::Left
                      : mode == 3 ? dsvr::MovementController::Right
                                  : dsvr::MovementController::NonDominant;
    }
    // The body-fit selector is informational only: the fit is always the
    // combined head+reach anchor. Reading it keeps the control honest but
      // the value stays "head".
    base.embodiment_fit = "head";
  }
  {
    const int arm = (int)SendMessageW(g_arm, CB_GETCURSEL, 0, 0);
    if (arm == 1) {
      base.arm_ik_enabled = true;
      base.arm_ik_log_only = true;
    } else if (arm == 2) {
      base.arm_ik_enabled = true;
      base.arm_ik_log_only = false;
    } else {
      base.arm_ik_enabled = false;
    }
  }
  
  int style_idx = (int)SendMessageW(g_style, CB_GETCURSEL, 0, 0);
  base.play_style =
      style_idx == 1 ? "seated" : style_idx == 2 ? "roomscale" : "standing";
  base.loader_chain_proxy = dsvr::Utf8FromWide(GetText(g_chain));
  // Debug capture: the autonomous session trace. Off by default; writes
  // input/hand/native telemetry while playing, no video/audio.
  base.autonomous_session_capture = Checked(g_debugcap);
  // The view combo owns both fields so the GUI can never show a combination
  // that the core would not apply: immersive sets headlook on/theater off,
  // theater sets headlook on with gameplay_theater on (menus still immersive-
  // free, and the flat screen stays up during play).
  {
    const int view = g_view ? (int)SendMessageW(g_view, CB_GETCURSEL, 0, 0) : 0;
    base.headlook_enabled = true;
    base.gameplay_theater = view == 1;
  }
  // The body combo owns all three hiding fields, so the panel cannot show one
  // state while the file says another. "Hide the whole model" also sets the
  // part-filter flags: presence.cpp hides the models outright, and leaving the
  // part filter half-engaged is what made the head reappear over the eyes.
  {
    const int body = g_body ? (int)SendMessageW(g_body, CB_GETCURSEL, 0, 0) : 0;
    base.hide_player_model = body == 3;
    base.hide_head = body >= 1;
    base.hide_torso_and_legs = body >= 2;
  }
  base.dev_mode = Checked(g_devtoggle);
  return base;
}

// What the game folder currently holds, in the user's words. The same three
// states DoctorChecks reports, so the window and Doctor never disagree.
std::wstring InstallStateLine() {
  const setup::InstallState st = setup::InstallStateOf(g_exe);
  const setup::FpsUnlockState dep = setup::FpsUnlockStateOf(g_exe);
  if (g_fps_unlock) {
    // Nothing to download twice: the button reads as done once it is chained.
    EnableWindow(g_fps_unlock, !dep.installed());
    SetWindowTextW(g_fps_unlock,
        dep.installed() ? L"FPS Unlock installed" : L"Install required FPS Unlock");
  }
  const std::wstring rate = dep.installed()
      ? L" Required FPS Unlock: installed and chained."
      : (dep.chain_present || dep.ini_present
             ? L" Required FPS Unlock: partially present - press Install required FPS Unlock."
             : L" Required FPS Unlock: missing - press Install required FPS Unlock before starting.");
  if (st.partial())
    return L"Partial install: only one of dinput8.dll / DSVR.dll is present -- "
           L"run Install again, or Uninstall." + rate;
  if (st.installed())
    return L"DSVR is installed. Play offline; run Uninstall before going online." + rate;
  return L"DSVR is not installed." + rate;
}

bool DiscardChanges(HWND parent);
bool RefreshGame(bool log_it) {
  const std::wstring candidate = setup::FindGameExe(GetText(g_game_edit));
  if (candidate.empty()) {
    g_path_resolved = false;
    SetStatus(L"Game not found -- use Browse or paste the game folder, then Verify.");
    if (log_it) GuiLog(L"Game not found. No settings were reloaded or saved.");
    return false;
  }
  const bool changed = candidate != g_exe;
  if (changed && !DiscardChanges(g_game_edit ? GetParent(g_game_edit) : nullptr)) {
    SetText(g_game_edit, g_exe);
    g_path_resolved = !g_exe.empty();
    UpdateBrief();
    return false;
  }
  g_exe = candidate;
  SetText(g_game_edit, g_exe);
  g_path_resolved = true;
  // Cache the last-known panel rate for the status line. A sidecar from a
  // previous run names it; otherwise the rate is unknown, not zero.
  g_headset_hz = 0;
  {
    const std::wstring sidecar =
        setup::JoinPath(setup::DirNameOf(g_exe), L"DSVR.toml.headset-hz");
    g_headset_hz = dsvr::ReadLastHeadsetHz(sidecar.c_str());
  }
  SetStatus(InstallStateLine());
  if (log_it) {
    GuiLog(L"Game: " + g_exe);
    GuiLog(InstallStateLine());
    GuiLog(L"Settings: " + ConfigPathForGame());
  }
  if (changed) {
    dsvr::Config cfg;
    const bool loaded = dsvr::ConfigLoad(ConfigPathForGame().c_str(), cfg);
    PopulateControls(cfg);
    if (log_it && !loaded) GuiLog(L"No readable DSVR.toml -- showing defaults. Save settings to create it.");
  }
  return true;
}

// Launching is the launcher's whole point, so it says what it is about to do
// before doing it: an unsaved edit or a missing install changes the answer.
// Steam DRM: the exe cannot self-activate when Steam is already open --
  // launching the raw exe fails Steam init even offline. Route through the
  // Steam client (steam://rungameid/570940) so the client activates the app
  // and the game starts whether Steam is open or not. Falls back to the raw
  // exe only when no Steam client path is found.
  void LaunchGame(HWND parent) {
    if (!RefreshGame(false)) return;
    if (setup::IsGameRunning()) {
    GuiLog(L"The game is already running -- no second instance. F7 in game reloads settings.");
    return;
  }
  const auto install = setup::InstallStateOf(g_exe);
  if (install.partial()) {
    MessageBoxW(parent, L"Only one mod DLL is present. Repair with Install DSVR or Uninstall first.",
                L"Partial installation", MB_OK | MB_ICONWARNING);
    return;
  }
  if (install.installed() && !setup::FpsUnlockStateOf(g_exe).installed()) {
    MessageBoxW(parent,
        L"FPS Unlock is a required dependency and is not installed yet.\n\nInstall it from Game & mods (Install required FPS Unlock), then launch again.",
        L"Required dependency missing", MB_OK | MB_ICONWARNING);
    SetPage(g_top, SetupPage::Game);
    return;
  }
  if (!install.installed() && MessageBoxW(parent,
      L"DSVR is not installed. Launch the native game without the mod?",
      L"Launch without DSVR", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
  const auto launched = setup::LaunchGameProcess(g_exe, true);
  if (!launched.started) {
    SetStatus(L"Launch failed -- see activity log.");
    GuiLog(L"FAIL: the game did not start (Win32 error " +
           std::to_wstring(launched.win32_error) + L").");
    return;
  }
  GuiLog(L"Started " + g_exe + L" (pid " + std::to_wstring(launched.pid) +
         L"). Play offline while DSVR is installed.");
  SetStatus(L"Game process started; headset/runtime readiness has not been tested.");
}


void LaunchPrimary(HWND parent) {
  if (!g_path_resolved || g_exe.empty()) {
    SetPage(g_top, SetupPage::Game);
    return;
  }
  const auto install = setup::InstallStateOf(g_exe);
  if (install.partial() || !install.installed()) {
    SetPage(g_top, SetupPage::Game);
    return;
  }
  if (g_verified_exe != g_exe || !g_verified_ok) {
    if (!RefreshGame(false)) return;
    g_verified_ok = setup::VerifyExe(g_exe, MakeLog());
    g_verified_exe = g_exe;
    SetStatus(g_verified_ok ? L"Game version verified. VR headset/runtime not checked." :
        L"Game version check failed. Do not install on an unsupported version.");
    return;
  }
  LaunchGame(parent);
}

// The file is the authority: ConfigLoad clamps what it reads, so anything the
// user typed outside an allowed range comes back changed. Say which field and
// the range instead of letting the controls quietly disagree with the keystrokes.
void ReportAdjustments(const dsvr::Config& asked, const dsvr::Config& saved) {
  if (asked.render_scale_percent != saved.render_scale_percent)
    GuiLog(L"Adjusted render scale " + std::to_wstring(asked.render_scale_percent) +
           L" -> " + std::to_wstring(saved.render_scale_percent) + L" (allowed 50..200).");
  if (asked.snap_turn_degrees != saved.snap_turn_degrees)
    GuiLog(L"Adjusted snap turn " + std::to_wstring(asked.snap_turn_degrees) + L" -> " +
           std::to_wstring(saved.snap_turn_degrees) + L" degrees (allowed 15..90).");
  if (asked.play_style != saved.play_style)
    GuiLog(L"Adjusted play style to \"" + dsvr::WideFromUtf8(saved.play_style) +
           L"\" (standing/seated/roomscale).");
  if (asked.embodiment_fit != saved.embodiment_fit)
    GuiLog(L"Adjusted body fit to \"" + dsvr::WideFromUtf8(saved.embodiment_fit) +
           L"\" (head/arm).");
  if (asked.loader_chain_proxy != saved.loader_chain_proxy)
    GuiLog(L"Adjusted the proxy filename: it cannot contain quotes, newlines or "
           L"carriage returns.");
}

// Instant-apply: read the controls, clamp the file cap to the last-known
  // headset rate (headset is the authority, no popup), save, and re-show
  // exactly what the toml holds. Every control change calls this; there is
  // no Save button and no dirty state.
bool SaveSettings(bool silent) {
  if (g_exe.empty() || setup::FindGameExe(GetText(g_game_edit)) != g_exe) {
    GuiLog(L"No game folder found -- nothing was saved. Use Browse or Auto-detect first.");
    return false;
  }
  std::wstring cfg_path = ConfigPathForGame();
  dsvr::Config cfg;
  dsvr::ConfigLoad(cfg_path.c_str(), cfg);
  cfg = ReadControls(cfg);
  if (!g_read_notes.empty()) GuiLog(g_read_notes);
  const dsvr::Config asked = cfg;
  if (!dsvr::ConfigSave(cfg_path.c_str(), cfg)) {
    GuiLog(L"FAIL: cannot write DSVR.toml (check permissions). Fix permissions and retry the control.");
    return false;
  }
  dsvr::Config check;
  if (!dsvr::ConfigLoad(cfg_path.c_str(), check)) {
    GuiLog(L"FAIL: settings written but could not be read back. Verify DSVR.toml.");
    return false;
  }
  PopulateControls(check);
  ReportAdjustments(asked, check);
  // The advanced list mirrors the file, so an instant-apply edit must refresh it.
  if (g_advanced_expanded && g_advanced_summary)
    SetText(g_advanced_summary, AdvancedSettingsIndex());
  if (!silent)
    GuiLog(L"Saved " + cfg_path + L". Restart the game to apply (F7 reloads the live-tunable ones).");
  return true;
}

// One control changed: save immediately and re-show the toml truth.
// Programmatic repopulation suppresses its own echo via g_suppress.
void ApplyControlNow(HWND /*hwnd*/) {
  if (g_suppress || g_exe.empty()) return;
  SaveSettings(true);
  UpdateBrief();
}

HFONT MakeFont(int points, int weight, const wchar_t* face = L"Segoe UI") {
  return CreateFontW(-MulDiv(points, (int)g_dpi, 72), 0, 0, 0, weight, FALSE,
      FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void RecreateFonts() {
  HFONT body = MakeFont(10, FW_NORMAL);
  HFONT title = MakeFont(18, FW_SEMIBOLD);
  HFONT section = MakeFont(11, FW_SEMIBOLD);
  HFONT hint = MakeFont(9, FW_NORMAL);
  if (!body || !title || !section || !hint) {
    DeleteObject(body); DeleteObject(title);
    DeleteObject(section); DeleteObject(hint);
    return;
  }
  HFONT old_body = g_font, old_title = g_title_font;
  HFONT old_section = g_section_font, old_hint = g_hint_font;
  g_font = body; g_title_font = title;
  g_section_font = section; g_hint_font = hint;
  for (const auto& p : g_placements) {
    const int id = GetDlgCtrlID(p.hwnd);
    HFONT face = p.hwnd == g_heading ? title :
        (id >= kHintBase ? hint : body);
    SendMessageW(p.hwnd, WM_SETFONT, (WPARAM)face, TRUE);
  }
  for (HWND combo : g_combos) {
    SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, S(20));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, S(20));
  }
  DeleteObject(old_body); DeleteObject(old_title);
  DeleteObject(old_section); DeleteObject(old_hint);
}

void AdjustFrame(RECT& rect) {
  using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
  const auto adjust = reinterpret_cast<AdjustForDpi>(GetProcAddress(
      GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
  const DWORD style = WS_OVERLAPPEDWINDOW | WS_VSCROLL;
  if (adjust) adjust(&rect, style, FALSE, 0, g_dpi);
  else AdjustWindowRectEx(&rect, style, FALSE, 0);
}

void AddFrame(const wchar_t* title, int x, int y, int w, int h, int right, int bottom);
LRESULT CALLBACK CheckboxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id, int right = -1, int bottom = -1) {
  const bool button = wcscmp(cls, L"BUTTON") == 0;
  const bool toggle = button && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX;
  if (button && (style & BS_TYPEMASK) == BS_GROUPBOX) {
    AddFrame(text, x, y, w, h, right, bottom);
    return nullptr;
  }
  if (button) {
    style |= WS_TABSTOP;
    if (!toggle) style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
  }
  const bool combo = wcscmp(cls, L"COMBOBOX") == 0;
  if (wcscmp(cls, L"EDIT") == 0 || combo) style |= WS_TABSTOP;
  if (combo) style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
  HWND control = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style,
      S(x), S(y), S(w), S(h), parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
  SendMessageW(control, WM_SETFONT, (WPARAM)g_font, TRUE);
  if (combo) {
    g_combos.push_back(control);
    SendMessageW(control, CB_SETITEMHEIGHT, (WPARAM)-1, S(20));
    SendMessageW(control, CB_SETITEMHEIGHT, 0, S(20));
  }
  if (toggle) {
    g_toggles.push_back(control);
    const auto previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(control,
        GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CheckboxProc)));
    if (!g_checkbox_proc) g_checkbox_proc = previous;
  }
  const SetupPage page = id == CTL_LAUNCH || id == CTL_NAV_HOME ||
      id == CTL_NAV_GAME || id == CTL_NAV_SETTINGS || id == CTL_NAV_ACTIVITY ||
      (y < 60 && id != CTL_FPS_UNLOCK) ? SetupPage::Shared
      : id == CTL_STATUS ? SetupPage::Home
      : id == CTL_DOCTOR || id == CTL_CLEAR_LOG ? SetupPage::Help
      : id == CTL_FPS_UNLOCK || id == CTL_LAUNCH_NATIVE ||
        id == CTL_DOWNLOAD_TPUP || id == CTL_TEXTURE_SHARE || id == CTL_OPEN_TPUP ||
        id == 156 || id == 153 || id == 154 || id == 155 ? SetupPage::Game
      : id == CTL_ADVANCED_TOGGLE || id == CTL_DEVTOGGLE || id == CTL_ADVANCED_OPEN ||
        id == CTL_ADVANCED_RELOAD || id == CTL_ADVANCED_INDEX ? SetupPage::Settings
      : y < 280 ? SetupPage::Game : y < 916 ? SetupPage::Settings : SetupPage::Help;
  g_placements.push_back({control, x, y, w, h, right, bottom, page});
  return control;
}

void AddItems(HWND combo, std::initializer_list<const wchar_t*> items) {
  for (const wchar_t* item : items) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)item);
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

// A quiet explanation line: same control, muted ink, told apart by its id block.
HWND AddHint(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
  HWND hint = Add(parent, L"STATIC", text, SS_LEFT, x, y, w, h, kHintBase + g_hint_id++);
  if (g_hint_font) SendMessageW(hint, WM_SETFONT, (WPARAM)g_hint_font, TRUE);
  return hint;
}

void AddFrame(const wchar_t* title, int x, int y, int w, int h, int right,
              int bottom) {
  const SetupPage page = y < 280 ? SetupPage::Game :
      y < 916 ? SetupPage::Settings : SetupPage::Help;
  g_frames.push_back({x, y, w, h, right, bottom, title, page});
}

void DrawFrames(HDC dc, const RECT& client) {
  HFONT old_font = (HFONT)SelectObject(dc, g_section_font ? g_section_font : g_font);
  SetBkMode(dc, TRANSPARENT);
  for (const auto& stored : g_frames) {
    if (stored.page != g_page ||
        (g_page == SetupPage::Settings && stored.y == kDevTop && !g_dev_mode))
      continue;
    const Frame f = DisplayFrame(stored);
    const int x = S(f.x), y = S(f.y) + S(9) - g_scroll_y;
    const int w = f.right >= 0 ? client.right - S(f.x + f.right) : S(f.w);
    const int content_h = client.bottom > S(ContentHeight()) ? client.bottom : S(ContentHeight());
    const int h = f.bottom >= 0 ? content_h - S(f.y + f.bottom) : S(f.h);
    if (w <= S(40) || h <= S(24)) continue;
    HPEN pen = CreatePen(PS_SOLID, 1, kEdge);
    HPEN old_pen = (HPEN)SelectObject(dc, pen);
    HBRUSH old_fill = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, x, y, x + w, y + h, S(6), S(6));
    SelectObject(dc, old_pen);
    SelectObject(dc, old_fill);
    DeleteObject(pen);
    SIZE size{};
    GetTextExtentPoint32W(dc, f.title.c_str(), (int)f.title.size(), &size);
    const RECT label{x + S(10), y - S(3), x + S(10) + size.cx + S(8),
                     y - S(3) + size.cy + S(6)};
    FillRect(dc, &label, g_backdrop_brush);
    SetTextColor(dc, kBone);
    TextOutW(dc, x + S(14), y - S(3), f.title.c_str(), (int)f.title.size());
  }
  SelectObject(dc, old_font);
}

// Push buttons use owner draw. Checkboxes retain BS_AUTOCHECKBOX and their
// native state/accessibility/Space-key semantics; only their paint is replaced.
void DrawButton(LPDRAWITEMSTRUCT item) {
  const bool toggle = InList(g_toggles, item->hwndItem);
  const bool checked = toggle &&
      SendMessageW(item->hwndItem, BM_GETCHECK, 0, 0) == BST_CHECKED;
  const bool pressed = (item->itemState & ODS_SELECTED) != 0;
  const bool disabled = (item->itemState & ODS_DISABLED) != 0;
  const bool focused = (item->itemState & ODS_FOCUS) != 0;
  const int id = item->CtlID;
  const bool primary = id == CTL_LAUNCH;
  const bool selected = (id == CTL_NAV_HOME && g_page == SetupPage::Home) ||
      (id == CTL_NAV_GAME && g_page == SetupPage::Game) ||
      (id == CTL_NAV_SETTINGS && g_page == SetupPage::Settings) ||
      (id == CTL_NAV_ACTIVITY && g_page == SetupPage::Help);
  HDC dc = item->hDC;
  const int saved_dc = SaveDC(dc);
  SelectObject(dc, g_font);
  const RECT box = item->rcItem;
  const COLORREF hi = primary || selected ? kEmber : kBone;
  const COLORREF lo = primary || selected ? kEmberDeep : kMossDim;
  const COLORREF fill = pressed ? lo : primary || selected ? kRaised : kField;
  HPEN pen = CreatePen(PS_SOLID, 1, disabled ? kEdge : (primary || selected || pressed) ? hi : kEdge);
  HBRUSH brush = CreateSolidBrush(fill);
  HPEN old_pen = (HPEN)SelectObject(dc, pen);
  HBRUSH old_fill = (HBRUSH)SelectObject(dc, brush);
  RoundRect(dc, box.left, box.top, box.right, box.bottom, 4, 4);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_fill);
  DeleteObject(pen);
  DeleteObject(brush);
  int text_x = box.left + S(9);
  if (toggle) {
    const int side = S(15);
    const int cy = (box.top + box.bottom) / 2;
    const RECT mark{box.left + S(9), cy - side / 2, box.left + S(9) + side,
                    cy + side / 2};
    HBRUSH mark_fill = CreateSolidBrush(checked ? kMoss : kBackdrop);
    HPEN mark_pen = CreatePen(PS_SOLID, 1, checked ? kMoss : kMuted);
    old_pen = (HPEN)SelectObject(dc, mark_pen);
    old_fill = (HBRUSH)SelectObject(dc, mark_fill);
    Rectangle(dc, mark.left, mark.top, mark.right, mark.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_fill);
    DeleteObject(mark_fill);
    DeleteObject(mark_pen);
    if (checked) {
      HPEN ink = CreatePen(PS_SOLID, MulDiv(2, (int)g_dpi, 96), kOnAccent);
      HPEN old = (HPEN)SelectObject(dc, ink);
      MoveToEx(dc, mark.left + S(3), cy, nullptr);
      LineTo(dc, mark.left + side / 2, mark.bottom - S(4));
      LineTo(dc, mark.right - S(3), mark.top + S(3));
      SelectObject(dc, old);
      DeleteObject(ink);
    }
    text_x = mark.right + S(8);
  }
  wchar_t text[256]{};
  GetWindowTextW(item->hwndItem, text, 256);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, disabled ? kMuted : kInk);
  RECT label{text_x, box.top, box.right - S(6), box.bottom};
  UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
  if (item->itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
  DrawTextW(dc, text, -1, &label, flags);
  if (focused && !(item->itemState & ODS_NOFOCUSRECT)) {
    RECT focus = box;
    InflateRect(&focus, -S(3), -S(3));
    SetTextColor(dc, hi);
    DrawFocusRect(dc, &focus);
  }
  RestoreDC(dc, saved_dc);
}

LRESULT CALLBACK CheckboxProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
    PAINTSTRUCT paint{};
    HDC dc = msg == WM_PAINT ? BeginPaint(hwnd, &paint) : reinterpret_cast<HDC>(wp);
    DRAWITEMSTRUCT item{};
    item.CtlType = ODT_BUTTON;
    item.CtlID = GetDlgCtrlID(hwnd);
    item.hwndItem = hwnd;
    item.hDC = dc;
    GetClientRect(hwnd, &item.rcItem);
    const auto state = SendMessageW(hwnd, BM_GETSTATE, 0, 0);
    const auto cues = SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0);
    if (state & BST_PUSHED) item.itemState |= ODS_SELECTED;
    if (GetFocus() == hwnd) item.itemState |= ODS_FOCUS;
    if (!IsWindowEnabled(hwnd)) item.itemState |= ODS_DISABLED;
    if (cues & UISF_HIDEFOCUS) item.itemState |= ODS_NOFOCUSRECT;
    if (cues & UISF_HIDEACCEL) item.itemState |= ODS_NOACCEL;
    DrawButton(&item);
    if (msg == WM_PAINT) EndPaint(hwnd, &paint);
    return 0;
  }
  const auto result = CallWindowProcW(g_checkbox_proc, hwnd, msg, wp, lp);
  if (msg == BM_SETCHECK || msg == BM_SETSTATE || msg == WM_SETFOCUS ||
      msg == WM_KILLFOCUS || msg == WM_ENABLE || msg == WM_UPDATEUISTATE)
    InvalidateRect(hwnd, nullptr, FALSE);
  return result;
}

void Relayout(HWND hwnd) {
  RECT rc{};
  GetClientRect(hwnd, &rc);
  const int content_h = rc.bottom > S(ContentHeight()) ? rc.bottom : S(ContentHeight());
  SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
  scroll.nMax = content_h - 1;
  scroll.nPage = rc.bottom;
  const int max_scroll = content_h - rc.bottom;
  if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
  if (g_scroll_y < 0) g_scroll_y = 0;
  scroll.nPos = g_scroll_y;
  SetScrollInfo(hwnd, SB_VERT, &scroll, TRUE);
  GetClientRect(hwnd, &rc);
  for (const auto& p : g_placements) {
    const bool dev_row = InList(g_dev_controls, p.hwnd);
    const int id = GetDlgCtrlID(p.hwnd);
    const bool dev_hidden = !g_dev_mode && dev_row;
    const bool advanced_hidden = !g_advanced_expanded &&
        (id == CTL_DEVTOGGLE || id == CTL_ADVANCED_OPEN ||
         id == CTL_ADVANCED_RELOAD || id == CTL_ADVANCED_INDEX);
    const bool chrome = p.page == SetupPage::Shared;
    const bool page_visible = chrome || p.page == g_page;
    bool visible = page_visible && !dev_hidden && !advanced_hidden;
    // Chrome keeps its stored row: no page remap, no scroll offset.
    const int y = chrome ? p.y : PageY(g_page, p.y, id);
    const int w = p.right >= 0 ? rc.right - S(p.x + p.right) : S(p.w);
    const int h = p.bottom >= 0 ? content_h - S(y + p.bottom) : S(p.h);
    // Scrolling moves content only. A row that would sit on the pinned strip
    // is hidden instead of drawn over the page tabs.
      const int top = S(y) - (chrome ? 0 : g_scroll_y);
    if (!chrome && top < S(kChromeBottom)) visible = false;
    ShowWindow(p.hwnd, visible ? SW_SHOW : SW_HIDE);
    if (!visible) continue;
    MoveWindow(p.hwnd, S(p.x), top, w > 0 ? w : 1, h > 0 ? h : 1, TRUE);
  }
  InvalidateRect(hwnd, nullptr, TRUE);
}

// Stored (dev-mode) frame box -> on-screen box for the current view: frames
// wholly below the developer block shift up with their rows; the one frame
// spanning the block shrinks instead of leaving a blank band.
Frame DisplayFrame(const Frame& f) {
  Frame shown = f;
  shown.y = PageY(g_page, f.y, 0);
  return shown;
}

// Developer-only controls hide in the default simplified view. Registering
// happens at creation; ApplyDevVisibility runs on populate and on toggle.
// The window itself shrinks: relayout rides every stored row up by the
// collapsed band and resizes the frame so no blank gap remains.
void ApplyDevVisibility() {
  for (HWND control : g_dev_controls)
    ShowWindow(control, g_dev_mode ? SW_SHOW : SW_HIDE);
  if (!g_top) return;
  g_scroll_y = 0;
  Relayout(g_top);
  RECT want{0, 0, S(kClientWidth), S(ContentHeight())};
  AdjustFrame(want);
  SetWindowPos(g_top, nullptr, 0, 0, want.right - want.left,
               want.bottom - want.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  Relayout(g_top);
}

// Instant-apply: nothing is ever unsaved, so navigation never prompts.
  bool DiscardChanges(HWND parent) {
    (void)parent;
    return true;
}

HWND AddDev(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
            int x, int y, int w, int h, int id, int right = -1,
            int bottom = -1) {
  HWND control = Add(parent, cls, text, style, x, y, w, h, id, right, bottom);
  if (control) g_dev_controls.push_back(control);
  return control;
}

void ToggleDevMode(HWND parent) {
  if (g_exe.empty()) {
    // No file to persist to: flip the view for this session only.
    g_dev_mode = !g_dev_mode;
    ApplyDevVisibility();
    UpdateBrief();
    return;
  }
  dsvr::Config cfg;
  const std::wstring path = ConfigPathForGame();
  dsvr::ConfigLoad(path.c_str(), cfg);
  cfg.dev_mode = !g_dev_mode;
  if (!dsvr::ConfigSave(path.c_str(), cfg)) {
    GuiLog(L"FAIL: developer view not saved. Check DSVR.toml permissions.");
    return;
  }
  g_dev_mode = cfg.dev_mode;
  SetChecked(g_devtoggle, g_dev_mode);
  ApplyDevVisibility();
  UpdateBrief();
    GuiLog(g_dev_mode ? L"Developer view: all tuning controls visible."
                      : L"Simplified view: tuning internals hidden.");
  (void)parent;
}

void ApplyPreset(int id) {
  if (g_exe.empty() || setup::FindGameExe(GetText(g_game_edit)) != g_exe) {
    GuiLog(L"Resolve the game with Browse or Verify before saving a preset.");
    return;
  }
  dsvr::Config cfg;
  const std::wstring path = ConfigPathForGame();
  dsvr::ConfigLoad(path.c_str(), cfg);
  cfg = ReadControls(cfg);
  if (!g_read_notes.empty()) GuiLog(g_read_notes);
  if (id == CTL_PRESET_MATCH) {
    const dsvr::HardwareSpec spec = setup::ProbeHardware();
    const dsvr::HardwareTier tier = dsvr::TierForHardware(spec);
    const wchar_t* tier_name = tier == dsvr::HardwareTier::kQuality ? L"quality" :
        tier == dsvr::HardwareTier::kBalanced ? L"balanced" : L"performance";
    GuiLog(std::wstring(L"Hardware: ") + std::to_wstring(spec.system_ram_gb) +
        L" GB RAM, " + std::to_wstring(spec.gpu_vram_gb) + L" GB VRAM" +
        (spec.discrete_gpu ? L", discrete GPU" : L", no discrete GPU") +
        L" -> " + tier_name + L" fit (eye scale and AA only; refresh stays automatic).");
    dsvr::ApplyMatchPcPreset(cfg, tier);
  } else {
    const dsvr::UserPreset preset = id == CTL_PRESET_COMFORT
        ? dsvr::UserPreset::kComfort
        : id == CTL_PRESET_BALANCED ? dsvr::UserPreset::kBalanced
                                    : dsvr::UserPreset::kSharper;
    dsvr::ApplyUserPreset(cfg, preset);
  }
  if (!dsvr::ConfigValid(cfg) || !dsvr::ConfigSave(path.c_str(), cfg)) {
    GuiLog(L"FAIL: preset not saved. Check DSVR.toml permissions; edits retained.");
    return;
  }
  dsvr::Config saved;
  if (!dsvr::ConfigLoad(path.c_str(), saved)) {
    GuiLog(L"FAIL: preset written but could not be read back. Verify DSVR.toml.");
    return;
  }
  PopulateControls(saved);
  ReportAdjustments(cfg, saved);
  GuiLog(L"Preset applied and saved to " + path);
}

// Reset to safe defaults: the factory posture for the average operator.
// Overwrites the file (never merges with a draft), then shows exactly that.
void ApplySafeDefaults(HWND parent) {
  if (g_exe.empty() || setup::FindGameExe(GetText(g_game_edit)) != g_exe) {
    GuiLog(L"Resolve the game with Browse or Verify before resetting to safe defaults.");
    return;
  }
  if (MessageBoxW(parent,
      L"Reset all settings to the safe factory defaults? Your current edits will be replaced.",
      L"Reset to safe defaults", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
    return;
  const dsvr::Config safe = dsvr::FactorySafeDefaults();
  const std::wstring path = ConfigPathForGame();
  if (!dsvr::ConfigSave(path.c_str(), safe)) {
    GuiLog(L"FAIL: safe defaults not saved. Check DSVR.toml permissions; edits retained.");
    return;
  }
  PopulateControls(safe);
  GuiLog(L"Safe defaults applied and saved to " + path);
}

void ResizeContent(HWND window) {
  RECT want{0, 0, S(kClientWidth), S(ContentHeight())};
  AdjustFrame(want);
  RECT work{};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
    want.bottom = std::min(want.bottom, work.bottom - work.top);
  SetWindowPos(window, nullptr, 0, 0, want.right - want.left, want.bottom - want.top,
      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  Relayout(window);
}

void SetPage(HWND window, SetupPage page) {
  g_page = page;
  g_scroll_y = 0;
  ResizeContent(window);
  for (HWND tab : {g_nav_home, g_nav_game, g_nav_settings, g_nav_activity})
    if (tab) InvalidateRect(tab, nullptr, TRUE);
}


void ToggleAdvancedSettings(HWND window) {
  g_advanced_expanded = !g_advanced_expanded;
  SetWindowTextW(g_advanced_toggle, g_advanced_expanded ?
      L"Hide advanced settings" : L"Show all saved settings");
  if (g_advanced_expanded && g_advanced_summary)
    SetText(g_advanced_summary, AdvancedSettingsIndex());
  ResizeContent(window);
}


void OpenAdvancedConfig(HWND parent) {
  if (!RefreshGame(false)) return;
  const std::wstring path = ConfigPathForGame();
  if (!setup::PathExists(path)) {
    if (MessageBoxW(parent, L"Create a default DSVR.toml next to the game so you can edit every saved option?",
        L"Create settings file", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    if (!dsvr::ConfigSaveDefaults(path.c_str())) {
      GuiLog(L"FAIL: could not create DSVR.toml. Check the game folder permissions.");
      return;
    }
  }
  if ((INT_PTR)ShellExecuteW(parent, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) <= 32 &&
      (INT_PTR)ShellExecuteW(parent, L"open", L"notepad.exe", path.c_str(), nullptr, SW_SHOWNORMAL) <= 32) {
    GuiLog(L"FAIL: no text editor could open " + path);
    return;
  }
  GuiLog(L"Opened DSVR.toml. Save your edits there, then choose Reload from file.");
}

void ReloadAdvancedConfig() {
  if (!RefreshGame(false)) return;
  const std::wstring path = ConfigPathForGame();
  dsvr::Config cfg;
  if (!dsvr::ConfigLoad(path.c_str(), cfg)) {
    GuiLog(L"FAIL: DSVR.toml could not be loaded. Your current controls were left unchanged.");
    return;
  }
  PopulateControls(cfg);
  if (g_advanced_expanded && g_advanced_summary)
    SetText(g_advanced_summary, AdvancedSettingsIndex());
  GuiLog(L"Reloaded saved settings from " + path + L".");
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_top = hwnd;
      g_heading = Add(hwnd, L"STATIC", L"DSVR", SS_LEFT, 20, 10, 300, 34, 0);
      SendMessageW(g_heading, WM_SETFONT, (WPARAM)g_title_font, TRUE);
      g_launch = Add(hwnd, L"BUTTON", L"Launch in VR", BS_PUSHBUTTON, 650, 12, 174, 32, CTL_LAUNCH);
      AddHint(hwnd, L"Play offline while DSVR is installed", 20, 46, 460, 22);
      g_nav_home = Add(hwnd, L"BUTTON", L"Home", BS_PUSHBUTTON, 496, 46, 62, 24, CTL_NAV_HOME);
      g_nav_game = Add(hwnd, L"BUTTON", L"Game & mods", BS_PUSHBUTTON, 562, 46, 96, 24, CTL_NAV_GAME);
      g_nav_settings = Add(hwnd, L"BUTTON", L"Mod settings", BS_PUSHBUTTON, 662, 46, 92, 24, CTL_NAV_SETTINGS);
      g_nav_activity = Add(hwnd, L"BUTTON", L"Help", BS_PUSHBUTTON, 758, 46, 66, 24, CTL_NAV_ACTIVITY);
      g_frames.push_back({16, 76, 808, 170, 16, -1, L"Your setup", SetupPage::Home});
      g_frames.push_back({16, 76, 808, 180, 16, -1, L"Help & checks", SetupPage::Help});
        g_frames.push_back({16, 360, 808, 116, 16, -1, L"Optional texture tools", SetupPage::Game});
      Add(hwnd, L"BUTTON", L"Game & setup", BS_GROUPBOX, 16, 76, 808, 270, 0, 16);
      g_game_edit = Add(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 28, 104, 548, 26, CTL_GAME_EDIT);
      Add(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 586, 104, 100, 26, CTL_BROWSE);
      Add(hwnd, L"BUTTON", L"Auto-detect", BS_PUSHBUTTON, 696, 104, 116, 26, CTL_DETECT);
      g_status = Add(hwnd, L"STATIC", L"Detecting game...", SS_LEFT | SS_PATHELLIPSIS, 28, 138, 784, 20, CTL_STATUS, 28);
      g_brief = Add(hwnd, L"STATIC", L"", SS_LEFT, 28, 160, 784, 68, kHintBase + g_hint_id++, 28);
      g_placements.back().page = SetupPage::Home;
      Add(hwnd, L"BUTTON", L"Check game version", BS_PUSHBUTTON, 28, 234, 108, 28, CTL_VERIFY);
      Add(hwnd, L"BUTTON", L"Run full check", BS_PUSHBUTTON, 144, 234, 84, 28, CTL_DOCTOR);
      g_install = Add(hwnd, L"BUTTON", L"Install DSVR", BS_PUSHBUTTON, 236, 234, 120, 28, CTL_INSTALL);
      Add(hwnd, L"BUTTON", L"Remove DSVR", BS_PUSHBUTTON, 364, 234, 100, 28, CTL_UNINSTALL);
      Add(hwnd, L"BUTTON", L"Open folder", BS_PUSHBUTTON, 472, 234, 110, 28, CTL_OPEN_FOLDER);
      g_fps_unlock = Add(hwnd, L"BUTTON", L"Install required FPS Unlock", BS_PUSHBUTTON, 28, 270, 204, 26,
                         CTL_FPS_UNLOCK);
    g_native_launch = Add(hwnd, L"BUTTON", L"Launch without DSVR", BS_PUSHBUTTON, 240, 270, 226, 26,
                         CTL_LAUNCH_NATIVE);
        AddHint(hwnd, L"TPUP helps unpack custom textures. Setup never installs textures or changes game files. Save the encrypted texture ZIP to Downloads, unzip it yourself, then import it in TPUP.", 28, 390, 778, 34);
        // The hint sits in the Game page's y band, so the id-based default
        // would land it on the VR settings page and collide with its rows.
        g_placements.back().page = SetupPage::Game;
        g_tpup_button = Add(hwnd, L"BUTTON", L"Download and unzip TPUP", BS_PUSHBUTTON, 28, 430, 230, 26, CTL_DOWNLOAD_TPUP);
        g_texture_button = Add(hwnd, L"BUTTON", L"Open texture download", BS_PUSHBUTTON, 270, 430, 250, 26, CTL_TEXTURE_SHARE);
        g_tpup_open = Add(hwnd, L"BUTTON", L"Open TPUP folder", BS_PUSHBUTTON, 532, 430, 280, 26, CTL_OPEN_TPUP);
        EnableWindow(g_tpup_open, FALSE);
        g_texture_status = Add(hwnd, L"STATIC", L"Optional. Files stay in Downloads; nothing is installed in the game.", SS_LEFT, 28, 462, 778, 22, 156);
        g_placements.back().page = SetupPage::Game;
      g_del_cfg = Add(hwnd, L"BUTTON", L"Also remove settings", BS_AUTOCHECKBOX,
          596, 238, 216, 22, CTL_DEL_CFG);
      // Stored layout is the full developer view. The simple view collapses
      // the band kDevTop..kDevBottom (advanced tuning) and rides every row
      // below it up by kDevH, shrinking the window itself. All stored rows
      // below are dev-mode coordinates; DisplayY/DisplayFrame do the shift.
      g_frames.push_back({16, 290, 808, 85, 16, -1, L"Presets", SetupPage::Settings});
    g_frames.push_back({16, 380, 808, 280, 16, -1, L"Comfort & movement", SetupPage::Settings});
    g_frames.push_back({16, 650, 808, 76, 16, -1, L"Developer tuning", SetupPage::Settings});
    g_frames.push_back({16, 726, 808, 184, 16, -1, L"Game experience", SetupPage::Settings});
      Add(hwnd, L"BUTTON", L"Comfort", BS_PUSHBUTTON, 340, 308, 86, 22, CTL_PRESET_COMFORT);
      AddHint(hwnd, L"Dim edges; snap turn.", 340, 332, 86, 14);
      Add(hwnd, L"BUTTON", L"Balanced", BS_PUSHBUTTON, 434, 308, 86, 22, CTL_PRESET_BALANCED);
      AddHint(hwnd, L"Recommended.", 434, 332, 86, 14);
      Add(hwnd, L"BUTTON", L"Sharper", BS_PUSHBUTTON, 528, 308, 86, 22, CTL_PRESET_SHARPER);
      AddHint(hwnd, L"Clearest picture.", 528, 332, 86, 14);
      Add(hwnd, L"BUTTON", L"Match this PC", BS_PUSHBUTTON, 350, 356, 132, 26, CTL_PRESET_MATCH);
      AddHint(hwnd, L"Presets save at once. Match only sets eye scale and AA; comfort stays as it is.", 28, 314, 300, 44);
      // Startup mode is the one VR question: Play in VR, desktop observer
      // (flatscreen-only), or combat-only trace. The old Play-in-VR
      // checkbox lives here as this combo; observer modes force VR off.
      Add(hwnd, L"STATIC", L"Startup mode", SS_LEFT, 28, 392, 100, 22, 0);
      g_mode = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 132, 388, 180, 100, CTL_MODE);
      AddItems(g_mode, {L"VR preview (mono)", L"Desktop observer (no VR)", L"Combat-only trace (no VR)"});
      AddHint(hwnd, L"Mono VR only; stereo renderer unavailable.", 28, 414, 280, 16);
      
      g_haptics = Add(hwnd, L"BUTTON", L"Controller vibration", BS_AUTOCHECKBOX, 322, 392, 190, 22, CTL_HAPTICS);
      AddHint(hwnd, L"Feel hits and blocks.", 322, 414, 190, 16);
      Add(hwnd, L"STATIC", L"Weapon hand", SS_LEFT, 528, 392, 100, 22, 0);
      g_hand = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 632, 388, 174, 140, CTL_HAND);
      AddItems(g_hand, {L"Right", L"Left"});
      AddHint(hwnd, L"Which hand holds your weapon.", 528, 414, 278, 16);
      Add(hwnd, L"STATIC", L"Play style", SS_LEFT, 28, 438, 100, 22, 0);
      g_style = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 132, 434, 180, 140, CTL_STYLE);
      AddItems(g_style, {L"Standing", L"Seated", L"Roomscale"});
      AddHint(hwnd, L"How you play: on feet, in a chair, or walking the room.", 28, 460, 300, 16);
      g_motion = Add(hwnd, L"BUTTON", L"Swing uses attack button (experimental)", BS_AUTOCHECKBOX, 328, 434, 300, 22, CTL_MOTION);
      AddHint(hwnd, L"Swing sends attack-button input only; contact/damage unqualified.", 328, 460, 478, 16);
      // ---- Developer block: kDevTop..kDevBottom. Every row here must be
      // AddDev so the simple view hides it; nothing after may use y < kDevBottom.
      AddDev(hwnd, L"STATIC", L"Resolution scale (%)", SS_LEFT, 28, 494, 150, 22, 0);
      g_scale = AddDev(hwnd, L"EDIT", L"100", ES_NUMBER | WS_BORDER, 182, 490, 52, 26, CTL_SCALE);
      AddDev(hwnd, L"STATIC", L"Higher scale is sharper but slower (50-200).", SS_LEFT, 240, 494, 260, 22, 0);
      g_vignette = Add(hwnd, L"BUTTON", L"Dim edges while moving", BS_AUTOCHECKBOX, 510, 490, 220, 22, CTL_VIGNETTE);
      // Snap and smooth are one turning choice: snap jumps the view in
      // fixed steps, smooth turns continuously. One checkbox (snap on/off)
      // replaces the old snap + smooth-turn pair; smooth is simply !snap.
      g_snap = Add(hwnd, L"BUTTON", L"Snap turning (off = smooth stick turning)", BS_AUTOCHECKBOX, 28, 522, 320, 22, CTL_SNAP);
      AddDev(hwnd, L"STATIC", L"Snap step (deg)", SS_LEFT, 354, 524, 120, 22, 0);
      g_snapdeg = AddDev(hwnd, L"EDIT", L"45", ES_NUMBER | WS_BORDER, 478, 520, 52, 26, CTL_SNAPDEG);
      AddDev(hwnd, L"STATIC", L"Snap angle can be adjusted in developer tuning.", SS_LEFT, 536, 524, 270, 22, 0);
      
      
      
      AddHint(hwnd, L"DSR-FPS-Unlock uses HMD refresh in VR and monitor refresh on desktop; DSVR sets no native cap.", 28, 556, 778, 22);
      // Walk steering is one choice: head (default), or a controller hand
      // with head fallback on hand loss. One combo replaces the old
      // reference + hand pair.
      Add(hwnd, L"STATIC", L"Walk steered by", SS_LEFT, 28, 590, 120, 22, 0);
      g_move_mode = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 152, 586, 300, 140, CTL_MOVE_MODE);
      AddItems(g_move_mode, {L"Head (default)", L"Non-dominant hand", L"Left hand", L"Right hand"});
      AddHint(hwnd, L"Lost hand falls back to head.", 458, 590, 348, 22);
      // Body fit is fixed (head height + arm reach, one combined anchor):
      // the old disabled combo carried no choice, so it is gone. The hint
      // stays so the fit stays discoverable.
      AddHint(hwnd, L"Body fit: head height + arm reach (fixed).", 28, 634, 438, 22);
      AddDev(hwnd, L"STATIC", L"Avatar arms follow", SS_LEFT, 28, 662, 140, 22, 0);
      g_arm = AddDev(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 172, 658, 330, 140, CTL_ARM);
      AddItems(g_arm, {L"Off (default)", L"Observe only (no pose writes)", L"Drive native arms (unqualified)"});
      AddDev(hwnd, L"STATIC", L"Arm tracking is unqualified; leave Off unless testing.", SS_LEFT, 508, 662, 298, 22, 0);
      // ---- End of developer block (must end at kDevBottom = 686).
      AddDev(hwnd, L"STATIC", L"Other mod proxy (optional)", SS_LEFT, 28, 700, 212, 22, 0);
      g_chain = AddDev(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 246, 696, 560, 26, CTL_CHAIN, 34);
      // ---- Post-block rows (y >= kDevBottom): these ride up in simple view.
      g_debugcap = Add(hwnd, L"BUTTON", L"Record play session (trace)", BS_AUTOCHECKBOX, 28, 730, 300, 22, CTL_DEBUGCAP);
      AddHint(hwnd, L"Saves movement/button logs for bug reports. No video.", 336, 730, 470, 22);
      Add(hwnd, L"STATIC", L"Gameplay view", SS_LEFT, 28, 760, 110, 22, 0);
      g_view = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 142, 756, 260, 140, CTL_VIEW);
      AddItems(g_view, {L"Immersive first person (default)", L"Theater screen"});
      AddHint(hwnd, L"Menus, the title screen and cutscenes always use the theater screen.", 410, 760, 396, 22);
      AddHint(hwnd, L"DSR-FPS-Unlock owns the rate cap; DSVR never sets one.", 28, 782, 560, 18);
      Add(hwnd, L"STATIC", L"My own body", SS_LEFT, 28, 810, 110, 22, 0);
      g_body = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 142, 806, 260, 160, CTL_BODY);
      AddItems(g_body, {L"Show all of it", L"Hide head, hair and helmet",
                    L"Hide head, torso and legs", L"Hide the whole player model"});
      AddHint(hwnd, L"Arms and weapon stay visible except in the last choice.", 410, 810, 396, 22);
      AddHint(hwnd, L"Calibrate height in VR. F7 applies live settings.", 28, 832, 560, 18);
      // Visible Advanced-tuning checkbox: the same action as Ctrl+D, so the
      // developer view is discoverable without the shortcut.
      g_advanced_toggle = Add(hwnd, L"BUTTON", L"Show all saved settings", BS_PUSHBUTTON, 28, 930, 240, 26, CTL_ADVANCED_TOGGLE);
  AddHint(hwnd, L"Advanced options and the full DSVR.toml settings list.", 280, 850, 360, 22);
    g_devtoggle = Add(hwnd, L"BUTTON", L"Show developer tuning", BS_AUTOCHECKBOX, 28, 1030, 250, 22, CTL_DEVTOGGLE);
    AddHint(hwnd, L"Reveals developer-only controls; it does not change game behavior by itself.", 286, 1030, 500, 22);
    g_advanced_open = Add(hwnd, L"BUTTON", L"Open DSVR.toml", BS_PUSHBUTTON, 28, 1064, 180, 26, CTL_ADVANCED_OPEN);
    g_advanced_reload = Add(hwnd, L"BUTTON", L"Reload from file", BS_PUSHBUTTON, 220, 1064, 180, 26, CTL_ADVANCED_RELOAD);
    g_advanced_summary = Add(hwnd, L"EDIT", L"", ES_MULTILINE | ES_READONLY | WS_BORDER | WS_VSCROLL | ES_AUTOVSCROLL, 28, 1100, 778, 330, CTL_ADVANCED_INDEX, 28, 16);
    SendMessageW(g_advanced_summary, EM_SETLIMITTEXT, 131072, 0);
    SetText(g_advanced_summary, AdvancedSettingsIndex());
    for (size_t i = g_placements.size() - 4; i < g_placements.size(); ++i) g_placements[i].page = SetupPage::Settings;
  Add(hwnd, L"BUTTON", L"Reset safe defaults", BS_PUSHBUTTON, 688, 882, 126, 26, CTL_RESET_SAFE);
  AddHint(hwnd, L"Undoes every setting above.", 28, 886, 640, 20);
  Add(hwnd, L"BUTTON", L"Clear log", BS_PUSHBUTTON, 716, 912, 108, 26, CTL_CLEAR_LOG);
    g_log = Add(hwnd, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER | WS_VSCROLL,
          16, 942, 808, 70, CTL_LOG, 16, 16);
      SendMessageW(g_log, EM_SETLIMITTEXT, 131072, 0);
      GuiLog(L"DSVR " + dsvr::WideFromUtf8(dsvr::kModVersion) + L" -- play offline while DSVR is installed.");
      PopulateControls(dsvr::Config{});
      RefreshGame(true);
      SetPage(hwnd, SetupPage::Home);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp), notification = HIWORD(wp);
      if (!g_suppress && id >= kFirstSetting && id <= kLastSetting &&
          (notification == BN_CLICKED || notification == EN_CHANGE || notification == CBN_SELCHANGE)) {
        // No click-time interlock needed: the startup-mode combo holds the
        // one VR question, so the window cannot show a combination
        // ConfigValid would refuse.
          if (id == CTL_DEVTOGGLE && notification == BN_CLICKED) {
          // The checkbox mirrors Ctrl+D: apply immediately, not on Save.
          ToggleDevMode(hwnd);
            SetChecked(g_devtoggle, g_dev_mode);
          ApplyControlNow(hwnd);
          return 0;
          }
          // Instant-apply: EN_CHANGE fires per keystroke in edit fields;
        // save on focus loss / Enter instead of every keystroke.
        if ((id == CTL_SCALE || id == CTL_SNAPDEG || id == CTL_CHAIN) &&
              notification == EN_CHANGE)
            return 0;
        ApplyControlNow(hwnd);
        return 0;
        }
        if ((id == CTL_SCALE || id == CTL_SNAPDEG || id == CTL_CHAIN) &&
          notification == EN_KILLFOCUS) {
        ApplyControlNow(hwnd);
        return 0;
        }
      if (id == CTL_GAME_EDIT && notification == EN_CHANGE) {
        g_path_resolved = false;
        UpdateBrief();
        return 0;
      }
      if (id == CTL_LAUNCH) {
        LaunchPrimary(hwnd);
      } else if (id == CTL_LAUNCH_NATIVE) {
        LaunchGame(hwnd);
      } else if (id == CTL_NAV_HOME || id == CTL_NAV_GAME ||
                 id == CTL_NAV_SETTINGS || id == CTL_NAV_ACTIVITY) {
        const SetupPage page = id == CTL_NAV_HOME ? SetupPage::Home :
            id == CTL_NAV_GAME ? SetupPage::Game :
            id == CTL_NAV_SETTINGS ? SetupPage::Settings : SetupPage::Help;
        SetPage(hwnd, page);
        return 0;
      } else if (id == CTL_BROWSE) {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Dark Souls Remastered\0DarkSoulsRemastered.exe\0\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = 32768;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) { SetText(g_game_edit, file); RefreshGame(true); }
      } else if (id == CTL_DETECT) {
        SetText(g_game_edit, L"");
        RefreshGame(true);
      } else if (id == CTL_VERIFY) {
        if (RefreshGame(false)) {
          g_verified_ok = setup::VerifyExe(g_exe, MakeLog());
          g_verified_exe = g_exe;
          SetStatus(g_verified_ok ? L"Executable build verified; runtime readiness still untested." :
                                   L"Executable verification failed -- do not install on an unsupported build.");
        }
      } else if (id == CTL_DOCTOR) {
        if (RefreshGame(false)) setup::DoctorChecks(g_exe, MakeLog());
      } else if (id == CTL_PRESET_COMFORT || id == CTL_PRESET_BALANCED ||
                 id == CTL_PRESET_SHARPER || id == CTL_PRESET_MATCH) {
        ApplyPreset(id);
      } else if (id == CTL_DEVMODE) {
        ToggleDevMode(hwnd);
      } else if (id == CTL_RESET_SAFE) {
        ApplySafeDefaults(hwnd);
      } else if (id == CTL_ADVANCED_TOGGLE) {
        ToggleAdvancedSettings(hwnd);
      } else if (id == CTL_ADVANCED_OPEN) {
        OpenAdvancedConfig(hwnd);
      } else if (id == CTL_ADVANCED_RELOAD) {
        ReloadAdvancedConfig();
      } else if (id == CTL_CLEAR_LOG) {
        SetText(g_log, L"");
      } else if (id == CTL_OPEN_FOLDER) {
        if (RefreshGame(false)) ShellExecuteW(hwnd, L"open", setup::DirNameOf(g_exe).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      } else if (id == CTL_UNINSTALL) {
        if (RefreshGame(false) && DiscardChanges(hwnd)) {
          const int result = setup::DoUninstall(g_exe, Checked(g_del_cfg), false, MakeLog(), MakeConfirm(hwnd));
          if (result == 0) { ClearDirty(); g_exe.clear(); RefreshGame(true); }
          else GuiLog(L"Uninstall did not complete. See diagnostics above.");
        }
      } else if (id == CTL_FPS_UNLOCK) {
        // The button disables itself once the chain is in place; guard the
        // handler too so a stray message cannot re-download over an install.
        if (setup::FpsUnlockStateOf(g_exe).installed()) {
          GuiLog(L"The frame-rate dependency is already installed and chained.");
        } else if (RefreshGame(false)) {
          const int result = setup::InstallFpsUnlock(g_exe, MakeLog(), MakeConfirm(hwnd));
          if (result == 0) RefreshGame(true);
          else GuiLog(L"The frame-rate dependency was not installed. See diagnostics above.");
        }
      } else if (id == CTL_DOWNLOAD_TPUP) {
        std::wstring folder;
        const int result = setup::DownloadTextureTools(MakeLog(), MakeConfirm(hwnd), folder);
        if (result == 0) {
          g_tpup_folder = folder;
          EnableWindow(g_tpup_open, TRUE);
          SetWindowTextW(g_texture_status,
              L"TPUP is in Downloads. Download the texture ZIP there, unzip it yourself, then open it in TPUP.");
        } else {
          GuiLog(L"TPUP was not made ready. See diagnostics above.");
        }
      } else if (id == CTL_TEXTURE_SHARE) {
        constexpr wchar_t kTextureDownloadUrl[] =
            L"https://file.kiwi/0a217274#eBUXneCX5FyA97NggbFhTQ";
        if ((INT_PTR)ShellExecuteW(hwnd, L"open", kTextureDownloadUrl, nullptr, nullptr,
                                   SW_SHOWNORMAL) <= 32) {
          GuiLog(L"FAIL: could not open the texture download page.");
        } else {
          GuiLog(L"Texture download opened. Save the ZIP in Downloads; unzip it yourself, then choose its folder in TPUP.");
        }
      } else if (id == CTL_OPEN_TPUP) {
        if (g_tpup_folder.empty() || !setup::PathExists(g_tpup_folder)) {
          EnableWindow(g_tpup_open, FALSE);
          GuiLog(L"TPUP folder is unavailable. Use Download and unzip TPUP first.");
        } else if ((INT_PTR)ShellExecuteW(hwnd, L"open", g_tpup_folder.c_str(), nullptr,
                                          nullptr, SW_SHOWNORMAL) <= 32) {
          GuiLog(L"FAIL: could not open the TPUP folder in Downloads.");
        }
      } else if (id == CTL_INSTALL) { if (RefreshGame(false)) {
        if (!(g_verified_exe == g_exe && g_verified_ok)) {
          GuiLog(L"Verify the build before installing -- Install is disabled until Verify passes.");
          UpdateBrief();
          return 0;
        }
        setup::InstallOptions opts;
        opts.enable_vr_after = g_mode &&
            (int)SendMessageW(g_mode, CB_GETCURSEL, 0, 0) <= 0;
        if (!SaveSettings(true)) return 0;
        const int result = setup::DoInstall(g_exe, opts, MakeLog(), MakeConfirm(hwnd));
        if (result == 0) { g_exe.clear(); RefreshGame(true); }
        else GuiLog(L"Install did not complete. See diagnostics above.");
      } }
      return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC dc = reinterpret_cast<HDC>(wp);
      HWND control = reinterpret_cast<HWND>(lp);
      const bool field = msg != WM_CTLCOLORSTATIC || control == g_log || InList(g_combos, control);
      const auto id = GetDlgCtrlID(control);
      SetTextColor(dc, control == g_heading ? kBone : id >= kHintBase ? kMuted : kInk);
      SetBkColor(dc, field ? kField : kBackdrop);
      return reinterpret_cast<LRESULT>(field ? g_field_brush : g_backdrop_brush);
    }
    case WM_MEASUREITEM: {
      auto* item = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
      if (item->CtlType == ODT_COMBOBOX) { item->itemHeight = S(20); return TRUE; }
      break;
    }
    case WM_DRAWITEM: {
      auto* item = reinterpret_cast<LPDRAWITEMSTRUCT>(lp);
      if (item->CtlType == ODT_BUTTON) { DrawButton(item); return TRUE; }
      if (item->CtlType == ODT_COMBOBOX) {
        const int saved = SaveDC(item->hDC);
        FillRect(item->hDC, &item->rcItem, g_field_brush);
        SelectObject(item->hDC, g_font);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? kMuted :
            (item->itemState & ODS_SELECTED) ? kBone : kInk);
        if (item->itemID != (UINT)-1) {
          const auto len = SendMessageW(item->hwndItem, CB_GETLBTEXTLEN, item->itemID, 0);
          if (len >= 0) {
            std::wstring text((size_t)len + 1, L'\0');
            SendMessageW(item->hwndItem, CB_GETLBTEXT, item->itemID, (LPARAM)text.data());
            RECT label = item->rcItem;
            label.left += S(6);
            DrawTextW(item->hDC, text.c_str(), -1, &label,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
          }
        }
        if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT))
          DrawFocusRect(item->hDC, &item->rcItem);
        RestoreDC(item->hDC, saved);
        return TRUE;
      }
      break;
    }
    case WM_ERASEBKGND: {
      RECT rc{};
      GetClientRect(hwnd, &rc);
      FillRect(reinterpret_cast<HDC>(wp), &rc, g_backdrop_brush);
      return 1;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      // Section outlines belong to the scrolling content, never to the band
      // holding the pinned heading and page tabs.
      IntersectClipRect(dc, 0, S(kChromeBottom), rc.right, rc.bottom);
        DrawFrames(dc, rc);
      EndPaint(hwnd, &paint);
      return 0;
    }
    case WM_SIZE:
      if (wp != SIZE_MINIMIZED) Relayout(hwnd);
      return 0;
    case WM_VSCROLL: {
      SCROLLINFO info{sizeof(info), SIF_ALL};
      GetScrollInfo(hwnd, SB_VERT, &info);
      int next = g_scroll_y;
      switch (LOWORD(wp)) {
        case SB_LINEUP: next -= S(28); break;
        case SB_LINEDOWN: next += S(28); break;
        case SB_PAGEUP: next -= (int)info.nPage; break;
        case SB_PAGEDOWN: next += (int)info.nPage; break;
        case SB_THUMBTRACK: next = info.nTrackPos; break;
        case SB_TOP: next = 0; break;
        case SB_BOTTOM: next = info.nMax; break;
      }
      g_scroll_y = next < 0 ? 0 : next;
      Relayout(hwnd);
      return 0;
    }
    case WM_DPICHANGED: {
      g_dpi = HIWORD(wp);
      RecreateFonts();
      const auto* rect = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
          rect->bottom - rect->top, SWP_NOACTIVATE | SWP_NOZORDER);
      Relayout(hwnd);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      RECT rc{0, 0, S(kClientWidth), S(220)};
      AdjustFrame(rc);
      auto* info = reinterpret_cast<MINMAXINFO*>(lp);
      info->ptMinTrackSize = {rc.right - rc.left, rc.bottom - rc.top};
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int RunGui() {
  if (HWND console = GetConsoleWindow()) ShowWindow(console, SW_HIDE);
  const auto user = GetModuleHandleW(L"user32.dll");
  using SetDpiContext = BOOL(WINAPI*)(HANDLE);
  const auto set_dpi = reinterpret_cast<SetDpiContext>(GetProcAddress(user, "SetProcessDpiAwarenessContext"));
  if (!set_dpi || !set_dpi(reinterpret_cast<HANDLE>(-4))) SetProcessDPIAware();
  HDC screen = GetDC(nullptr);
  if (screen) { g_dpi = GetDeviceCaps(screen, LOGPIXELSY); ReleaseDC(nullptr, screen); }
  RecreateFonts();
  g_backdrop_brush = CreateSolidBrush(kBackdrop);
  g_field_brush = CreateSolidBrush(kField);
  g_install = nullptr;
  g_dev_mode = false;
  g_dev_controls.clear();
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DSVRSetupGui";
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = g_backdrop_brush;
  const bool registered = RegisterClassW(&wc) != 0;
  RECT rc{0, 0, S(kClientWidth), S(ContentHeight())};
  AdjustFrame(rc);
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int height = rc.bottom - rc.top;
  if (work.bottom > work.top && height > work.bottom - work.top) height = work.bottom - work.top;
  g_main = registered ? CreateWindowW(wc.lpszClassName, L"DSVR Launcher -- Dark Souls Remastered",
      WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
      rc.right - rc.left, height, nullptr, nullptr, wc.hInstance, nullptr) : nullptr;
  MSG msg{};
  int result = 0;
  // Ctrl+D toggles the developer view from anywhere in the window, including
  // inside edit boxes. The accelerator runs before dialog navigation.
  ACCEL accel_entry{};
  accel_entry.fVirt = FVIRTKEY | FCONTROL;
  accel_entry.key = 'D';
  accel_entry.cmd = CTL_DEVMODE;
  HACCEL accel = CreateAcceleratorTableW(&accel_entry, 1);
  if (g_main) {
    ShowWindow(g_main, SW_SHOW);
    UpdateWindow(g_main);
    HWND previous_focus = nullptr;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
      if (accel && TranslateAcceleratorW(g_main, accel, &msg)) continue;
      if (!IsDialogMessageW(g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
      // Keyboard navigation must scroll an offscreen control into view too.
      HWND focus = GetFocus();
      if (focus != previous_focus && IsChild(g_main, focus)) {
        RECT child{}, client{};
        GetWindowRect(focus, &child);
        MapWindowPoints(nullptr, g_main, reinterpret_cast<POINT*>(&child), 2);
        GetClientRect(g_main, &client);
        const int band = S(kChromeBottom);
          if (child.top < band || child.bottom > client.bottom) {
          g_scroll_y += child.top < band ? child.top - band
                                         : child.bottom - client.bottom;
          Relayout(g_main);
        }
      }
      previous_focus = focus;
    }
    if (IsWindow(g_main)) DestroyWindow(g_main);
  }
  if (accel) DestroyAcceleratorTable(accel);
  if (registered) UnregisterClassW(wc.lpszClassName, wc.hInstance);
  DeleteObject(g_font); g_font = nullptr;
  DeleteObject(g_title_font); g_title_font = nullptr;
  DeleteObject(g_section_font); g_section_font = nullptr;
  DeleteObject(g_hint_font); g_hint_font = nullptr;
  DeleteObject(g_backdrop_brush); g_backdrop_brush = nullptr;
  DeleteObject(g_field_brush); g_field_brush = nullptr;
  g_placements.clear(); g_frames.clear(); g_combos.clear(); g_toggles.clear();
  g_dev_controls.clear();
  return !g_main || result < 0 ? 1 : (int)msg.wParam;
}

