// Downloads and installs the pinned DSR-FPS-Unlock release as the loader's
// chain proxy. UI-free like the rest of setup_internal.h: every reason goes
// through the caller's LogFn so the CLI and the GUI report the same thing.
#include "setup_internal.h"

#include <windows.h>
#include <shlobj.h>
#include <winhttp.h>
#include <cstdio>
#include <vector>

#include "../common/config.h"
#include "../common/memory.h"

namespace setup {
namespace {

// The release CDN redirects to these hosts; anything else is refused.
const wchar_t* kAllowedHosts[] = {L"github.com", L"objects.githubusercontent.com",
                                  L"raw.githubusercontent.com",
                                  L"release-assets.githubusercontent.com"};

std::wstring TempDir() {
  wchar_t path[32768]{};
  const DWORD n = GetTempPathW(ARRAYSIZE(path), path);
  return n ? std::wstring(path, n) : L".";
}

bool LowerEq(const std::wstring& a, const wchar_t* b) {
  std::wstring lower;
  for (wchar_t c : a) lower += (wchar_t)towlower(c);
  return lower == b;
}

// Split an https URL into host + path. Rejects anything not plain https with
// no user info, port or query.
bool SplitUrl(const std::wstring& url, std::wstring& host, std::wstring& path) {
  const std::wstring scheme = L"https://";
  if (url.compare(0, scheme.size(), scheme) != 0) return false;
  const auto slash = url.find(L'/', scheme.size());
  if (slash == std::wstring::npos) return false;
  host = url.substr(scheme.size(), slash - scheme.size());
  path = url.substr(slash);
  if (host.empty() || path.empty()) return false;
  if (host.find(L'@') != std::wstring::npos || host.find(L':') != std::wstring::npos)
    return false;
  if (path.find(L'#') != std::wstring::npos) return false;
  if (path.find(L'?') != std::wstring::npos &&
      !LowerEq(host, L"release-assets.githubusercontent.com")) return false;
  return true;
}

bool HostAllowed(const std::wstring& host) {
  for (const wchar_t* allowed : kAllowedHosts)
    if (LowerEq(host, allowed)) return true;
  return false;
}

std::wstring ToHexLower(const std::string& hex) {
  std::wstring out;
  for (char c : hex) out += (wchar_t)towlower((unsigned char)c);
  return out;
}

bool MatchesDigest(const std::wstring& file, const wchar_t* expected,
                   const LogFn& log, const wchar_t* label) {
  std::string hex;
  if (!dsvr::FileSha256(file.c_str(), hex)) {
    log(std::wstring(L"FAIL: could not hash ") + label);
    return false;
  }
  const std::wstring got = ToHexLower(hex);
  if (got != expected) {
    log(std::wstring(L"FAIL: ") + label + L" digest mismatch");
    log(L"  expected " + std::wstring(expected));
    log(L"  actual   " + got);
    return false;
  }
  log(std::wstring(L"OK: ") + label + L" digest verified");
  return true;
}

// One-shot HTTPS GET to a local file, following redirects only to allow-listed
// hosts. Returns false without writing a partial file on any transport failure.
bool Download(const std::wstring& url, const std::wstring& dest, const LogFn& log) {
  std::wstring host, path;
  if (!SplitUrl(url, host, path) || !HostAllowed(host)) {
    log(L"FAIL: refusing a URL outside the pinned GitHub release host");
    return false;
  }
  HINTERNET session = WinHttpOpen(L"DSVR-Setup/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) session = WinHttpOpen(L"DSVR-Setup/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) {
    log(L"FAIL: WinHttpOpen failed");
    return false;
  }
  struct Closer {
    HINTERNET h;
    ~Closer() { if (h) WinHttpCloseHandle(h); }
  } session_closer{session};
  WinHttpSetTimeouts(session, 8000, 8000, 15000, 120000);

  std::wstring current_host = host, current_path = path;
  std::vector<unsigned char> body;
  for (int hop = 0; hop < 6; ++hop) {
    if (!HostAllowed(current_host)) {
      log(L"FAIL: redirect left the allow-listed host");
      return false;
    }
    HINTERNET connect = WinHttpConnect(session, current_host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
      log(L"FAIL: WinHttpConnect failed for " + current_host);
      return false;
    }
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", current_path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) {
      WinHttpCloseHandle(connect);
      log(L"FAIL: WinHttpOpenRequest failed");
      return false;
    }
    const wchar_t* headers = L"Accept: application/octet-stream\r\n";
    BOOL sent = WinHttpSendRequest(request, headers, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (sent) sent = WinHttpReceiveResponse(request, nullptr);
    if (!sent) {
      const DWORD err = GetLastError();
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      log(L"FAIL: request failed (" + std::to_wstring(err) + L") from " + current_host);
      return false;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    std::wstring location;
    if (status >= 300 && status < 400) {
      wchar_t buffer[4096]{};
      DWORD bytes = sizeof(buffer);
      if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                              buffer, &bytes, WINHTTP_NO_HEADER_INDEX))
        location.assign(buffer, bytes / sizeof(wchar_t));
      while (!location.empty() && (location.back() == L'\r' || location.back() == L'\n'))
        location.pop_back();
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      std::wstring next_host, next_path;
      if (!SplitUrl(location, next_host, next_path)) {
        log(L"FAIL: redirect target was not a plain https URL");
        return false;
      }
      current_host = next_host;
      current_path = next_path;
      continue;
    }
    if (status != 200) {
      WinHttpCloseHandle(request);
      WinHttpCloseHandle(connect);
      log(L"FAIL: HTTP " + std::to_wstring(status) + L" from " + current_host);
      return false;
    }
    body.clear();
    for (;;) {
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available)) break;
      if (!available) break;
      std::vector<unsigned char> chunk(available);
      DWORD read = 0;
      if (!WinHttpReadData(request, chunk.data(), available, &read) || !read) break;
      body.insert(body.end(), chunk.begin(), chunk.begin() + read);
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    break;
  }
  if (body.empty()) {
    log(L"FAIL: the download came back empty");
    return false;
  }
  HANDLE file = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    log(L"FAIL: could not open " + dest);
    return false;
  }
  DWORD written = 0;
  const bool ok = WriteFile(file, body.data(), (DWORD)body.size(), &written, nullptr) &&
                  written == body.size();
  CloseHandle(file);
  if (!ok) {
    DeleteFileW(dest.c_str());
    log(L"FAIL: could not write " + dest);
    return false;
  }
  log(L"Downloaded " + std::to_wstring(body.size()) + L" bytes");
  return true;
}

