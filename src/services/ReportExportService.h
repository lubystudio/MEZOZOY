#pragma once

#include "../core/Models.h"

#include <filesystem>
#include <string>

namespace mezozoy {

class ReportExportService {
public:
    bool exportSceneCsv(const std::filesystem::path& path, const Project& project, std::wstring* error = nullptr) const;
    bool exportProjectHtml(const std::filesystem::path& path, const Project& project, std::wstring* error = nullptr) const;
};

}  // namespace mezozoy
