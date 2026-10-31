// Standalone native launcher regression. Compile with user32/gdi32/shell32/comdlg32.
// All game/config/process operations are inert test doubles; controls stay hidden.
#include <windows.h>
#include <cstdio>
#include <string>

static int launches = 0;
static std::wstring launched_exe, launched_dir;
static BOOL WINAPI ThemeTestCreateProcessW(LPCWSTR exe, LPWSTR, LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR dir, LPSTARTUPINFOW,
    LPPROCESS_INFORMATION process) {
  ++launches; launched_exe = exe; launched_dir = dir;
  process->hProcess = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  process->hThread = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  process->dwProcessId = 123;
  return TRUE;
}
#define CreateProcessW ThemeTestCreateProcessW
static int message_box_answer = IDYES;
static int WINAPI ThemeTestMessageBoxW(HWND, LPCWSTR, LPCWSTR, UINT) {
  return message_box_answer;
}
#define MessageBoxW ThemeTestMessageBoxW
#include "../src/setup/gui.cpp"
#undef CreateProcessW
#undef MessageBoxW

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
dsvr::Config fixture{};
setup::InstallState install_state{true, true};
setup::FpsUnlockState fps_unlock_state{};
int fps_unlock_installs = 0;
int saves = 0;
bool verify_fail_next = false;
int installs = 0;
bool chain_conflict_next = false;
bool chain_move_fail_next = false;
int fps_unlock_install_result = 0;
  int tpup_downloads = 0;
