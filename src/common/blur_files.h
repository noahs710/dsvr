#pragma once
#include <string>
#include "config.h"
namespace dsvr {
// Commit the original preference to the mod config BEFORE changing the native
// INI. All writes replace a completed sibling file; unrelated INI bytes survive.
bool InstallBlurPreference(const std::wstring& ini,const std::wstring& config,
                           Config& settings,std::string& status);
bool UninstallBlurPreference(const std::wstring& ini,const std::wstring& config,
                             std::string& status);
}