// Extract with the in-box bsdtar (Windows 10 1803+ ships tar.exe with zip
// support). No third-party archive code in the installer.
bool ExtractZip(const std::wstring& zip, const std::wstring& dir, const LogFn& log) {
  wchar_t system_dir[MAX_PATH]{};
  if (!GetSystemDirectoryW(system_dir, ARRAYSIZE(system_dir))) {
    log(L"FAIL: could not locate Windows tools for ZIP extraction");
    return false;
  }
  const std::wstring tar = std::wstring(system_dir) + L"\\tar.exe";
  const std::wstring powershell = std::wstring(system_dir) +
      L"\\WindowsPowerShell\\v1.0\\powershell.exe";
  auto quote_arg = [](const std::wstring& value) { return L"\"" + value + L"\""; };
  std::wstring command;
  LPCWSTR application = nullptr;
  if (GetFileAttributesW(tar.c_str()) != INVALID_FILE_ATTRIBUTES) {
    application = tar.c_str();
    command = quote_arg(tar) + L" -xf " + quote_arg(zip) + L" -C " + quote_arg(dir);
  } else if (GetFileAttributesW(powershell.c_str()) != INVALID_FILE_ATTRIBUTES) {
    application = powershell.c_str();
    auto quote_ps = [](const std::wstring& value) {
      std::wstring escaped = value;
      for (size_t at = 0; (at = escaped.find(L'\'', at)) != std::wstring::npos; at += 2)
        escaped.replace(at, 1, L"''");
      return L"'" + escaped + L"'";
    };
    const std::wstring script = L"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath " +
        quote_ps(zip) + L" -DestinationPath " + quote_ps(dir);
    command = quote_arg(powershell) + L" -NoLogo -NoProfile -NonInteractive -Command " + quote_arg(script);
  } else {
    log(L"FAIL: neither tar.exe nor Windows PowerShell is available to extract ZIP files");
    return false;
  }
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(application, mutable_command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
    log(L"FAIL: could not start the Windows ZIP extractor (" +
        std::to_wstring(GetLastError()) + L")");
    return false;
  }
  const DWORD wait = WaitForSingleObject(pi.hProcess, 120000);
  DWORD exit_code = 1;
  if (wait == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exit_code);
  else if (wait == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  if (wait != WAIT_OBJECT_0 || exit_code != 0) {
    log(wait == WAIT_TIMEOUT ? L"FAIL: ZIP extraction timed out" :
        L"FAIL: Windows ZIP extractor exited with " + std::to_wstring(exit_code));
    return false;
  }
  return true;
}

// bsdtar preserves the archive layout; the release ZIP holds its files at the
// root, but search one level down so a future layout change is found, not lost.
std::wstring FindExtracted(const std::wstring& dir, const wchar_t* name) {
  const std::wstring direct = JoinPath(dir, name);
  if (PathExists(direct)) return direct;
  WIN32_FIND_DATAW data{};
  const std::wstring pattern = JoinPath(dir, L"*");
  HANDLE find = FindFirstFileW(pattern.c_str(), &data);
  if (find == INVALID_HANDLE_VALUE) return L"";
  std::wstring found;
  do {
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (data.cFileName == L"." || data.cFileName == L"..") continue;
    const std::wstring candidate = JoinPath(JoinPath(dir, data.cFileName), name);
    if (PathExists(candidate)) { found = candidate; break; }
  } while (FindNextFileW(find, &data));
  FindClose(find);
  return found;
}

bool CopyInto(const std::wstring& from, const std::wstring& to, const LogFn& log) {
  if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
    log(L"FAIL: could not install " + to + L" (" + std::to_wstring(GetLastError()) + L")");
    return false;
  }
  return true;
}