const std::wstring fake_exe = L"C:\\DSVR-Theme-Test\\DarkSoulsRemastered.exe";
dsvr::HardwareSpec probed_hardware{32, 12, true};
int stub_headset_hz = 90;
}
namespace dsvr {
bool ConfigLoad(const wchar_t*, Config& out) { out = fixture; return true; }
bool ConfigSave(const wchar_t*, const Config& value) { fixture = value; ++saves; return true; }
bool ConfigSaveDefaults(const wchar_t*) { return true; }
int ReadLastHeadsetHz(const wchar_t*) { return stub_headset_hz; }
bool WriteLastHeadsetHz(const wchar_t*, int) { return true; }
std::wstring WideFromUtf8(const std::string& value) { return {value.begin(), value.end()}; }
std::string Utf8FromWide(const std::wstring& value) {
  std::string ascii;
  for (wchar_t ch : value) ascii.push_back(static_cast<char>(ch));
  return ascii;  // All fixture strings are ASCII; no locale/file I/O in the stub.
}
}
namespace setup {
std::wstring JoinPath(const std::wstring& dir, const std::wstring& file) { return dir + L"\\" + file; }
std::wstring DirNameOf(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/")); }
bool PathExists(const std::wstring& path) { return !path.empty(); }
std::wstring FindGameExe(const std::wstring& value) { return value == L"invalid" ? L"" : fake_exe; }
bool IsGameRunning() { return false; }
dsvr::HardwareSpec ProbeHardware() { return probed_hardware; }
bool VerifyExe(const std::wstring&, const LogFn&) {
  if (verify_fail_next) { verify_fail_next = false; return false; }
  return true;
}
InstallState InstallStateOf(const std::wstring&) { return install_state; }
std::vector<std::wstring> DoctorChecks(const std::wstring&, const LogFn&) { return {}; }
int DoInstall(const std::wstring& exe, const InstallOptions&, const LogFn& log, const ConfirmFn&) {
  ++installs;
  // Stubbed chain-conflict branch mirrors src/setup/setup.cpp's DoInstall
  // foreign-proxy path (GREEN: both log the full colliding path).
  const std::wstring dir = exe.substr(0, exe.find_last_of(L"\\/"));
  if (chain_conflict_next) {
    chain_conflict_next = false;
    const std::wstring chain_dest = dir + L"\\dinput8-chain.dll";
    log(L"FAIL: " + chain_dest +
        L" already exists. Resolve the conflict by hand, then run "
        L"Install again. Nothing was changed.");
    return 1;
  }
  if (chain_move_fail_next) {
    chain_move_fail_next = false;
    const std::wstring dest_loader = dir + L"\\dinput8.dll";
    const std::wstring chain_dest = dir + L"\\dinput8-chain.dll";
    log(L"FAIL: could not move " + dest_loader + L" to " + chain_dest +
        L". Nothing was changed.");
    return 1;
  }
  return 0;
}
int DoUninstall(const std::wstring&, bool, bool, const LogFn&, const ConfirmFn&) { return 0; }
FpsUnlockState FpsUnlockStateOf(const std::wstring&) { return fps_unlock_state; }
int InstallFpsUnlock(const std::wstring&, const LogFn&, const ConfirmFn&) {
  ++fps_unlock_installs;
  return fps_unlock_install_result;
}

int DownloadTextureTools(const LogFn&, const ConfirmFn&, std::wstring& folder) {
  ++tpup_downloads;
  folder = L"Downloads TPUP";
  return 0;
}
}

static bool HasVisibleStyle(HWND window) {
  return window && (GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE) != 0;
}

int main() {
  CHECK(kBackdrop == RGB(9, 9, 11));
  CHECK(kField == RGB(20, 20, 22));
  CHECK(kInk == RGB(237, 237, 240));
  CHECK(kEmber == RGB(200, 197, 212));
  fixture.frame_limit_fps = 80;
  fixture.physical_attacks = true;
  fixture.motion_attacks = false;
  fixture.snap_turn = false;
  fixture.smooth_turn = false;
  g_dpi = 96;
  RecreateFonts();
  g_backdrop_brush = CreateSolidBrush(kBackdrop);
  g_field_brush = CreateSolidBrush(kField);
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"DSVRThemeTest"; wc.hbrBackground = g_backdrop_brush;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  CHECK(RegisterClassW(&wc));
  RECT outer{0, 0, kClientWidth, ContentHeight()}; AdjustFrame(outer);
  g_main = CreateWindowW(wc.lpszClassName,
      L"DSVR Theme Preview -- mocked game/config (no real launch)",
      WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, 40, 40,
      outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, wc.hInstance, nullptr);
  // Keep the top-level window hidden; child visibility styles still drive
  // layout assertions without exposing a test window.
  CHECK(g_main && !IsWindowVisible(g_main));
  CHECK(g_brief && g_launch && g_toggles.size() == 7);
  CHECK(GetText(g_launch) == L"Verify game");
  CHECK(g_native_launch && !IsWindowEnabled(g_native_launch));
  CHECK(GetText(g_nav_home) == L"Home");
  CHECK(GetText(g_nav_game) == L"Game & mods");
  CHECK(GetText(g_nav_settings) == L"Mod settings");
  CHECK(GetText(g_nav_activity) == L"Help");
  LOGFONTW font_family{};
  CHECK(GetObjectW(g_font, sizeof(font_family), &font_family));
  CHECK(std::wstring(font_family.lfFaceName) == L"Segoe UI");
  CHECK(GetObjectW(g_title_font, sizeof(font_family), &font_family));
  CHECK(std::wstring(font_family.lfFaceName) == L"Segoe UI");
  CHECK(GetObjectW(g_section_font, sizeof(font_family), &font_family));
  CHECK(std::wstring(font_family.lfFaceName) == L"Segoe UI");
  CHECK(GetObjectW(g_hint_font, sizeof(font_family), &font_family));
  CHECK(std::wstring(font_family.lfFaceName) == L"Segoe UI");
  CHECK((GetWindowLongPtrW(g_motion, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX);
  CHECK(GetWindowLongPtrW(g_motion, GWL_STYLE) & WS_TABSTOP);
  CHECK((GetWindowLongPtrW(g_launch, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW);
  CHECK(!Checked(g_motion));
  CHECK(GetText(g_motion).find(L"Swing uses attack button") != std::wstring::npos);
  CHECK(GetText(g_brief).find(L"Headset and runtime have not been checked") != std::wstring::npos &&
        GetText(g_mode).find(L"VR preview (mono)") != std::wstring::npos &&
        GetText(g_mode).find(L"Play in VR") == std::wstring::npos);

  CHECK(GetText(g_brief).find(L"Game version not verified") != std::wstring::npos);
  CHECK(g_page == SetupPage::Home);
  CHECK(HasVisibleStyle(g_status) && HasVisibleStyle(g_brief));
  CHECK(HasVisibleStyle(g_nav_home) && !HasVisibleStyle(g_game_edit));
  CHECK(!HasVisibleStyle(g_mode) && !HasVisibleStyle(g_log));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_NAV_SETTINGS, BN_CLICKED), 0);
  CHECK(g_page == SetupPage::Settings && HasVisibleStyle(g_mode));
      CHECK(HasVisibleStyle(g_advanced_toggle) && !HasVisibleStyle(g_advanced_summary));
      SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_ADVANCED_TOGGLE, BN_CLICKED), 0);
      CHECK(g_advanced_expanded && HasVisibleStyle(g_advanced_summary));
      CHECK(GetText(g_advanced_summary).find(L"Quest 3 controller bindings") != std::wstring::npos);
      SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_ADVANCED_TOGGLE, BN_CLICKED), 0);
      CHECK(!g_advanced_expanded && !HasVisibleStyle(g_advanced_summary));
  // The heading, Launch button and page tabs are chrome: they stay pinned
  // while a long page scrolls, or the tabs slide off the top and there is no
  // way back out of the VR settings page.
  auto client_top = [](HWND control) {
    RECT r{};
    GetWindowRect(control, &r);
    POINT origin{0, 0};
    ClientToScreen(g_main, &origin);
    return r.top - origin.y;
  };
  {
    const int heading = client_top(g_heading);
    const int tabs = client_top(g_nav_settings);
    CHECK(heading == S(10) && tabs == S(46));
    // Force a scrollable page: a tall VR settings page is what used to push
    // the page tabs out of reach, leaving no way back to another page.
    RECT short_want{0, 0, S(kClientWidth), S(480)};
    AdjustFrame(short_want);
    SetWindowPos(g_main, nullptr, 40, 40, short_want.right - short_want.left,
                 short_want.bottom - short_want.top, SWP_NOZORDER);
    SendMessageW(g_main, WM_VSCROLL, SB_BOTTOM, 0);
    CHECK(g_scroll_y > 0);
    CHECK(HasVisibleStyle(g_heading) && HasVisibleStyle(g_launch));
    CHECK(HasVisibleStyle(g_nav_home) && HasVisibleStyle(g_nav_game) &&
          HasVisibleStyle(g_nav_settings) && HasVisibleStyle(g_nav_activity));
    CHECK(client_top(g_heading) == heading);
    CHECK(client_top(g_nav_settings) == tabs);
    for (const auto& p : g_placements) {
      if (p.page == SetupPage::Shared || !HasVisibleStyle(p.hwnd)) continue;
      CHECK(client_top(p.hwnd) >= S(kChromeBottom));
    }
    SendMessageW(g_main, WM_VSCROLL, SB_TOP, 0);
    CHECK(g_scroll_y == 0);
      ResizeContent(g_main);
  }
  // Simple view: the developer block is gone, so the window is shorter and
  // the hidden tuning controls are parked offscreen.
  {
    RECT now{};
    GetWindowRect(g_main, &now);
    RECT want{0, 0, S(kClientWidth), S(ContentHeight())};
    AdjustFrame(want);
    CHECK(now.right - now.left == want.right - want.left);
    CHECK(now.bottom - now.top == want.bottom - want.top);
    CHECK(!HasVisibleStyle(g_scale) && !HasVisibleStyle(g_arm));
    // The retired frame-cap control (id 140) must not come back. Turning,
    // vignette and walk steering are comfort choices a player owns, so they
    // stay in the simple view; scale, snap step, arms and the other-mod
    // proxy are tuning internals and do not.
    CHECK(HasVisibleStyle(g_snap) && GetDlgItem(g_main, 140) == nullptr);
    CHECK(HasVisibleStyle(g_vignette) && HasVisibleStyle(g_move_mode));
    CHECK(!HasVisibleStyle(g_snapdeg) && !HasVisibleStyle(g_chain));
    CHECK(HasVisibleStyle(g_debugcap) && HasVisibleStyle(g_hand));
    CHECK(HasVisibleStyle(g_motion) && HasVisibleStyle(g_style));
    CHECK(HasVisibleStyle(g_mode));
  }
  // Walk the shown controls: anything with a positive-area intersection is
  // a layout bug. Hint captions sit under their control by design; label
  // statics tile rows. Only same-kind control overlaps count.
  auto HasLayoutOverlap = [](const char* tag) {
    std::vector<std::pair<HWND, RECT>> shown;
    for (const auto& p : g_placements) {
      if (!IsWindow(p.hwnd) || !HasVisibleStyle(p.hwnd)) continue;
      RECT r{};
      GetWindowRect(p.hwnd, &r);
      MapWindowPoints(nullptr, g_main, reinterpret_cast<POINT*>(&r), 2);
      if (r.top < -1000) continue;  // parked hidden developer row
      shown.emplace_back(p.hwnd, r);
    }
    bool bad = false;
    for (size_t i = 0; i < shown.size(); ++i)
      for (size_t j = i + 1; j < shown.size(); ++j) {
        const int idi = GetDlgCtrlID(shown[i].first);
        const int idj = GetDlgCtrlID(shown[j].first);
        const bool i_hint = idi >= 900, j_hint = idj >= 900;
        if (i_hint != j_hint) continue;
        if (idi == 0 && idj == 0) continue;
        RECT a = shown[i].second, b = shown[j].second;
        const bool a_combo = GetWindowLongPtrW(shown[i].first, GWL_STYLE) & CBS_DROPDOWNLIST;
        const bool b_combo = GetWindowLongPtrW(shown[j].first, GWL_STYLE) & CBS_DROPDOWNLIST;
        if (a_combo) a.bottom = a.top + S(26);
        if (b_combo) b.bottom = b.top + S(26);
        RECT inter{};
        const bool overlap = IntersectRect(&inter, &a, &b) != FALSE;
        const bool area = overlap && inter.right > inter.left && inter.bottom > inter.top;
        if (area) {
          std::printf("%s OVERLAP %d vs %d a=(%ld,%ld,%ld,%ld) b=(%ld,%ld,%ld,%ld)\n",
                      tag, idi, idj, a.left, a.top, a.right, a.bottom,
                      b.left, b.top, b.right, b.bottom);
          bad = true;
        }
      }
    return bad;
  };
  CHECK(!HasLayoutOverlap("SIMPLE"));
  // Hardware auto-fit presets: probe stub reports 32 GB / 12 GB / discrete,
  // Native BUTTON handles activation and owns its check state. The themed paint
  // must neither implement a parallel bool nor toggle a second time on BN_CLICKED.
  SendMessageW(g_motion, BM_CLICK, 0, 0);
  CHECK(Checked(g_motion));
  CHECK(Checked(g_motion));
  SendMessageW(g_motion, BM_CLICK, 0, 0);
  CHECK(!Checked(g_motion));
  const auto edited = ReadControls(fixture);
  CHECK(edited.physical_attacks && !edited.motion_attacks && !edited.smooth_turn);
  CHECK(edited.frame_limit_fps == 80);
  // One steering combo: head, or a controller hand with head fallback.
  // Index 0 = head, 1 = non-dominant, 2 = left, 3 = right.
  SendMessageW(g_move_mode, CB_SETCURSEL, 3, 0);
  {
    const auto steered = ReadControls(fixture);
    CHECK(steered.movement_orientation == dsvr::MovementOrientation::Controller);
    CHECK(steered.movement_controller == dsvr::MovementController::Right);
  }
  SendMessageW(g_move_mode, CB_SETCURSEL, 0, 0);
  {
    const auto steered = ReadControls(fixture);
    CHECK(steered.movement_orientation == dsvr::MovementOrientation::Head);
  }
  // Body visibility: one combo over three config fields. Each index must
  // write exactly the field combination its label promises, and loading a
  // file must select the index that describes it -- the panel can never show
  // "show all of it" while the file hides the head.
  {
    CHECK(GetDlgItem(g_main, CTL_BODY) != nullptr);
    for (int index = 0; index <= 3; ++index) {
      SendMessageW(g_body, CB_SETCURSEL, index, 0);
      const auto body = ReadControls(fixture);
      CHECK(body.hide_player_model == (index == 3));
      CHECK(body.hide_head == (index >= 1));
      CHECK(body.hide_torso_and_legs == (index >= 2));
    }
    dsvr::Config shown = fixture;
    shown.hide_player_model = shown.hide_head = shown.hide_torso_and_legs = false;
    PopulateControls(shown);
    CHECK((int)SendMessageW(g_body, CB_GETCURSEL, 0, 0) == 0);
    shown.hide_head = true;
    PopulateControls(shown);
    CHECK((int)SendMessageW(g_body, CB_GETCURSEL, 0, 0) == 1);
    shown.hide_torso_and_legs = true;
    PopulateControls(shown);
    CHECK((int)SendMessageW(g_body, CB_GETCURSEL, 0, 0) == 2);
    shown.hide_player_model = true;
    PopulateControls(shown);
    CHECK((int)SendMessageW(g_body, CB_GETCURSEL, 0, 0) == 3);
    PopulateControls(fixture);
  }
  CHECK(!HasLayoutOverlap("SIMPLE"));
  // Instant-apply: the two BM_CLICK toggles above each saved once.
  CHECK(saves == 2 && fixture.physical_attacks);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_VERIFY, BN_CLICKED), 0);
  CHECK(g_verified_ok && GetText(g_launch) == L"Launch game");
  // Exercise the actual WM_COMMAND launch wiring, but CreateProcessW above is a
  // local test double: this test cannot launch a game or deploy anything.
  // Steam DRM: the launch routes through the Steam client (rungameid), not
  // the raw exe, so Steam init succeeds whether Steam is open or not.
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_LAUNCH, BN_CLICKED), 0);
  CHECK(launches == 1);
  CHECK(launched_exe.find(L"Steam.exe") != std::wstring::npos);
  CHECK(launched_dir == L"C:\\DSVR-Theme-Test");
  install_state = {false, false};
  UpdateBrief();
  CHECK(GetText(g_launch) == L"Set up DSVR" && IsWindowEnabled(g_native_launch));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_LAUNCH, BN_CLICKED), 0);
  CHECK(g_page == SetupPage::Game && launches == 1);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_LAUNCH_NATIVE, BN_CLICKED), 0);
  CHECK(launches == 2);
  install_state = {true, true};
  UpdateBrief();
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_NAV_SETTINGS, BN_CLICKED), 0);
  SetText(g_game_edit, L"invalid");
  CHECK(!RefreshGame(false) && IsWindowEnabled(g_launch));
  CHECK(GetText(g_launch) == L"Find game");
  SetText(g_game_edit, fake_exe); CHECK(RefreshGame(false));
  install_state.has_core = false; UpdateBrief();
  CHECK(IsWindowEnabled(g_launch));
  CHECK(GetText(g_launch) == L"Repair setup");
  CHECK(GetText(g_brief).find(L"setup is incomplete") != std::wstring::npos);
  install_state.has_core = true; UpdateBrief();
  CHECK(IsWindowEnabled(g_launch) && GetText(g_launch) == L"Launch game");
  // Install gate: a failed Verify must disable Install and refuse install.
  verify_fail_next = true;
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_VERIFY, BN_CLICKED), 0);
  HWND install_btn = GetDlgItem(g_main, CTL_INSTALL);
  CHECK(install_btn && !IsWindowEnabled(install_btn));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_INSTALL, BN_CLICKED), 0);
  CHECK(installs == 0);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_VERIFY, BN_CLICKED), 0);
  CHECK(IsWindowEnabled(install_btn));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_INSTALL, BN_CLICKED), 0);
  CHECK(installs == 1);
  // Chain-conflict FAILs must name the full colliding path (RED: fails until
  // the production messages carry the game-dir + exact filename).
  chain_conflict_next = true;
  const int conflict_base = installs;
  SetText(g_log, L"");
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_INSTALL, BN_CLICKED), 0);
  CHECK(installs == conflict_base + 1);
  CHECK(GetText(g_log).find(L"dinput8-chain.dll") != std::wstring::npos);
  CHECK(GetText(g_log).find(L"C:\\DSVR-Theme-Test\\dinput8-chain.dll") != std::wstring::npos);
  chain_move_fail_next = true;
  SetText(g_log, L"");
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_INSTALL, BN_CLICKED), 0);
  CHECK(GetText(g_log).find(L"dinput8.dll") != std::wstring::npos);
  CHECK(GetText(g_log).find(L"C:\\DSVR-Theme-Test\\dinput8.dll") != std::wstring::npos);
  CHECK(GetText(g_log).find(L"C:\\DSVR-Theme-Test\\dinput8-chain.dll") != std::wstring::npos);

  // Frame-rate dependency button: present, clickable while absent, and it must
  // not offer a second download once the chain is in place.
  HWND fps_btn = GetDlgItem(g_main, CTL_FPS_UNLOCK);
  CHECK(fps_btn && IsWindowEnabled(fps_btn));
  SetText(g_log, L"");
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_FPS_UNLOCK, BN_CLICKED), 0);
  CHECK(fps_unlock_installs == 1);
  // A successful install reloads the game folder, which reports the dependency
  // state in the activity log.
  CHECK(GetText(g_log).find(L"not installed - press Install FPS Unlock") != std::wstring::npos);
  fps_unlock_state.chain_present = fps_unlock_state.ini_present = fps_unlock_state.proxy_set = true;
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_VERIFY, BN_CLICKED), 0);
  CHECK(!IsWindowEnabled(fps_btn));
  CHECK(GetText(fps_btn).find(L"installed") != std::wstring::npos);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_FPS_UNLOCK, BN_CLICKED), 0);
  CHECK(fps_unlock_installs == 1);
  fps_unlock_state.chain_present = fps_unlock_state.ini_present = fps_unlock_state.proxy_set = false;
  // Hardware auto-fit presets: probe stub reports 32 GB / 12 GB / discrete,
  // so both presets land on the Quality tier mapping.
  SetText(g_log, L"");
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_DETECT_HW, BN_CLICKED), 0);
  CHECK(GetText(g_log).find(L"tier Quality") != std::wstring::npos);
  CHECK(GetText(g_log).find(L"Auto Smooth (") != std::wstring::npos);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_PRESET_SMOOTH, BN_CLICKED), 0);
  CHECK(fixture.frame_limit_fps == 80 && fixture.render_scale_percent == 100 &&
        fixture.aa_mode == 3);
    // Instant-apply: ticking a checkbox writes the toml without Save.
    {
      const int saves_before = saves;
      SendMessageW(g_motion, BM_CLICK, 0, 0);
      CHECK(saves == saves_before + 1);
      SendMessageW(g_motion, BM_CLICK, 0, 0);
      CHECK(saves == saves_before + 2);
    }
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_PRESET_RES, BN_CLICKED), 0);
  CHECK(fixture.frame_limit_fps == 80 && fixture.render_scale_percent == 130);
  probed_hardware = {8, 4, false};
  SetText(g_log, L"");
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_PRESET_SMOOTH, BN_CLICKED), 0);
  CHECK(fixture.frame_limit_fps == 80 && fixture.render_scale_percent == 100);
  CHECK(GetText(g_log).find(L"tier Performance") != std::wstring::npos);
  probed_hardware = {32, 12, true};
  // Navigation opens focused pages and keeps the shared launch/header controls available.
  CHECK(GetDlgItem(g_main, CTL_NAV_HOME) != nullptr);
  CHECK(GetDlgItem(g_main, CTL_NAV_GAME) != nullptr);
  CHECK(GetDlgItem(g_main, CTL_NAV_SETTINGS) != nullptr);
  CHECK(GetDlgItem(g_main, CTL_NAV_ACTIVITY) != nullptr);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_NAV_HOME, BN_CLICKED), 0);
  CHECK(g_page == SetupPage::Home && HasVisibleStyle(g_status));
  CHECK(!HasLayoutOverlap("HOME"));
  CHECK(!HasVisibleStyle(g_game_edit) && !HasVisibleStyle(g_mode));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_NAV_GAME, BN_CLICKED), 0);
  CHECK(g_page == SetupPage::Game && HasVisibleStyle(g_game_edit));
  CHECK(HasVisibleStyle(g_install) && !HasVisibleStyle(g_mode));
  CHECK(g_tpup_button && !IsWindowEnabled(g_tpup_open));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_DOWNLOAD_TPUP, BN_CLICKED), 0);
  CHECK(tpup_downloads == 1 && IsWindowEnabled(g_tpup_open));
  CHECK(g_tpup_folder.find(L"Downloads") != std::wstring::npos);
  CHECK(!HasLayoutOverlap("GAME"));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_NAV_ACTIVITY, BN_CLICKED), 0);
  CHECK(g_page == SetupPage::Help && HasVisibleStyle(g_log));
  CHECK(HasVisibleStyle(GetDlgItem(g_main, CTL_DOCTOR)) && !HasVisibleStyle(g_game_edit));
  CHECK(!HasLayoutOverlap("HELP"));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_NAV_SETTINGS, BN_CLICKED), 0);
  CHECK(g_page == SetupPage::Settings && HasVisibleStyle(g_mode));
      CHECK(HasVisibleStyle(g_advanced_toggle) && !HasVisibleStyle(g_advanced_summary));
      SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_ADVANCED_TOGGLE, BN_CLICKED), 0);
      CHECK(g_advanced_expanded && HasVisibleStyle(g_advanced_summary));
      CHECK(GetText(g_advanced_summary).find(L"Quest 3 controller bindings") != std::wstring::npos);
      SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_ADVANCED_TOGGLE, BN_CLICKED), 0);
      CHECK(!g_advanced_expanded && !HasVisibleStyle(g_advanced_summary));
  CHECK(!HasLayoutOverlap("SETTINGS"));
  SetWindowPos(g_main, nullptr, 40, 40, outer.right - outer.left, 480, SWP_NOZORDER);
  SendMessageW(g_main, WM_VSCROLL, SB_BOTTOM, 0);
  CHECK(g_scroll_y > 0);
  SendMessageW(g_main, WM_VSCROLL, SB_TOP, 0);
  CHECK(g_scroll_y == 0);
  // Reset to safe defaults: overwrites every shown field with the factory
  // posture, then saves. A dirty draft never survives a reset.
  fixture.render_scale_percent = 200;
  fixture.snap_turn = false;
  fixture.autonomous_session_capture = true;
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_RESET_SAFE, BN_CLICKED), 0);
  CHECK(fixture == dsvr::FactorySafeDefaults());
  CHECK(GetText(g_log).find(L"Safe defaults") != std::wstring::npos);
  // A refused reset leaves the file untouched.
  fixture.render_scale_percent = 200;
  message_box_answer = IDNO;
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_RESET_SAFE, BN_CLICKED), 0);
  CHECK(fixture.render_scale_percent == 200);
  message_box_answer = IDYES;
  // Developer mode: Ctrl+D toggles the full control set. The default view
  // is simplified and the window shrinks; developer mode reveals the tuning
  // internals and grows the window back. Sticky and persisted in the file.
  CHECK(!fixture.dev_mode);
  CHECK(!HasVisibleStyle(g_scale) && !HasVisibleStyle(g_arm));
  CHECK(GetDlgItem(g_main, 140) == nullptr);
  // The developer-tuning checkbox lives inside the advanced-settings block:
  // hidden until "Show all saved settings" opens that block, and it mirrors
  // Ctrl+D once open.
  HWND devtoggle = GetDlgItem(g_main, CTL_DEVTOGGLE);
  CHECK(devtoggle != nullptr && !HasVisibleStyle(devtoggle));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_ADVANCED_TOGGLE, BN_CLICKED), 0);
  CHECK(HasVisibleStyle(devtoggle) && !Checked(devtoggle));
  CHECK(HasVisibleStyle(g_advanced_summary) && HasVisibleStyle(g_advanced_open) &&
        HasVisibleStyle(g_advanced_reload));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_ADVANCED_TOGGLE, BN_CLICKED), 0);
  CHECK(!HasVisibleStyle(devtoggle) && !HasVisibleStyle(g_advanced_summary));
  RECT simple_rect{};
  GetWindowRect(g_main, &simple_rect);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_DEVMODE, 1), 0);
  CHECK(fixture.dev_mode);
  // The checkbox label is fixed; its check state mirrors Ctrl+D.
  CHECK(Checked(devtoggle));
  CHECK(HasVisibleStyle(g_scale) && HasVisibleStyle(g_arm));
  CHECK(HasVisibleStyle(g_mode) && HasVisibleStyle(g_debugcap));
  CHECK(HasVisibleStyle(g_snap) && HasVisibleStyle(g_vignette));
  {
    RECT now{};
    GetWindowRect(g_main, &now);
    RECT want{0, 0, S(kClientWidth), S(ContentHeight())};
    AdjustFrame(want);
    CHECK(now.bottom - now.top == want.bottom - want.top);
    // The Settings page keeps one content height in both views: developer rows
    // are hidden in place, so toggling developer mode never resizes the window.
    CHECK(now.bottom - now.top == simple_rect.bottom - simple_rect.top);
  }
  // No two visible controls may overlap in developer view either.
  CHECK(!HasLayoutOverlap("DEV"));
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_DEVMODE, 1), 0);
  CHECK(!fixture.dev_mode);
  CHECK(!Checked(devtoggle));
  CHECK(!HasVisibleStyle(g_scale) && !HasVisibleStyle(g_arm));
  {
    RECT now{};
    GetWindowRect(g_main, &now);
    CHECK(now.bottom - now.top == simple_rect.bottom - simple_rect.top);
  }
  HWND debugcap = GetDlgItem(g_main, CTL_DEBUGCAP);
  CHECK(debugcap != nullptr);
  CHECK(!Checked(debugcap));
  SendMessageW(debugcap, BM_CLICK, 0, 0);
  CHECK(Checked(debugcap));
  CHECK(fixture.autonomous_session_capture);
  // Startup mode is the one VR question: Play, Observer, Combat-trace.
  // Observer modes are flatscreen-only by construction; no VR checkbox
  // remains to interlock against.
  HWND mode = GetDlgItem(g_main, CTL_MODE);
  CHECK(mode != nullptr);
  CHECK((int)SendMessageW(mode, CB_GETCURSEL, 0, 0) == 0);
  SendMessageW(mode, CB_SETCURSEL, 2, 0);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_MODE, CBN_SELCHANGE), (LPARAM)mode);
  CHECK((int)SendMessageW(mode, CB_GETCURSEL, 0, 0) == 2);
  {
      const auto edited = ReadControls(fixture);
      CHECK(edited.observer_only && edited.combat_trace_only && !edited.vr_enabled);
  }
  SendMessageW(mode, CB_SETCURSEL, 1, 0);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_MODE, CBN_SELCHANGE), (LPARAM)mode);
  {
      const auto edited = ReadControls(fixture);
    CHECK(edited.observer_only && !edited.combat_trace_only && !edited.vr_enabled);
    }
  CHECK(fixture.observer_only && !fixture.vr_enabled);
  SendMessageW(mode, CB_SETCURSEL, 0, 0);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_MODE, CBN_SELCHANGE), (LPARAM)mode);
    {
    const auto edited = ReadControls(fixture);
    CHECK(!edited.observer_only && !edited.combat_trace_only && edited.vr_enabled);
  }
  SendMessageW(mode, CB_SETCURSEL, 1, 0);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_MODE, CBN_SELCHANGE), (LPARAM)mode);
  CHECK(fixture.observer_only && !fixture.vr_enabled);
  SendMessageW(mode, CB_SETCURSEL, 0, 0);
    SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_MODE, CBN_SELCHANGE), (LPARAM)mode);
    CHECK(!fixture.observer_only);
  {
    const auto edited = ReadControls(fixture);
    CHECK(edited.autonomous_session_capture);
  }
  CHECK(fixture.autonomous_session_capture);
  SendMessageW(debugcap, BM_CLICK, 0, 0);
  CHECK(!Checked(debugcap));
  CHECK(!fixture.autonomous_session_capture);
  // No frame-cap control is present; rate selection remains automatic.
    CHECK(GetDlgItem(g_main, 140) == nullptr);
    CHECK(fixture.frame_limit_fps == 0);
    // DPI refresh replaces fonts only after controls have released the old ones.
  RECT scaled{40, 40, 1340, 1240};
  SendMessageW(g_main, WM_DPICHANGED, MAKELONG(144, 144), (LPARAM)&scaled);
  CHECK(g_dpi == 144);
  LOGFONTW font{}; CHECK(GetObjectW(g_font, sizeof(font), &font));
  CHECK(font.lfHeight == -20);
  SendMessageW(g_main, WM_DPICHANGED, MAKELONG(96, 96), (LPARAM)&outer);
  SetWindowPos(g_main, nullptr, 40, 40, outer.right - outer.left, 600, SWP_NOZORDER);
  SendMessageW(g_main, WM_VSCROLL, SB_BOTTOM, 0);
  CHECK(g_scroll_y > 0);
  SendMessageW(g_main, WM_VSCROLL, SB_TOP, 0); CHECK(g_scroll_y == 0);
  HDC dc = GetDC(g_motion);
  SendMessageW(g_motion, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
  const auto before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
  for (int i = 0; i < 100; ++i)
    SendMessageW(g_motion, WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT);
  CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before + 1);
  ReleaseDC(g_motion, dc);
  std::puts("native launcher theme/state/DPI/mock launch: PASS");
  if (IsWindow(g_main)) DestroyWindow(g_main);
  CHECK(UnregisterClassW(wc.lpszClassName, wc.hInstance));
  DeleteObject(g_font); DeleteObject(g_title_font);
  DeleteObject(g_backdrop_brush); DeleteObject(g_field_brush);
  return 0;
}
