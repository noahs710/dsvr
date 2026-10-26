// Standalone native launcher regression/preview. Compile with user32/gdi32/
// shell32/comdlg32; setup, config and process creation are all inert test doubles.
// --preview shows the real Win32 controls without reading/writing a game folder.
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
#include "../src/setup/gui.cpp"
#undef CreateProcessW

#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); return 1; } } while (0)
namespace {
dsvr::Config fixture{};
setup::InstallState install_state{true, true};
int saves = 0;
bool verify_fail_next = false;
int installs = 0;
bool chain_conflict_next = false;
bool chain_move_fail_next = false;
const std::wstring fake_exe = L"C:\\DSVR-Theme-Test\\DarkSoulsRemastered.exe";
}
namespace dsvr {
bool ConfigLoad(const wchar_t*, Config& out) { out = fixture; return true; }
bool ConfigSave(const wchar_t*, const Config& value) { fixture = value; ++saves; return true; }
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
std::wstring FindGameExe(const std::wstring& value) { return value == L"invalid" ? L"" : fake_exe; }
bool IsGameRunning() { return false; }
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
}

int main(int argc, char** argv) {
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
  RECT outer{0, 0, kClientWidth, kContentHeight}; AdjustFrame(outer);
  g_main = CreateWindowW(wc.lpszClassName,
      L"DSVR Theme Preview -- mocked game/config (no real launch)",
      WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_CLIPCHILDREN, 40, 40,
      outer.right - outer.left, outer.bottom - outer.top, nullptr, nullptr, wc.hInstance, nullptr);
  CHECK(g_main && g_brief && g_launch && g_toggles.size() == 6);
  CHECK((GetWindowLongPtrW(g_motion, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX);
  CHECK(GetWindowLongPtrW(g_motion, GWL_STYLE) & WS_TABSTOP);
  CHECK((GetWindowLongPtrW(g_launch, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW);
  CHECK(!Checked(g_motion));
  CHECK(GetText(g_brief).find(L"motion attacks off") != std::wstring::npos);
  CHECK(GetText(g_brief).find(L"on -- unqualified") != std::wstring::npos);
  CHECK(GetText(g_brief).find(L"turning off") != std::wstring::npos);
  CHECK(GetText(g_brief).find(L"Build not verified") != std::wstring::npos);
  CHECK(SendMessageW(g_fps, CB_GETCURSEL, 0, 0) == 2);
  // Native BUTTON handles activation and owns its check state. The themed paint
  // must neither implement a parallel bool nor toggle a second time on BN_CLICKED.
  SendMessageW(g_motion, BM_CLICK, 0, 0);
  CHECK(Checked(g_motion) && g_dirty);
  CHECK(GetText(g_brief).find(L"motion attacks on") != std::wstring::npos);
  SendMessageW(g_motion, BM_CLICK, 0, 0);
  CHECK(!Checked(g_motion));
  const auto edited = ReadControls(fixture);
  CHECK(edited.physical_attacks && !edited.motion_attacks && !edited.smooth_turn);
  CHECK(edited.frame_limit_fps == 80);
  CHECK(SaveSettings(true) && saves == 1 && fixture.physical_attacks);
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_VERIFY, BN_CLICKED), 0);
  CHECK(GetText(g_brief).find(L"Build verified") != std::wstring::npos);
  // Exercise the actual WM_COMMAND launch wiring, but CreateProcessW above is a
  // local test double: this test cannot launch a game or deploy anything.
  SendMessageW(g_main, WM_COMMAND, MAKEWPARAM(CTL_LAUNCH, BN_CLICKED), 0);
  CHECK(launches == 1 && launched_exe == fake_exe);
  CHECK(launched_dir == L"C:\\DSVR-Theme-Test");
  SetText(g_game_edit, L"invalid");
  CHECK(!RefreshGame(false) && !IsWindowEnabled(g_launch));
  SetText(g_game_edit, fake_exe); CHECK(RefreshGame(false));
  install_state.has_core = false; UpdateBrief();
  CHECK(!IsWindowEnabled(g_launch));
  CHECK(GetText(g_brief).find(L"Partial install") != std::wstring::npos);
  install_state.has_core = true; UpdateBrief();
  CHECK(IsWindowEnabled(g_launch));
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
  // DPI refresh replaces fonts only after controls have released the old ones.
  RECT scaled{40, 40, 1340, 1240};
  SendMessageW(g_main, WM_DPICHANGED, MAKELONG(144, 144), (LPARAM)&scaled);
  CHECK(g_dpi == 144);
  LOGFONTW font{}; CHECK(GetObjectW(g_font, sizeof(font), &font));
  CHECK(font.lfHeight == -18);
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
  if (argc > 1 && std::string(argv[1]) == "--preview") {
    SetWindowPos(g_main, nullptr, 40, 40, outer.right - outer.left,
        outer.bottom - outer.top, SWP_NOZORDER);
    SetStatus(L"Theme preview: all game/config/process operations are inert test doubles.");
    ShowWindow(g_main, SW_SHOW); UpdateWindow(g_main);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
      if (!IsDialogMessageW(g_main, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  }
  if (IsWindow(g_main)) DestroyWindow(g_main);
  CHECK(UnregisterClassW(wc.lpszClassName, wc.hInstance));
  DeleteObject(g_font); DeleteObject(g_title_font);
  DeleteObject(g_backdrop_brush); DeleteObject(g_field_brush);
  return 0;
}