// Snapshot whatever is about to be replaced, so a bad dependency version can be
// put back by hand. Nothing is deleted here.
void SnapshotExisting(const std::wstring& game_dir, const std::vector<std::wstring>& names,
                      const LogFn& log) {
  wchar_t stamp[32]{};
  SYSTEMTIME st{};
  GetLocalTime(&st);
  swprintf(stamp, ARRAYSIZE(stamp), L"%04d%02d%02dT%02d%02d%02d", st.wYear, st.wMonth,
           st.wDay, st.wHour, st.wMinute, st.wSecond);
  const std::wstring folder =
      TempDir() + L"DSVR-fps-unlock-backup-" + stamp;
  bool any = false;
  for (const auto& name : names)
    if (PathExists(JoinPath(game_dir, name))) any = true;
  if (!any) return;
  if (!CreateDirectoryW(folder.c_str(), nullptr) &&
      GetLastError() != ERROR_ALREADY_EXISTS) {
    log(L"WARN: could not create the backup folder; nothing was snapshotted");
    return;
  }
  for (const auto& name : names) {
    const std::wstring from = JoinPath(game_dir, name);
    if (!PathExists(from)) continue;
    if (CopyFileW(from.c_str(), JoinPath(folder, name).c_str(), FALSE))
      log(L"Backed up " + name + L" to " + folder);
    else
      log(L"WARN: could not back up " + name);
  }
}

}  // namespace

const FpsUnlockPin& FpsUnlockPinInfo() {
  static const FpsUnlockPin pin{
      L"https://github.com/zofrea1/DS1-FPS-Unlock-PTDE-Remastered/releases/"
      L"download/v1.9.0-REMASTERED-DX11/DS1-FPS-Unlock-v1.9.0-REMASTERED-DX11.zip",
      L"37797dd85d49ce440bb693d2cf27ff2993c0d4327f78f10ebe4c7e1751cefbba",
      L"e368f8a5d69b4b9a93a76cb63f92a0f0f4bd8920d81b232b094b204bee10b00c",
      L"dinput8-chain.dll",
      L"DSR-FPS-Unlock.ini"};
  return pin;
}

bool FpsUnlockUrlAllowed(const std::wstring& url) {
  std::wstring host, path;
  if (!SplitUrl(url, host, path) || !HostAllowed(host)) return false;
  return url == FpsUnlockPinInfo().url;
}


namespace {

constexpr wchar_t kTextureToolsUrl[] =
    L"https://github.com/JKAnderson/DSR-TPUP/releases/download/1.5/"
    L"DSR.Texture.Packer.Unpacker.1.5.zip";
constexpr wchar_t kTextureToolsZipSha256[] =
    L"b54f1e2ef932ea31d03b7eb1f7eb9aa17d4cb4adc302ebc6394efaa14b297d58";
constexpr wchar_t kTextureToolsExeSha256[] =
    L"b84d5f3956c47936309f9f00d6b136c2d9d5347292f61059806d1a23bb6d4539";
constexpr wchar_t kTextureToolsTexconvSha256[] =
    L"06532ef243e34a983dcf2447b2c4b28c5a8102a04b709a03da6837a9f7d3deaa";

std::wstring DownloadsPath() {
  PWSTR known = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_CREATE, nullptr, &known)) && known) {
    std::wstring path(known);
    CoTaskMemFree(known);
    return path;
  }
  if (known) CoTaskMemFree(known);
  wchar_t profile[32768]{};
  const DWORD size = GetEnvironmentVariableW(L"USERPROFILE", profile, ARRAYSIZE(profile));
  if (!size || size >= ARRAYSIZE(profile)) return {};
  return JoinPath(profile, L"Downloads");
}

