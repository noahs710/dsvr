// DSVR-Setup GUI: friendly configurator window (no console needed).
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

#include <commdlg.h>
#include <shellapi.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "../common/config.h"
#include "../common/version.h"
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
  CTL_PRESET_PERF = 113,
  CTL_PRESET_VETERAN = 114,
  CTL_FW_BLOCK = 115,
  CTL_FW_UNBLOCK = 116,
  CTL_DEL_CFG = 117,
  CTL_CLEAR_LOG = 118,
  CTL_LAUNCH = 119,

  CTL_VR = 130,
  CTL_VIGNETTE = 131,
  CTL_SNAP = 132,
  CTL_HAPTICS = 133,
  CTL_MOTION = 134,
  CTL_SCALE = 135,
  CTL_SNAPDEG = 136,
  CTL_HAND = 137,
  CTL_STYLE = 138,
  CTL_CHAIN = 139,
  CTL_FPS = 140,
  CTL_MOVE_MODE = 141,
  CTL_MOVE_HAND = 142,
  CTL_BODY_FIT = 143,
  CTL_ARM = 144,
};
constexpr int kFirstSetting = CTL_VR;
constexpr int kLastSetting = CTL_ARM;

HWND g_game_edit = nullptr, g_status = nullptr, g_log = nullptr;
HWND g_vr = nullptr, g_vignette = nullptr, g_snap = nullptr;
HWND g_haptics = nullptr, g_motion = nullptr;
HWND g_scale = nullptr, g_snapdeg = nullptr;
HWND g_hand = nullptr, g_style = nullptr, g_chain = nullptr;
HWND g_fps = nullptr;
HWND g_move_mode = nullptr, g_move_hand = nullptr;
HWND g_body_fit = nullptr, g_arm = nullptr;
HWND g_del_cfg = nullptr;
HWND g_main = nullptr;
HFONT g_font = nullptr, g_title_font = nullptr;
std::wstring g_exe;  // resolved exe path (empty = not found)
std::wstring g_status_base;  // status text without the unsaved-changes suffix
unsigned g_dpi = 96;
bool g_dirty = false;      // controls differ from what is on disk
bool g_suppress = false;   // a programmatic control update is in flight
std::wstring g_read_notes;  // fields ReadControls could not apply
HWND g_brief = nullptr, g_launch = nullptr, g_heading = nullptr;
dsvr::Config g_disk_config{};
bool g_path_resolved = false;
std::wstring g_verified_exe;
bool g_verified_ok = false;
int g_scroll_y = 0;
WNDPROC g_checkbox_proc = nullptr;
constexpr int kClientWidth = 840, kContentHeight = 760;

// Ash-black backdrop, soot fields, ochre gold accent, parchment ink. These
// Win32 control classes have no dark mode, so the window answers its own colour
// messages and owner-draws every button and check box instead of accepting the
// system's light-theme rendering.
constexpr COLORREF kBackdrop = RGB(18, 17, 16);
constexpr COLORREF kField = RGB(31, 29, 27);
constexpr COLORREF kEdge = RGB(84, 71, 46);
constexpr COLORREF kInk = RGB(226, 220, 205);
constexpr COLORREF kMuted = RGB(151, 143, 127);
constexpr COLORREF kGold = RGB(201, 156, 76);
constexpr COLORREF kGoldDim = RGB(122, 96, 50);
constexpr COLORREF kOnGold = RGB(20, 18, 15);
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
};
std::vector<Frame> g_frames;

bool InList(const std::vector<HWND>& list, HWND hwnd) {
  for (HWND item : list) if (item == hwnd) return true;
  return false;
}

void UpdateBrief();

// Logical -> physical pixels for the current display.
int S(int logical) { return MulDiv(logical, (int)g_dpi, 96); }

// A control whose box is replayed on resize. right/bottom >= 0 replace the
// fixed width/height with a margin from that edge, in logical units.
struct Placement {
  HWND hwnd;
  int x, y, w, h;
  int right, bottom;
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
  if (g_status)
    SetWindowTextW(g_status, (g_dirty ? g_status_base + L"   [unsaved changes -- click Save settings]"
                                     : g_status_base)
                                 .c_str());
  UpdateBrief();
}

