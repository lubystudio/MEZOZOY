#pragma once

#include <optional>
#include <string>

namespace mezozoy {

struct UpdateInfo {
    std::wstring version;
    std::wstring releaseUrl;
};

class UpdateService {
public:
    static std::optional<UpdateInfo> check(const std::wstring& repository,
                                           const std::wstring& currentVersion);
    static std::optional<UpdateInfo> parseRelease(const std::string& json,
                                                  const std::wstring& repository,
                                                  const std::wstring& currentVersion);
    static bool isNewerVersion(const std::wstring& candidate, const std::wstring& current);
};

}  // namespace mezozoy