bool VerifiedTextureToolsFolder(const std::wstring& folder, const LogFn& log) {
  const auto exe = JoinPath(folder, L"DSR-TPUP.exe");
  const auto texconv = JoinPath(JoinPath(folder, L"bin"), L"texconv.exe");
  return MatchesDigest(exe, kTextureToolsExeSha256, log, L"TPUP executable") &&
      MatchesDigest(texconv, kTextureToolsTexconvSha256, log, L"TPUP texture converter");
}

}  // namespace

const wchar_t* TextureToolsUrl() { return kTextureToolsUrl; }

bool TextureToolsUrlAllowed(const std::wstring& url) {
  std::wstring host, path;
  return SplitUrl(url, host, path) && HostAllowed(host) && url == kTextureToolsUrl;
}

int DownloadTextureTools(const LogFn& log, const ConfirmFn& confirm,
                         std::wstring& extracted_folder) {
  extracted_folder.clear();
  if (!TextureToolsUrlAllowed(kTextureToolsUrl)) {
    log(L"FAIL: the pinned TPUP URL failed its allow-list check");
    return 2;
  }
  const std::wstring downloads = DownloadsPath();
  if (downloads.empty() || GetFileAttributesW(downloads.c_str()) == INVALID_FILE_ATTRIBUTES) {
    log(L"FAIL: Windows could not locate the user's Downloads folder");
    return 2;
  }
  const std::wstring folder = JoinPath(downloads, L"DSR Texture Packer & Unpacker 1.5");
  const std::wstring exe = JoinPath(folder, L"DSR-TPUP.exe");
  if (PathExists(folder)) {
    if (!PathExists(exe) || !VerifiedTextureToolsFolder(folder, log)) {
      log(L"FAIL: an existing TPUP folder was left untouched because its files do not match the pinned release");
      return 3;
    }
    extracted_folder = folder;
    log(L"TPUP 1.5 is already extracted and verified in Downloads");
    return 0;
  }
  const std::wstring archive = JoinPath(downloads, L"DSR.Texture.Packer.Unpacker.1.5.zip");
  if (!confirm(L"Download optional texture tool",
      L"Download the pinned DSR Texture Packer & Unpacker 1.5 ZIP to your Downloads folder, verify it, and extract it there?\n\n"
      L"This does not run TPUP or modify the game. You choose when to launch it.\n\nContinue?",
      false, false)) {
    log(L"Cancelled: TPUP was not downloaded.");
    return 1;
  }
  bool have_archive = false;
  if (PathExists(archive)) {
    have_archive = MatchesDigest(archive, kTextureToolsZipSha256, log, L"TPUP ZIP");
    if (!have_archive) {
      log(L"FAIL: the existing Downloads ZIP is not the pinned release; it was left untouched");
      return 3;
    }
  } else {
    const std::wstring partial = archive + L".partial-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64());
    log(L"Downloading TPUP 1.5 to Downloads...");
    if (!Download(kTextureToolsUrl, partial, log)) return 4;
    if (!MatchesDigest(partial, kTextureToolsZipSha256, log, L"TPUP ZIP")) {
      DeleteFileW(partial.c_str());
      return 5;
    }
    if (!MoveFileExW(partial.c_str(), archive.c_str(), MOVEFILE_WRITE_THROUGH)) {
      const DWORD error = GetLastError();
      DeleteFileW(partial.c_str());
      log(L"FAIL: could not place the verified ZIP in Downloads (" + std::to_wstring(error) + L")");
      return 6;
    }
    have_archive = true;
  }
  if (!have_archive) return 7;
  log(L"Extracting the verified TPUP archive in Downloads...");
  if (!ExtractZip(archive, downloads, log)) return 8;
  if (!VerifiedTextureToolsFolder(folder, log)) {
    log(L"FAIL: extracted TPUP files did not match the pinned release; nothing was run");
    return 9;
  }
  extracted_folder = folder;
  log(L"PASS: TPUP 1.5 was downloaded, hash-verified, and extracted to " + folder);
  log(L"TPUP was not launched. Extract the texture archive yourself, then open its folder in TPUP.");
  return 0;
}

