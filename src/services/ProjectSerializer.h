#pragma once

#include "../core/Models.h"

#include <filesystem>
#include <string>

namespace mezozoy {

class ProjectSerializer {
public:
    bool load(const std::filesystem::path& path, Project& project, std::wstring* error = nullptr) const;
    bool save(const std::filesystem::path& path, const Project& project, std::wstring* error = nullptr) const;
    std::string toXml(const Project& project) const;

private:
    static int integer(const std::string& value, int fallback = 0);
    static float number(const std::string& value, float fallback = 0.0f);
    static bool boolean(const std::string& value, bool fallback = false);
};

}  // namespace mezozoy
