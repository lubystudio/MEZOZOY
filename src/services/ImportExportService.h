#pragma once

#include "../core/Models.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mezozoy {

enum class ScriptExchangeFormat { Text, Fountain };

struct ImportPreview {
    std::filesystem::path source;
    ScriptExchangeFormat format = ScriptExchangeFormat::Text;
    std::vector<Scene> scenes;
    std::vector<std::wstring> detectedCharacters;
    std::vector<std::wstring> detectedLocations;
    std::wstring error;
};

class ImportExportService {
public:
    ImportPreview preview(const std::filesystem::path& path, ScriptExchangeFormat format) const;
    std::size_t apply(Project& project, ImportPreview preview, bool addDetectedBibleItems = true) const;
    bool exportText(const std::filesystem::path& path, const Project& project, bool fountain, std::wstring* error = nullptr) const;

private:
    static bool isHeading(const std::wstring& line, bool fountain);
    static std::wstring normalizeTitle(std::wstring line, bool fountain);
};

}  // namespace mezozoy
