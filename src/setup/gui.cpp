// DSVR-Setup GUI: friendly configurator window (no console needed).
// Double-click friendly: auto-detects the game, checks the build, edits
// DSVR.toml, installs/uninstalls with MessageBox confirmations. All heavy
// logic lives in setup.cpp (shared with the CLI).

#include "gui.h"

#include <windows.h>

#include <commdlg.h>
#include <shellapi.h>

#include <string>

#include "../common/config.h"
#include "../common/version.h"
#include "setup_internal.h"

namespace {

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
  CTL_VR = 110,
  CTL_VIGNETTE = 111,
  CTL_SNAP = 112,
  CTL_HAPTICS = 113,
  CTL_MOTION = 114,
  CTL_SCALE = 115,
  CTL_SNAPDEG = 116,
  CTL_EYE = 117,
  CTL_HAND = 118,
  CTL_STYLE = 119,
  CTL_CHAIN = 120,
  CTL_SAVE = 121,
  CTL_OPEN_FOLDER = 122,
  CTL_TOML_LABEL = 123,
  CTL_PRESET_COMFORT = 124,
  CTL_PRESET_PERF = 125,
  CTL_PRESET_VETERAN = 126,
  CTL_FPS = 127,
  CTL_MOVE_MODE = 128,
  CTL_MOVE_HAND = 129,
  CTL_BODY_FIT = 130,
};

HWND g_game_edit = nullptr, g_status = nullptr, g_log = nullptr;
HWND g_vr = nullptr, g_vignette = nullptr, g_snap = nullptr;
HWND g_haptics = nullptr, g_motion = nullptr;
HWND g_scale = nullptr, g_snapdeg = nullptr, g_eye = nullptr;
HWND g_hand = nullptr, g_style = nullptr, g_chain = nullptr;
HWND g_fps = nullptr;
HWND g_move_mode = nullptr, g_move_hand = nullptr;
HWND g_body_fit = nullptr;
HWND g_main = nullptr;
std::wstring g_exe;  // resolved exe path (empty = not found)

