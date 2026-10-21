#pragma once

#include <cctype>
#include <string>

namespace dsvr {

// Pure text state machine for the game's motion-blur preference in
// DarkSouls.ini ([DisplaySettingFilter] Blur, integer; 0 = off). Setup owns
// all file I/O; these functions only find, force, and restore the value so
// install/uninstall behaviour is testable without the game.
struct BlurPreference {
  bool found = false;
  std::string value;  // trimmed literal as written, e.g. "1"
};

namespace blur_detail {
inline std::string Trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  const auto e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}
inline std::string Lower(std::string s) {
  for (auto& c : s) c = (char)tolower((unsigned char)c);
  return s;
}
struct Hit {
  bool found = false;
  std::string value;
  size_t value_begin = 0, value_end = 0;  // span in the original text
};
inline Hit Find(const std::string& text) {
  Hit hit{};
  std::string section;
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t eol = text.find('\n', pos);
    const size_t line_end = eol == std::string::npos ? text.size() : eol;
    size_t content_end = line_end;
    if (content_end > pos && text[content_end - 1] == '\r') --content_end;
    const std::string line = text.substr(pos, content_end - pos);
    const std::string t = Trim(line);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
      section = Lower(Trim(t.substr(1, t.size() - 2)));
    } else if (section == "displaysettingfilter") {
      const auto eq = line.find('=');
      if (eq != std::string::npos &&
          Lower(Trim(line.substr(0, eq))) == "blur") {
        size_t vb = pos + eq + 1;
        while (vb < content_end && (text[vb] == ' ' || text[vb] == '\t')) ++vb;
        size_t ve = content_end;
        while (ve > vb && (text[ve - 1] == ' ' || text[ve - 1] == '\t')) --ve;
        hit.found = true;
        hit.value = text.substr(vb, ve - vb);
        hit.value_begin = vb;
        hit.value_end = ve;
        return hit;
      }
    }
    if (eol == std::string::npos) break;
    pos = eol + 1;
  }
  return hit;
}
}  // namespace blur_detail

inline BlurPreference FindBlurPreference(const std::string& ini_text) {
  const auto hit = blur_detail::Find(ini_text);
  return {hit.found, hit.value};
}

enum class BlurForceResult { Forced, AlreadyOff, NotFound };
inline BlurForceResult ForceBlurOff(const std::string& ini_text, std::string& out) {
  const auto hit = blur_detail::Find(ini_text);
  if (!hit.found) { out = ini_text; return BlurForceResult::NotFound; }
  if (hit.value == "0") { out = ini_text; return BlurForceResult::AlreadyOff; }
  out = ini_text.substr(0, hit.value_begin) + "0" +
        ini_text.substr(hit.value_end);
  return BlurForceResult::Forced;
}

enum class BlurRestoreResult { Restored, AlreadyMatches, NotFound };
inline BlurRestoreResult RestoreBlurPreference(const std::string& ini_text,
                                               const std::string& original_value,
                                               std::string& out) {
  const auto hit = blur_detail::Find(ini_text);
  if (!hit.found) { out = ini_text; return BlurRestoreResult::NotFound; }
  if (hit.value == original_value) { out = ini_text; return BlurRestoreResult::AlreadyMatches; }
  out = ini_text.substr(0, hit.value_begin) + original_value +
        ini_text.substr(hit.value_end);
  return BlurRestoreResult::Restored;
}

enum class BlurInstallAction { RecordAndForce, ForceOnly, AlreadyOff, Refuse };
// has_stored_original means an earlier install already recorded the user's
// value; the forced "0" must never overwrite that record.
inline BlurInstallAction DecideBlurInstall(bool found,
                                           const std::string& current_value,
                                           bool has_stored_original) {
  if (!found) return BlurInstallAction::Refuse;
  if (has_stored_original) return BlurInstallAction::ForceOnly;
  if (current_value == "0") return BlurInstallAction::AlreadyOff;
  return BlurInstallAction::RecordAndForce;
}

}  // namespace dsvr
