#pragma once

#include <filesystem>
#include <string>

namespace mezozoy {

class CrashReporter {
public:
    static void Install();
    static void Log(const std::wstring& context, const std::wstring& details);
    static std::filesystem::path LogPath();
};

}  // namespace mezozoy