void GuiLog(const std::wstring& line) {
  if (!g_log) return;
  int len = GetWindowTextLengthW(g_log);
  SendMessageW(g_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
  std::wstring with_nl = line + L"\r\n";
  SendMessageW(g_log, EM_REPLACESEL, FALSE, (LPARAM)with_nl.c_str());
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

void PopulateControls(const dsvr::Config& c) {
  SetChecked(g_vr, c.vr_enabled);
  SetChecked(g_vignette, c.comfort_vignette);
  SetChecked(g_snap, c.snap_turn);
  SetChecked(g_haptics, c.haptics);
  SetChecked(g_motion, c.motion_attacks);
  SetText(g_scale, std::to_wstring(c.render_scale_percent));
  SetText(g_snapdeg, std::to_wstring(c.snap_turn_degrees));
  wchar_t eye[32]{};
  swprintf_s(eye, L"%.2f", (double)c.eye_height);
  SetText(g_eye, eye);
  SendMessageW(g_hand, CB_SETCURSEL, c.dominant_hand_left ? 1 : 0, 0);
  SendMessageW(g_move_mode, CB_SETCURSEL,
               c.movement_orientation == dsvr::MovementOrientation::Controller ? 1 : 0, 0);
  SendMessageW(g_move_hand, CB_SETCURSEL,
               c.movement_controller == dsvr::MovementController::Left
                   ? 1
               : c.movement_controller == dsvr::MovementController::Right ? 2 : 0,
               0);
   SendMessageW(g_body_fit, CB_SETCURSEL, c.embodiment_fit == "arm" ? 1 : 0, 0);
  {
    const int limits[] = {60, 72, 90, 120, 144, 240};
    int fps_idx = 0;
    for (int i = 0; i < 6; ++i)
      if (c.frame_limit_fps == limits[i]) fps_idx = i;
    SendMessageW(g_fps, CB_SETCURSEL, fps_idx, 0);
  }
  int style_idx = 0;
  if (c.play_style == "seated") style_idx = 1;
  if (c.play_style == "roomscale") style_idx = 2;
  SendMessageW(g_style, CB_SETCURSEL, style_idx, 0);
  SetText(g_chain, dsvr::WideFromUtf8(c.loader_chain_proxy));
}

dsvr::Config ReadControls(dsvr::Config base) {
  base.vr_enabled = Checked(g_vr);
  base.comfort_vignette = Checked(g_vignette);
  base.snap_turn = Checked(g_snap);
  base.haptics = Checked(g_haptics);
  base.motion_attacks = Checked(g_motion);
  int scale = _wtoi(GetText(g_scale).c_str());
  if (scale > 0) base.render_scale_percent = scale;
  int deg = _wtoi(GetText(g_snapdeg).c_str());
  if (deg > 0) base.snap_turn_degrees = deg;
  double eye = _wtof(GetText(g_eye).c_str());
  if (eye > 0) base.eye_height = (float)eye;
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
    const int limits[] = {60, 72, 90, 120, 144, 240};
    int fps_idx = (int)SendMessageW(g_fps, CB_GETCURSEL, 0, 0);
    if (fps_idx >= 0 && fps_idx < 6) base.frame_limit_fps = limits[fps_idx];
  }
  int style_idx = (int)SendMessageW(g_style, CB_GETCURSEL, 0, 0);
  base.play_style =
      style_idx == 1 ? "seated" : style_idx == 2 ? "roomscale" : "standing";
  base.loader_chain_proxy = dsvr::Utf8FromWide(GetText(g_chain));
  return base;
}

bool RefreshGame(bool log_it) {
  std::wstring typed = GetText(g_game_edit);
  g_exe = setup::FindGameExe(typed);
  if (g_exe.empty()) {
    SetText(g_status, L"Game not found -- use Browse or paste the folder.");
    if (log_it) GuiLog(L"Game not found. Browse to DarkSoulsRemastered.exe.");
    return false;
  }
  SetText(g_game_edit, g_exe);
  SetText(g_status, L"Game found. Click Verify, then Install.");
  if (log_it) GuiLog(L"Game: " + g_exe + L" Click Verify, then Install.");
  dsvr::Config cfg;
  std::wstring cfg_path = ConfigPathForGame();
  if (dsvr::ConfigLoad(cfg_path.c_str(), cfg)) {
    if (log_it) GuiLog(L"Loaded existing DSVR.toml settings. Review, then click Verify.");
  } else {
    if (log_it)
      GuiLog(L"No DSVR.toml yet -- showing defaults. Click Save to create it.");
  }
  PopulateControls(cfg);
  return true;
}

bool SaveSettings(bool silent) {
  if (g_exe.empty() && !RefreshGame(true)) return false;
  std::wstring cfg_path = ConfigPathForGame();
  dsvr::Config cfg;
  dsvr::ConfigLoad(cfg_path.c_str(), cfg);  // keep fields not shown in GUI
  cfg = ReadControls(cfg);
  // Clamp via a save/load round-trip so GUI and core agree exactly.
  if (!dsvr::ConfigSave(cfg_path.c_str(), cfg)) {
    GuiLog(L"FAIL: cannot write DSVR.toml (check permissions). Fix permissions, then retry Save.");
    return false;
  }
  dsvr::Config check;
  dsvr::ConfigLoad(cfg_path.c_str(), check);
  PopulateControls(check);
  if (!silent) GuiLog(L"Saved DSVR.toml. Restart the game to apply.");
  return true;
}

HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style,
         int x, int y, int w, int h, int id) {
  HWND hwnd = CreateWindowW(cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w,
                            h, parent, (HMENU)(INT_PTR)id, nullptr, nullptr);
  SendMessageW(hwnd, WM_SETFONT,
               (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
  return hwnd;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CREATE: {
      Add(hwnd, L"STATIC",
          L"DSVR -- Dark Souls: Remastered VR  (OFFLINE ONLY while installed)",
          SS_LEFT, 16, 10, 700, 20, 0);
      Add(hwnd, L"STATIC", L"Game (DarkSoulsRemastered.exe):", SS_LEFT, 16,
          38, 220, 20, 0);
      g_game_edit = Add(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 16,
                        60, 500, 24, CTL_GAME_EDIT);
      Add(hwnd, L"BUTTON", L"Browse...", BS_PUSHBUTTON, 524, 60, 90, 24,
          CTL_BROWSE);
      Add(hwnd, L"BUTTON", L"Auto-detect", BS_PUSHBUTTON, 622, 60, 100, 24,
          CTL_DETECT);
      g_status = Add(hwnd, L"STATIC", L"Detecting game... Click Auto-detect or Browse if this persists.", SS_LEFT, 16, 90,
                     700, 20, CTL_STATUS);

      Add(hwnd, L"BUTTON", L"Verify build", BS_PUSHBUTTON, 16, 118, 110, 28,
          CTL_VERIFY);
      Add(hwnd, L"BUTTON", L"Doctor", BS_PUSHBUTTON, 134, 118, 90, 28,
          CTL_DOCTOR);
      Add(hwnd, L"BUTTON", L"Install DSVR", BS_PUSHBUTTON, 232, 118, 120, 28,
          CTL_INSTALL);
      Add(hwnd, L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 360, 118, 100, 28,
          CTL_UNINSTALL);
      Add(hwnd, L"BUTTON", L"Open game folder", BS_PUSHBUTTON, 468, 118, 130,
          28, CTL_OPEN_FOLDER);
      Add(hwnd, L"BUTTON", L"Save settings", BS_PUSHBUTTON, 606, 118, 110, 28,
          CTL_SAVE);

      Add(hwnd, L"STATIC", L"Settings (saved to DSVR.toml):", SS_LEFT, 16,
          156, 240, 20, 0);
      Add(hwnd, L"BUTTON", L"Comfort", BS_PUSHBUTTON, 400, 154, 90, 24,
          CTL_PRESET_COMFORT);
      Add(hwnd, L"BUTTON", L"Performance", BS_PUSHBUTTON, 498, 154, 110, 24,
          CTL_PRESET_PERF);
      Add(hwnd, L"BUTTON", L"Veteran", BS_PUSHBUTTON, 616, 154, 100, 24,
          CTL_PRESET_VETERAN);
      g_vr = Add(hwnd, L"BUTTON", L"Enable VR (off = safe flatscreen)",
                 BS_AUTOCHECKBOX, 16, 178, 250, 20, CTL_VR);
      g_vignette = Add(hwnd, L"BUTTON", L"Comfort vignette",
                       BS_AUTOCHECKBOX, 280, 178, 160, 20, CTL_VIGNETTE);
      g_snap = Add(hwnd, L"BUTTON", L"Snap turn", BS_AUTOCHECKBOX, 450, 178,
                   100, 20, CTL_SNAP);
      g_haptics = Add(hwnd, L"BUTTON", L"Haptics", BS_AUTOCHECKBOX, 560, 178,
                      90, 20, CTL_HAPTICS);
      g_motion = Add(hwnd, L"BUTTON", L"Motion attacks (IK arms)",
                     BS_AUTOCHECKBOX, 16, 202, 250, 20, CTL_MOTION);
      Add(hwnd, L"STATIC", L"Render %:", SS_LEFT, 280, 202, 60, 20, 0);
      g_scale = Add(hwnd, L"EDIT", L"100", ES_NUMBER | WS_BORDER, 340, 200,
                    50, 22, CTL_SCALE);
      Add(hwnd, L"STATIC", L"Snap deg:", SS_LEFT, 400, 202, 60, 20, 0);
      g_snapdeg = Add(hwnd, L"EDIT", L"45", ES_NUMBER | WS_BORDER, 460, 200,
                      50, 22, CTL_SNAPDEG);
      Add(hwnd, L"STATIC", L"Eye m:", SS_LEFT, 520, 202, 50, 20, 0);
      g_eye = Add(hwnd, L"EDIT", L"1.65", WS_BORDER, 570, 200, 60, 22,
                  CTL_EYE);

      Add(hwnd, L"STATIC", L"Weapon hand:", SS_LEFT, 16, 232, 90, 20, 0);
      g_hand = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 106,
                   230, 110, 100, CTL_HAND);
      SendMessageW(g_hand, CB_ADDSTRING, 0, (LPARAM)L"Right");
      SendMessageW(g_hand, CB_ADDSTRING, 0, (LPARAM)L"Left");
      SendMessageW(g_hand, CB_SETCURSEL, 0, 0);
      Add(hwnd, L"STATIC", L"Play style:", SS_LEFT, 230, 232, 70, 20, 0);
      g_style = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 300,
                    230, 120, 100, CTL_STYLE);
      SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"Standing");
      SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"Seated");
      SendMessageW(g_style, CB_ADDSTRING, 0, (LPARAM)L"Roomscale");
      SendMessageW(g_style, CB_SETCURSEL, 0, 0);
      Add(hwnd, L"STATIC", L"Frame limit:", SS_LEFT, 16, 258, 80, 20, 0);
      g_fps = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 106,
                  256, 150, 120, CTL_FPS);
      SendMessageW(g_fps, CB_ADDSTRING, 0, (LPARAM)L"60 (native)");
      SendMessageW(g_fps, CB_ADDSTRING, 0, (LPARAM)L"72");
      SendMessageW(g_fps, CB_ADDSTRING, 0, (LPARAM)L"90");
      SendMessageW(g_fps, CB_ADDSTRING, 0,
                   (LPARAM)L"120 (possibly unsafe)");
      SendMessageW(g_fps, CB_ADDSTRING, 0,
                   (LPARAM)L"144 (possibly unsafe)");
      SendMessageW(g_fps, CB_ADDSTRING, 0,
                   (LPARAM)L"240 (possibly unsafe)");
      SendMessageW(g_fps, CB_SETCURSEL, 0, 0);
      Add(hwnd, L"STATIC", L"Other mod proxy file (optional):", SS_LEFT, 434, 232, 80, 20, 0);
      g_chain = Add(hwnd, L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, 514, 230,
                    208, 22, CTL_CHAIN);

  Add(hwnd, L"STATIC", L"Left stick oriented by:", SS_LEFT, 16, 282, 130, 20, 0);
  g_move_mode = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 150, 280,
                    140, 110, CTL_MOVE_MODE);
  SendMessageW(g_move_mode, CB_ADDSTRING, 0, (LPARAM)L"Head (default)");
  SendMessageW(g_move_mode, CB_ADDSTRING, 0, (LPARAM)L"Controller hand");
  SendMessageW(g_move_mode, CB_SETCURSEL, 0, 0);
  Add(hwnd, L"STATIC", L"Hand:", SS_LEFT, 300, 282, 40, 20, 0);
  g_move_hand = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 344, 280,
                    140, 110, CTL_MOVE_HAND);
  SendMessageW(g_move_hand, CB_ADDSTRING, 0, (LPARAM)L"Non-dominant");
  SendMessageW(g_move_hand, CB_ADDSTRING, 0, (LPARAM)L"Left");
  SendMessageW(g_move_hand, CB_ADDSTRING, 0, (LPARAM)L"Right");
  SendMessageW(g_move_hand, CB_SETCURSEL, 0, 0);
  Add(hwnd, L"STATIC",
      L"Movement stays head-oriented if that hand is lost.", SS_LEFT, 494, 282,
      228, 20, 0);

    Add(hwnd, L"STATIC", L"Body fit:", SS_LEFT, 16, 306, 80, 20, 0);
    g_body_fit = Add(hwnd, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_BORDER, 106,
                   304, 190, 110, CTL_BODY_FIT);
    SendMessageW(g_body_fit, CB_ADDSTRING, 0, (LPARAM)L"Native head bone (default)");
    SendMessageW(g_body_fit, CB_ADDSTRING, 0, (LPARAM)L"Native arm reach");
    SendMessageW(g_body_fit, CB_SETCURSEL, 0, 0);
    Add(hwnd, L"STATIC",
      L"Head fit puts the camera in the avatar's own head; arm fit matches your hands.",
      SS_LEFT, 306, 306, 416, 20, 0);

      Add(hwnd, L"STATIC",
          L"F9 in-game = log marker for bug reports. F7 = reload settings. "
          L"Hotkeys need the game focused.",
          SS_LEFT, 280, 258, 440, 20, CTL_TOML_LABEL);
      g_log = Add(hwnd, L"EDIT", L"",
                  ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER |
                      WS_VSCROLL,
                  16, 332, 706, 252, CTL_LOG);
      GuiLog(L"DSVR Setup " +
             dsvr::WideFromUtf8(dsvr::kModVersion) +
             L" -- offline only while installed. Click Verify, then Install.");
      GuiLog(L"1) Verify build  2) Install  3) Steam Offline -> play.");
      RefreshGame(true);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(wp);
      if (id == CTL_BROWSE) {
        wchar_t file[32768]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Dark Souls Remastered\0DarkSoulsRemastered.exe\0"
                          L"Executables\0*.exe\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = 32767;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
          SetText(g_game_edit, file);
          RefreshGame(true);
        }
      } else if (id == CTL_DETECT) {
        SetText(g_game_edit, L"");
        if (!RefreshGame(true))
          GuiLog(L"Auto-detect failed. Browse to the game exe instead.");
      } else if (id == CTL_VERIFY) {
        if (RefreshGame(false))
          setup::VerifyExe(g_exe, MakeLog());
      } else if (id == CTL_DOCTOR) {
        if (RefreshGame(false)) setup::DoctorChecks(g_exe, MakeLog());
      } else if (id == CTL_SAVE) {
        SaveSettings(false);
      } else if (id == CTL_PRESET_COMFORT || id == CTL_PRESET_PERF ||
                 id == CTL_PRESET_VETERAN) {
        // One-click presets. Apply to controls; Save persists.
        dsvr::Config preset;
        if (id == CTL_PRESET_COMFORT) {
          preset.comfort_vignette = true;
          preset.vignette_strength = 0.8f;
          preset.snap_turn = true;
          preset.smooth_turn = false;
          preset.snap_turn_degrees = 45;
          preset.render_scale_percent = 90;
        } else if (id == CTL_PRESET_PERF) {
          preset.comfort_vignette = true;
          preset.vignette_strength = 0.65f;
          preset.snap_turn = true;
          preset.smooth_turn = false;
          preset.snap_turn_degrees = 45;
          preset.render_scale_percent = 80;
        } else {
          preset.comfort_vignette = false;
          preset.vignette_strength = 0.65f;
          preset.snap_turn = false;
          preset.smooth_turn = true;
          preset.smooth_turn_speed_dps = 150.0f;
          preset.render_scale_percent = 130;
        }
        auto ApplyPreset = [](dsvr::Config& dst, const dsvr::Config& src) {
          dst.comfort_vignette = src.comfort_vignette;
          dst.vignette_strength = src.vignette_strength;
          dst.snap_turn = src.snap_turn;
          dst.smooth_turn = src.smooth_turn;
          dst.snap_turn_degrees = src.snap_turn_degrees;
          dst.smooth_turn_speed_dps = src.smooth_turn_speed_dps;
          dst.render_scale_percent = src.render_scale_percent;
        };
        // Keep hand/style/motion from current controls.
        dsvr::Config cur;
        dsvr::ConfigLoad(ConfigPathForGame().c_str(), cur);
        cur = ReadControls(cur);
        ApplyPreset(cur, preset);
        PopulateControls(cur);
        // Vignette strength / smooth speed have no visible controls, so
        // persist the preset now (visible fields included) to not lose them.
        if (!g_exe.empty()) {
          dsvr::Config on_disk;
          dsvr::ConfigLoad(ConfigPathForGame().c_str(), on_disk);
          on_disk = ReadControls(on_disk);
          ApplyPreset(on_disk, preset);
          dsvr::ConfigSave(ConfigPathForGame().c_str(), on_disk);
        }
        GuiLog(id == CTL_PRESET_COMFORT
                   ? L"Preset: Comfort applied + saved."
                   : id == CTL_PRESET_PERF
                         ? L"Preset: Performance applied + saved."
                         : L"Preset: Veteran applied + saved.");
      } else if (id == CTL_OPEN_FOLDER) {
        if (RefreshGame(false))
          ShellExecuteW(hwnd, L"open", setup::DirNameOf(g_exe).c_str(),
                        nullptr, nullptr, SW_SHOWNORMAL);
      } else if (id == CTL_UNINSTALL) {
        if (RefreshGame(false)) {
          setup::DoUninstall(g_exe, false, false, MakeLog(),
                             MakeConfirm(hwnd));
          RefreshGame(false);
        }
      } else if (id == CTL_INSTALL) {
        if (RefreshGame(false)) {
          // Persist GUI choices first so install keeps them.
          dsvr::Config cfg;
          std::wstring cfg_path = ConfigPathForGame();
          dsvr::ConfigLoad(cfg_path.c_str(), cfg);
          bool want_vr = Checked(g_vr);
          setup::InstallOptions opts;
          opts.enable_vr_after = want_vr;
          setup::DoInstall(g_exe, opts, MakeLog(), MakeConfirm(hwnd));
          // Apply the remaining GUI fields on top of what install wrote.
          if (setup::PathExists(cfg_path)) {
            dsvr::Config after;
            dsvr::ConfigLoad(cfg_path.c_str(), after);
            after = ReadControls(after);
            after.vr_enabled = want_vr;
            dsvr::ConfigSave(cfg_path.c_str(), after);
            PopulateControls(after);
          }
          RefreshGame(false);
        }
      }
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int RunGui() {
  // Hide the console window that the console-subsystem exe starts with.
  if (HWND console = GetConsoleWindow()) ShowWindow(console, SW_HIDE);

  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DSVRSetupGui";
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassW(&wc);

  g_main = CreateWindowW(
      wc.lpszClassName, L"DSVR Setup -- Dark Souls: Remastered VR",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
      CW_USEDEFAULT, 754, 664, nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_main) return 1;
  ShowWindow(g_main, SW_SHOW);
  UpdateWindow(g_main);

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    if (!IsDialogMessageW(g_main, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  return 0;
}

