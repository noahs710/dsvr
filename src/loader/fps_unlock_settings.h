#pragma once

#include <string>
#include <string_view>

namespace dsvr::fps_unlock {

inline int ChooseMaxFps(bool vr_enabled, bool observer_only, int headset_hz, int desktop_hz) {
  const auto valid = [](int fps) { return fps >= 10 && fps <= 1000; };
  if (vr_enabled && !observer_only) return valid(headset_hz) ? headset_hz : 0;
  return valid(desktop_hz) ? desktop_hz : 0;
}

inline std::string TrimIniKey(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() && (text[first] == ' ' || text[first] == '\t' || text[first] == '\r'))
    ++first;
  std::size_t last = text.size();
  while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t' || text[last - 1] == '\r'))
    --last;
  std::string key(text.substr(first, last - first));
  for (char& c : key)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return key;
}

inline bool SetFpsUnlockSettings(std::string& ini, int max_fps) {
  const bool enabled = max_fps >= 10 && max_fps <= 1000;
  if (max_fps != 0 && !enabled) return false;
  const std::string eol = ini.find("\r\n") == std::string::npos ? "\n" : "\r\n";
  bool in_framerate = false;
  bool found_framerate = false;
  bool found_unlock = false;
  bool found_max = false;
  bool found_variable = false;
  std::string output;
  const auto append_missing = [&] {
    if (!found_unlock) output += std::string("FPSUnlock = ") + (enabled ? "true" : "false") + eol;
    if (enabled && !found_max) output += "MaxFPS = " + std::to_string(max_fps) + eol;
    if (enabled && !found_variable) output += "VariableFrameTime = true" + eol;
  };
  std::size_t pos = 0;
  while (pos < ini.size()) {
    const std::size_t end = ini.find('\n', pos);
    const bool has_eol = end != std::string::npos;
    const std::size_t line_end = has_eol ? end : ini.size();
    std::string line = ini.substr(pos, line_end - pos);
    const bool had_cr = !line.empty() && line.back() == '\r';
    if (had_cr) line.pop_back();
    const std::string line_eol = has_eol ? (had_cr ? "\r\n" : "\n") : "";
    const std::string trimmed = TrimIniKey(line);
    if (trimmed.size() >= 2 && trimmed.front() == '[' && trimmed.back() == ']') {
      if (in_framerate) append_missing();
      in_framerate = TrimIniKey(std::string_view(trimmed).substr(1, trimmed.size() - 2)) == "framerate";
      found_framerate = found_framerate || in_framerate;
    } else if (in_framerate) {
      const std::size_t equal = line.find('=');
      if (equal != std::string::npos) {
        const std::string key = TrimIniKey(std::string_view(line).substr(0, equal));
        std::string value;
        if (key == "fpsunlock") { value = enabled ? "true" : "false"; found_unlock = true; }
        else if (key == "maxfps" || key == "targetfps") {
          found_max = true;
          if (enabled) value = std::to_string(max_fps);
        } else if (key == "variableframetime") {
          found_variable = true;
          if (enabled) value = "true";
        }
        if (!value.empty()) {
          const std::size_t comment = line.find('#', equal + 1);
          line = line.substr(0, equal + 1) + " " + value +
              (comment == std::string::npos ? "" : " " + line.substr(comment));
        }
      }
    }
    output += line + line_eol;
    pos = has_eol ? end + 1 : ini.size();
  }
  if (in_framerate) {
    append_missing();
  } else if (!found_framerate) {
    if (!output.empty() && output.back() != '\n') output += eol;
    if (!output.empty()) output += eol;
    output += "[Framerate]" + eol;
    output += std::string("FPSUnlock = ") + (enabled ? "true" : "false") + eol;
    if (enabled) {
      output += "MaxFPS = " + std::to_string(max_fps) + eol;
      output += "VariableFrameTime = true" + eol;
    }
  }
  ini = std::move(output);
  return true;
}

}  // namespace dsvr::fps_unlock