FpsUnlockState FpsUnlockStateOf(const std::wstring& exe) {
  FpsUnlockState state;
  const std::wstring dir = DirNameOf(exe);
  if (dir.empty()) return state;
  state.chain_present = PathExists(JoinPath(dir, FpsUnlockPinInfo().chain_file));
  state.ini_present = PathExists(JoinPath(dir, FpsUnlockPinInfo().ini_file));
  dsvr::Config cfg;
  const std::wstring toml = JoinPath(dir, L"DSVR.toml");
  if (dsvr::ConfigLoad(toml.c_str(), cfg))
    state.proxy_set = dsvr::WideFromUtf8(cfg.loader_chain_proxy) ==
                      FpsUnlockPinInfo().chain_file;
  return state;
}

int InstallFpsUnlock(const std::wstring& exe, const LogFn& log, const ConfirmFn& confirm) {
  const auto& pin = FpsUnlockPinInfo();
  if (!FpsUnlockUrlAllowed(pin.url)) {
    log(L"FAIL: the pinned download URL failed its own allow-list check");
    return 2;
  }
  if (exe.empty() || !PathExists(exe)) {
    log(L"FAIL: detect the game folder before installing the frame-rate dependency");
    return 2;
  }
  if (IsGameRunning()) {
    log(L"FAIL: the game is running; close it before installing the dependency");
    return 2;
  }
  const std::wstring dir = DirNameOf(exe);
  const auto state = FpsUnlockStateOf(exe);
  if (state.installed()) {
    log(L"INFO: the frame-rate dependency is already installed and chained");
    return 0;
  }
  if (!confirm(L"Install FPS Unlock -- required",
               L"FPS Unlock is a required DSVR dependency. DSVR will not start "
               L"without it.\n\n"
               L"Yes: download the pinned release from GitHub, verify its "
               L"SHA-256, and install it as the chain proxy.\n"
               L"No: cancel. DSVR stays installed, but Start stays blocked.\n\n"
               L"Play offline (Steam Offline + the game's Offline Mode).",
               false, false)) {
    log(L"Cancelled: the frame-rate dependency was not installed.");
    return 1;
  }

  const std::wstring work = TempDir() + L"DSVR-fps-unlock-" + std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(work.c_str(), nullptr);
  const std::wstring zip = work + L"\\release.zip";
  const std::wstring extract = work + L"\\extract";
  CreateDirectoryW(extract.c_str(), nullptr);

  log(L"Downloading the pinned DSR-FPS-Unlock release...");
  if (!Download(pin.url, zip, log)) return 3;
  if (!MatchesDigest(zip, pin.zip_sha256, log, L"release ZIP")) return 4;
  if (!ExtractZip(zip, extract, log)) return 5;

  const std::wstring dll = FindExtracted(extract, L"dinput8.dll");
  if (dll.empty()) {
    log(L"FAIL: the verified ZIP did not contain dinput8.dll");
    return 6;
  }
  if (!MatchesDigest(dll, pin.dll_sha256, log, L"dependency DLL")) return 7;
  const std::wstring ini = FindExtracted(extract, pin.ini_file);
  if (ini.empty()) {
    log(L"FAIL: the verified ZIP did not contain " + std::wstring(pin.ini_file));
    return 8;
  }

  SnapshotExisting(dir, {pin.chain_file, pin.ini_file}, log);
  if (!CopyInto(dll, JoinPath(dir, pin.chain_file), log)) return 9;
  if (!CopyInto(ini, JoinPath(dir, pin.ini_file), log)) return 10;

  // Point the loader at the chain DLL. The root dinput8.dll is DSVR's own
  // proxy and is never rewritten here.
  dsvr::Config cfg;
  const std::wstring toml = JoinPath(dir, L"DSVR.toml");
  if (!dsvr::ConfigLoad(toml.c_str(), cfg)) {
    log(L"WARN: could not read DSVR.toml; set chain_proxy = \"dinput8-chain.dll\" by hand");
    return 11;
  }
  cfg.loader_chain_proxy = "dinput8-chain.dll";
  if (!dsvr::ConfigSave(toml.c_str(), cfg)) {
    log(L"WARN: the dependency is installed but chain_proxy could not be saved");
    return 12;
  }
  const auto after = FpsUnlockStateOf(exe);
  if (!after.installed()) {
    log(L"WARN: the dependency is installed but the chain is not fully in place");
    return 13;
  }
  log(L"PASS: DSR-FPS-Unlock installed as dinput8-chain.dll and chained from DSVR.toml.");
  log(L"DSVR hands it the automatically retrieved display refresh; DSVR applies no frame cap.");
  DeleteFileW(zip.c_str());
  return 0;
}

}  // namespace setup