void MarkDirty() {
  if (g_dirty) return;
  g_dirty = true;
  SetStatus(g_status_base);
}

void ClearDirty() {
  g_dirty = false;
  SetStatus(g_status_base);
}

setup::LogFn MakeLog() { return [](const std::wstring& s) { GuiLog(s); }; }

setup::ConfirmFn MakeConfirm(HWND parent) {
  return [parent](const wchar_t* title, const std::wstring& text,
                  bool /*default_yes*/, bool destructive) {
    UINT flags = MB_YESNO | MB_ICONQUESTION;
    if (destructive) flags |= MB_DEFBUTTON2;
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

// File presence is not runtime qualification. Draft motion settings and the
// separate native physical-attacks flag must never masquerade as each other.
void UpdateBrief() {
  if (!g_brief || !g_vr || !g_arm || !g_motion || !g_snap) return;
  const auto st = g_exe.empty() ? setup::InstallState{} : setup::InstallStateOf(g_exe);
  std::wstring state = !g_path_resolved ? L"Resolve the path with Verify, Browse or Auto-detect." :
      st.partial() ? L"Partial install -- repair or uninstall before launching." :
      st.installed() ? L"Mod files present; VR/runtime not tested. Offline only." :
      L"Mod not installed; Launch starts the native game.";
  if (g_path_resolved) state += g_verified_exe == g_exe ?
      (g_verified_ok ? L" Build verified." : L" Build verification failed.") : L" Build not verified.";
  const int arm = (int)SendMessageW(g_arm, CB_GETCURSEL, 0, 0);
  const std::wstring switches = std::wstring(g_dirty ? L"Draft: " : L"Settings: ") +
      L"VR " + (Checked(g_vr) ? L"on" : L"off") +
      L" | arms " + (arm == 2 ? L"drive (unqualified)" : arm == 1 ? L"observe only" : L"off") +
      L" | motion attacks " + (Checked(g_motion) ? L"on" : L"off") +
      L" | " + (Checked(g_snap) ? L"snap turn" : g_disk_config.smooth_turn ? L"smooth turn" : L"turning off");
  const std::wstring native = std::wstring(L"Native physical attacks (loaded/default): ") +
      (g_disk_config.physical_attacks ? L"on -- unqualified" : L"off") +
      L". Separate from motion attacks; manual combat remains available.";
  SetWindowTextW(g_brief, (state + L"\r\n" + switches + L"\r\n" + native).c_str());
  EnableWindow(g_launch, g_path_resolved && !st.partial());
}

void PopulateControls(const dsvr::Config& c) {
  // Writing an edit control sends EN_CHANGE, which would otherwise read back as
  // a user edit and leave the window permanently "unsaved".
  g_suppress = true;
  g_disk_config = c;
  SetChecked(g_vr, c.vr_enabled);
  SetChecked(g_vignette, c.comfort_vignette);
  SetChecked(g_snap, c.snap_turn);
  SetChecked(g_haptics, c.haptics);
  SetChecked(g_motion, c.motion_attacks);
  SetText(g_scale, std::to_wstring(c.render_scale_percent));
  SetText(g_snapdeg, std::to_wstring(c.snap_turn_degrees));
  // Eye height belongs to calibration/the in-game manual override only.
  SendMessageW(g_hand, CB_SETCURSEL, c.dominant_hand_left ? 1 : 0, 0);
  SendMessageW(g_move_mode, CB_SETCURSEL,
               c.movement_orientation == dsvr::MovementOrientation::Controller ? 1 : 0, 0);
  SendMessageW(g_move_hand, CB_SETCURSEL,
               c.movement_controller == dsvr::MovementController::Left
                   ? 1
               : c.movement_controller == dsvr::MovementController::Right ? 2 : 0,
               0);
   SendMessageW(g_body_fit, CB_SETCURSEL, c.embodiment_fit == "arm" ? 1 : 0, 0);
  // Arm tracking is two config fields with one honest three-state control:
  // off, enabled-but-observe-only (the shipped default), and driving the model.
  SendMessageW(g_arm, CB_SETCURSEL, !c.arm_ik_enabled ? 0 : c.arm_ik_log_only ? 1 : 2,
               0);
  {
    const int limits[] = {60, 72, 80, 90, 120, 144, 240};
    int fps_idx = 0;
    for (int i = 0; i < 7; ++i)
      if (c.frame_limit_fps == limits[i]) fps_idx = i;
    SendMessageW(g_fps, CB_SETCURSEL, fps_idx, 0);
  }
  int style_idx = 0;
  if (c.play_style == "seated") style_idx = 1;
  if (c.play_style == "roomscale") style_idx = 2;
  SendMessageW(g_style, CB_SETCURSEL, style_idx, 0);
  SetText(g_chain, dsvr::WideFromUtf8(c.loader_chain_proxy));
  g_suppress = false;
  ClearDirty();
}

dsvr::Config ReadControls(dsvr::Config base) {
  g_read_notes.clear();
  base.vr_enabled = Checked(g_vr);
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
    const int mode = (int)SendMessageW(g_move_mode, CB_GETCURSEL, 0, 0);
    if (mode == 1)
      base.movement_orientation = dsvr::MovementOrientation::Controller;
    else if (mode == 0)
      base.movement_orientation = dsvr::MovementOrientation::Head;
    const int hand = (int)SendMessageW(g_move_hand, CB_GETCURSEL, 0, 0);
    base.movement_controller =
        hand == 1 ? dsvr::MovementController::Left
                  : hand == 2 ? dsvr::MovementController::Right
                              : dsvr::MovementController::NonDominant;
     const int fit = (int)SendMessageW(g_body_fit, CB_GETCURSEL, 0, 0);
    base.embodiment_fit = fit == 1 ? "arm" : "head";
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
  {
    const int limits[] = {60, 72, 80, 90, 120, 144, 240};
    int fps_idx = (int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0);
    if (fps_idx >= 0 && fps_idx < 7) base.frame_limit_fps = limits[fps_idx];
  }
  int style_idx = (int)SendMessageW(g_style, CB_GETCURSEL, 0, 0);
  base.play_style =
      style_idx == 1 ? "seated" : style_idx == 2 ? "roomscale" : "standing";
  base.loader_chain_proxy = dsvr::Utf8FromWide(GetText(g_chain));
  return base;
}

// What the game folder currently holds, in the user's words. The same three
// states DoctorChecks reports, so the window and Doctor never disagree.
std::wstring InstallStateLine() {
  const setup::InstallState st = setup::InstallStateOf(g_exe);
  if (st.partial())
    return L"Partial install: only one of dinput8.dll / DSVR.dll is present -- "
           L"run Install again, or Uninstall.";
  if (st.installed())
    return L"DSVR is installed. Play offline; run Uninstall before going online.";
  return L"DSVR is not installed.";
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
  if (g_dirty && MessageBoxW(parent,
      L"Launch with settings currently on disk, ignoring unsaved edits?\nChoose No, then Save settings to keep your edits.",
      L"Unsaved settings", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
  if (!install.installed() && MessageBoxW(parent,
      L"DSVR is not installed. Launch the native game without the mod?",
      L"Launch without DSVR", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
  STARTUPINFOW start{};
  start.cb = sizeof(start);
  PROCESS_INFORMATION process{};
  std::wstring command = L"\"" + g_exe + L"\"";
  if (!CreateProcessW(g_exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, setup::DirNameOf(g_exe).c_str(), &start, &process)) {
    const auto error = GetLastError();
    SetStatus(L"Launch failed -- see activity log.");
    GuiLog(L"FAIL: the game did not start (Win32 error " + std::to_wstring(error) + L").");
    return;
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  GuiLog(L"Started " + g_exe + L" (pid " + std::to_wstring(process.dwProcessId) +
         L"). Play offline while DSVR is installed.");
  SetStatus(L"Game process started; headset/runtime readiness has not been tested.");
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
  if (asked.frame_limit_fps != saved.frame_limit_fps)
    GuiLog(L"Adjusted frame limit " + std::to_wstring(asked.frame_limit_fps) + L" -> " +
           std::to_wstring(saved.frame_limit_fps) +
           L" (fallback: 60/72/80/90/120/144/240; live runtime overrides).");
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

bool SaveSettings(bool silent) {
  if (g_exe.empty() || setup::FindGameExe(GetText(g_game_edit)) != g_exe) {
    // Refreshing here would reload the controls from a file that does not exist
    // yet and throw away what the user just typed. Say no, keep the edits.
    GuiLog(L"No game folder found -- nothing was saved. Use Browse or "
           L"Auto-detect first, then Save settings.");
    return false;
  }
  std::wstring cfg_path = ConfigPathForGame();
  dsvr::Config cfg;
  dsvr::ConfigLoad(cfg_path.c_str(), cfg);  // keep fields not shown in GUI
  cfg = ReadControls(cfg);
  if (!g_read_notes.empty()) GuiLog(g_read_notes);
  // Clamp via a save/load round-trip so GUI and core agree exactly.
  const dsvr::Config asked = cfg;
  if (!dsvr::ConfigSave(cfg_path.c_str(), cfg)) {
    GuiLog(L"FAIL: cannot write DSVR.toml (check permissions). Fix permissions, then retry Save.");
    return false;
  }
  dsvr::Config check;
  if (!dsvr::ConfigLoad(cfg_path.c_str(), check)) {
    GuiLog(L"FAIL: settings written but could not be read back. Verify DSVR.toml.");
    return false;
  }
  PopulateControls(check);
  ReportAdjustments(asked, check);
  if (!silent)
    GuiLog(L"Saved " + cfg_path +
           L". Restart the game to apply (F7 reloads the live-tunable ones).");
  return true;
}

HFONT MakeFont(int points, int weight, const wchar_t* face = L"Segoe UI") {
  return CreateFontW(-MulDiv(points, (int)g_dpi, 72), 0, 0, 0, weight, FALSE,
      FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

// A serif display face carries the mod's identity; the first installed one wins.
const wchar_t* SerifFace() {
  wchar_t dir[MAX_PATH]{};
  GetWindowsDirectoryW(dir, MAX_PATH);
  const std::wstring fonts = std::wstring(dir) + L"\\Fonts\\";
  const struct { const wchar_t* face, *file; } candidates[] = {
      {L"Georgia", L"georgia.ttf"}, {L"Goudy Old Style", L"GOUDOS.TTF"},
      {L"Book Antiqua", L"BOOKOS.TTF"}, {L"Times New Roman", L"times.ttf"}};
  for (const auto& face : candidates)
    if (GetFileAttributesW((fonts + face.file).c_str()) != INVALID_FILE_ATTRIBUTES)
      return face.face;
  return L"Segoe UI";
}

void RecreateFonts() {
  HFONT body = MakeFont(9, FW_NORMAL);
  HFONT title = MakeFont(18, FW_NORMAL, SerifFace());
  if (!body || !title) { DeleteObject(body); DeleteObject(title); return; }
  HFONT old_body = g_font, old_title = g_title_font;
  g_font = body; g_title_font = title;
  for (const auto& p : g_placements)
    SendMessageW(p.hwnd, WM_SETFONT, (WPARAM)(p.hwnd == g_heading ? title : body), TRUE);
  for (HWND combo : g_combos) {
    SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, S(20));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, S(20));
  }
  DeleteObject(old_body); DeleteObject(old_title);
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
  g_placements.push_back({control, x, y, w, h, right, bottom});
  return control;
}

void AddItems(HWND combo, std::initializer_list<const wchar_t*> items) {
  for (const wchar_t* item : items) SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)item);
  SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

// A quiet explanation line: same control, muted ink, told apart by its id block.
HWND AddHint(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
  return Add(parent, L"STATIC", text, SS_LEFT, x, y, w, h, kHintBase + g_hint_id++);
}

void AddFrame(const wchar_t* title, int x, int y, int w, int h, int right = -1,
              int bottom = -1) {
  g_frames.push_back({x, y, w, h, right, bottom, title});
}

void DrawFrames(HDC dc, const RECT& client) {
  HFONT old_font = (HFONT)SelectObject(dc, g_font);
  SetBkMode(dc, TRANSPARENT);
  for (const auto& f : g_frames) {
    const int x = S(f.x), y = S(f.y) + S(9) - g_scroll_y;
    const int w = f.right >= 0 ? client.right - S(f.x + f.right) : S(f.w);
    const int content_h = client.bottom > S(kContentHeight) ? client.bottom : S(kContentHeight);
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
    SetTextColor(dc, kGold);
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
  const bool primary = item->CtlID == CTL_LAUNCH;
  HDC dc = item->hDC;
  const int saved_dc = SaveDC(dc);
  SelectObject(dc, g_font);
  const RECT box = item->rcItem;
  const COLORREF fill = pressed ? kGoldDim : primary ? RGB(41, 34, 22) : kField;
  HPEN pen = CreatePen(PS_SOLID, 1, disabled ? kEdge : (primary || pressed) ? kGold : kEdge);
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
    HBRUSH mark_fill = CreateSolidBrush(checked ? kGold : kBackdrop);
    HPEN mark_pen = CreatePen(PS_SOLID, 1, checked ? kGold : kMuted);
    old_pen = (HPEN)SelectObject(dc, mark_pen);
    old_fill = (HBRUSH)SelectObject(dc, mark_fill);
    Rectangle(dc, mark.left, mark.top, mark.right, mark.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_fill);
    DeleteObject(mark_fill);
    DeleteObject(mark_pen);
    if (checked) {
      HPEN ink = CreatePen(PS_SOLID, MulDiv(2, (int)g_dpi, 96), kOnGold);
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
  SetTextColor(dc, disabled ? kMuted : (pressed && !toggle) ? kOnGold : kInk);
  RECT label{text_x, box.top, box.right - S(6), box.bottom};
  UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
  if (item->itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
  DrawTextW(dc, text, -1, &label, flags);
  if (focused && !(item->itemState & ODS_NOFOCUSRECT)) {
    RECT focus = box;
    InflateRect(&focus, -S(3), -S(3));
    SetTextColor(dc, kGold);
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
  const int content_h = rc.bottom > S(kContentHeight) ? rc.bottom : S(kContentHeight);
  SCROLLINFO scroll{sizeof(scroll), SIF_RANGE | SIF_PAGE | SIF_POS};
  scroll.nMax = content_h - 1;
  scroll.nPage = rc.bottom;
  const int max_scroll = content_h - rc.bottom;
  if (g_scroll_y > max_scroll) g_scroll_y = max_scroll;
  scroll.nPos = g_scroll_y;
  SetScrollInfo(hwnd, SB_VERT, &scroll, TRUE);
  GetClientRect(hwnd, &rc);
  for (const auto& p : g_placements) {
    const int w = p.right >= 0 ? rc.right - S(p.x + p.right) : S(p.w);
    const int h = p.bottom >= 0 ? content_h - S(p.y + p.bottom) : S(p.h);
    MoveWindow(p.hwnd, S(p.x), S(p.y) - g_scroll_y, w > 0 ? w : 1, h > 0 ? h : 1, TRUE);
  }
  InvalidateRect(hwnd, nullptr, TRUE);
}

bool DiscardChanges(HWND parent) {
  return !g_dirty || MessageBoxW(parent,
      L"Discard unsaved settings? Choose No to keep editing, then Save settings.",
      L"Unsaved settings", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
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
  cfg.comfort_vignette = id != CTL_PRESET_VETERAN;
  cfg.vignette_strength = id == CTL_PRESET_COMFORT ? 0.8f : 0.65f;
  cfg.snap_turn = id != CTL_PRESET_VETERAN;
  cfg.smooth_turn = !cfg.snap_turn;
  cfg.snap_turn_degrees = 45;
  if (id == CTL_PRESET_VETERAN) cfg.smooth_turn_speed_dps = 150.0f;
  cfg.render_scale_percent = id == CTL_PRESET_COMFORT ? 90 : id == CTL_PRESET_PERF ? 80 : 130;
  if (!dsvr::ConfigSave(path.c_str(), cfg)) {
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

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      g_heading = Add(hwnd, L"STATIC", L"DSVR", SS_LEFT, 20, 10, 300, 34, 0);
      SendMessageW(g_heading, WM_SETFONT, (WPARAM)g_title_font, TRUE);
      g_launch = Add(hwnd, L"BUTTON", L"Launch game", BS_PUSHBUTTON, 650, 12, 174, 32, CTL_LAUNCH);
      AddHint(hwnd, L"Dark Souls Remastered  |  Play offline while the mod is installed", 20, 46, 780, 22);
      Add(hwnd, L"BUTTON", L"1. Game & installation", BS_GROUPBOX, 16, 76, 808, 204, 0, 16);
      g_game_edit = Add(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 28, 104, 548, 26, CTL_GAME_EDIT);
      Add(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 586, 104, 100, 26, CTL_BROWSE);
      Add(hwnd, L"BUTTON", L"Auto-detect", BS_PUSHBUTTON, 696, 104, 116, 26, CTL_DETECT);
      g_status = Add(hwnd, L"STATIC", L"Detecting game...", SS_LEFT | SS_PATHELLIPSIS, 28, 138, 784, 20, CTL_STATUS, 28);
      g_brief = Add(hwnd, L"STATIC", L"", SS_LEFT, 28, 160, 784, 68, kHintBase + g_hint_id++, 28);
      Add(hwnd, L"BUTTON", L"Verify build", BS_PUSHBUTTON, 28, 234, 108, 28, CTL_VERIFY);
      Add(hwnd, L"BUTTON", L"Doctor", BS_PUSHBUTTON, 144, 234, 84, 28, CTL_DOCTOR);
      Add(hwnd, L"BUTTON", L"Install DSVR", BS_PUSHBUTTON, 236, 234, 120, 28, CTL_INSTALL);
      Add(hwnd, L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 364, 234, 100, 28, CTL_UNINSTALL);
      Add(hwnd, L"BUTTON", L"Open folder", BS_PUSHBUTTON, 472, 234, 110, 28, CTL_OPEN_FOLDER);
      g_del_cfg = Add(hwnd, L"BUTTON", L"Remove settings on uninstall", BS_AUTOCHECKBOX,
          596, 238, 216, 22, CTL_DEL_CFG);
      Add(hwnd, L"BUTTON", L"2. Settings", BS_GROUPBOX, 16, 290, 808, 312, 0, 16);
      Add(hwnd, L"BUTTON", L"Comfort", BS_PUSHBUTTON, 402, 310, 90, 26, CTL_PRESET_COMFORT);
      Add(hwnd, L"BUTTON", L"Performance", BS_PUSHBUTTON, 500, 310, 112, 26, CTL_PRESET_PERF);
      Add(hwnd, L"BUTTON", L"Veteran", BS_PUSHBUTTON, 620, 310, 90, 26, CTL_PRESET_VETERAN);
      AddHint(hwnd, L"Presets save immediately", 28, 314, 300, 20);
      g_vr = Add(hwnd, L"BUTTON", L"Enable VR (off = flatscreen)", BS_AUTOCHECKBOX, 28, 348, 260, 22, CTL_VR);
      g_vignette = Add(hwnd, L"BUTTON", L"Comfort vignette", BS_AUTOCHECKBOX, 302, 348, 170, 22, CTL_VIGNETTE);
      g_snap = Add(hwnd, L"BUTTON", L"Snap turn", BS_AUTOCHECKBOX, 482, 348, 110, 22, CTL_SNAP);
      g_haptics = Add(hwnd, L"BUTTON", L"Haptics", BS_AUTOCHECKBOX, 602, 348, 110, 22, CTL_HAPTICS);
      g_motion = Add(hwnd, L"BUTTON", L"Motion attacks (unqualified)", BS_AUTOCHECKBOX, 28, 380, 300, 22, CTL_MOTION);
      Add(hwnd, L"STATIC", L"Render %", SS_LEFT, 342, 382, 68, 22, 0);
      g_scale = Add(hwnd, L"EDIT", L"100", ES_NUMBER | WS_BORDER, 414, 378, 52, 26, CTL_SCALE);
      Add(hwnd, L"STATIC", L"Snap degrees", SS_LEFT, 482, 382, 98, 22, 0);
      g_snapdeg = Add(hwnd, L"EDIT", L"45", ES_NUMBER | WS_BORDER, 586, 378, 52, 26, CTL_SNAPDEG);
      Add(hwnd, L"STATIC", L"Weapon hand", SS_LEFT, 28, 418, 104, 22, 0);
      g_hand = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 136, 414, 118, 140, CTL_HAND);
      AddItems(g_hand, {L"Right", L"Left"});
      Add(hwnd, L"STATIC", L"Play style", SS_LEFT, 270, 418, 82, 22, 0);
      g_style = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 356, 414, 130, 140, CTL_STYLE);
      AddItems(g_style, {L"Standing", L"Seated", L"Roomscale"});
      Add(hwnd, L"STATIC", L"FPS fallback", SS_LEFT, 502, 418, 90, 22, 0);
      g_fps = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 596, 414, 210, 200, CTL_FPS);
      AddItems(g_fps, {L"60", L"72", L"80", L"90", L"120 (unqualified)", L"144 (unqualified)", L"240 (unqualified)"});
      Add(hwnd, L"STATIC", L"Move relative to", SS_LEFT, 28, 454, 118, 22, 0);
      g_move_mode = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 150, 450, 154, 140, CTL_MOVE_MODE);
      AddItems(g_move_mode, {L"Head (default)", L"Controller hand"});
      Add(hwnd, L"STATIC", L"Move hand", SS_LEFT, 320, 454, 86, 22, 0);
      g_move_hand = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 410, 450, 154, 140, CTL_MOVE_HAND);
      AddItems(g_move_hand, {L"Non-dominant", L"Left", L"Right"});
      AddHint(hwnd, L"Lost hand falls back to head.", 580, 454, 226, 22);
      Add(hwnd, L"STATIC", L"Body fit", SS_LEFT, 28, 490, 70, 22, 0);
      g_body_fit = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 104, 486, 206, 140, CTL_BODY_FIT);
      AddItems(g_body_fit, {L"Native head bone", L"Native arm reach"});
      Add(hwnd, L"STATIC", L"Arm tracking", SS_LEFT, 326, 490, 100, 22, 0);
      g_arm = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 430, 486, 376, 140, CTL_ARM);
      AddItems(g_arm, {L"Off (default)", L"Observe only (no pose writes)", L"Drive native arms (unqualified)"});
      Add(hwnd, L"STATIC", L"Other mod proxy (optional)", SS_LEFT, 28, 526, 212, 22, 0);
      g_chain = Add(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 246, 522, 560, 26, CTL_CHAIN, 34);
      AddHint(hwnd, L"FPS fallback is overridden by live runtime refresh; physics remains unqualified.", 28, 550, 778, 18);
      AddHint(hwnd, L"Calibrate height in VR. F7: live settings only; restart for attach/host changes.", 28, 574, 640, 24);
      Add(hwnd, L"BUTTON", L"Save settings", BS_PUSHBUTTON, 680, 568, 126, 28, CTL_SAVE);
      Add(hwnd, L"STATIC", L"Activity & diagnostics", SS_LEFT, 20, 616, 500, 22, 0);
      Add(hwnd, L"BUTTON", L"Clear log", BS_PUSHBUTTON, 716, 612, 108, 26, CTL_CLEAR_LOG);
      g_log = Add(hwnd, L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER | WS_VSCROLL,
          16, 646, 808, 98, CTL_LOG, 16, 16);
      SendMessageW(g_log, EM_SETLIMITTEXT, 131072, 0);
      GuiLog(L"DSVR " + dsvr::WideFromUtf8(dsvr::kModVersion) + L" -- Verify, Install, then play offline.");
      PopulateControls(dsvr::Config{});
      RefreshGame(true);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(wp), notification = HIWORD(wp);
      if (!g_suppress && id >= kFirstSetting && id <= kLastSetting &&
          (notification == BN_CLICKED || notification == EN_CHANGE || notification == CBN_SELCHANGE)) {
        MarkDirty();
        UpdateBrief();
        return 0;
      }
      if (id == CTL_GAME_EDIT && notification == EN_CHANGE) {
        g_path_resolved = false;
        UpdateBrief();
        return 0;
      }
      if (id == CTL_LAUNCH) {
        LaunchGame(hwnd);
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
      } else if (id == CTL_SAVE) {
        SaveSettings(false);
      } else if (id == CTL_PRESET_COMFORT || id == CTL_PRESET_PERF || id == CTL_PRESET_VETERAN) {
        ApplyPreset(id);
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
      } else if (id == CTL_INSTALL) {
        if (RefreshGame(false)) {
          setup::InstallOptions opts;
          opts.enable_vr_after = Checked(g_vr);
          if (!SaveSettings(true)) return 0;
          const int result = setup::DoInstall(g_exe, opts, MakeLog(), MakeConfirm(hwnd));
          if (result == 0) { g_exe.clear(); RefreshGame(true); }
          else GuiLog(L"Install did not complete. See diagnostics above.");
        }
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC dc = reinterpret_cast<HDC>(wp);
      HWND control = reinterpret_cast<HWND>(lp);
      const bool field = msg != WM_CTLCOLORSTATIC || control == g_log || InList(g_combos, control);
      const auto id = GetDlgCtrlID(control);
      SetTextColor(dc, control == g_heading ? kGold : id >= kHintBase ? kMuted : kInk);
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
            (item->itemState & ODS_SELECTED) ? kGold : kInk);
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
      RECT rc{0, 0, S(kClientWidth), S(540)};
      AdjustFrame(rc);
      auto* info = reinterpret_cast<MINMAXINFO*>(lp);
      info->ptMinTrackSize = {rc.right - rc.left, rc.bottom - rc.top};
      return 0;
    }
    case WM_CLOSE:
      if (DiscardChanges(hwnd)) DestroyWindow(hwnd);
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
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DSVRSetupGui";
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = g_backdrop_brush;
  const bool registered = RegisterClassW(&wc) != 0;
  RECT rc{0, 0, S(kClientWidth), S(kContentHeight)};
  AdjustFrame(rc);
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  int height = rc.bottom - rc.top;
  if (work.bottom > work.top && height > work.bottom - work.top) height = work.bottom - work.top;
  g_main = registered ? CreateWindowW(wc.lpszClassName, L"DSVR Setup -- Dark Souls: Remastered VR",
      WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
      rc.right - rc.left, height, nullptr, nullptr, wc.hInstance, nullptr) : nullptr;
  MSG msg{};
  int result = 0;
  if (g_main) {
    ShowWindow(g_main, SW_SHOW);
    UpdateWindow(g_main);
    HWND previous_focus = nullptr;
    while ((result = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
      if (!IsDialogMessageW(g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
      // Keyboard navigation must scroll an offscreen control into view too.
      HWND focus = GetFocus();
      if (focus != previous_focus && IsChild(g_main, focus)) {
        RECT child{}, client{};
        GetWindowRect(focus, &child);
        MapWindowPoints(nullptr, g_main, reinterpret_cast<POINT*>(&child), 2);
        GetClientRect(g_main, &client);
        if (child.top < 0 || child.bottom > client.bottom) {
          g_scroll_y += child.top < 0 ? child.top : child.bottom - client.bottom;
          Relayout(g_main);
        }
      }
      previous_focus = focus;
    }
    if (IsWindow(g_main)) DestroyWindow(g_main);
  }
  if (registered) UnregisterClassW(wc.lpszClassName, wc.hInstance);
  DeleteObject(g_font); g_font = nullptr;
  DeleteObject(g_title_font); g_title_font = nullptr;
  DeleteObject(g_backdrop_brush); g_backdrop_brush = nullptr;
  DeleteObject(g_field_brush); g_field_brush = nullptr;
  g_placements.clear(); g_frames.clear(); g_combos.clear(); g_toggles.clear();
  return !g_main || result < 0 ? 1 : (int)msg.wParam;
}

