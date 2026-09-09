#pragma once

#include "../core/Models.h"

#include <filesystem>
#include <string>
#include <vector>

namespace mezozoy {

class PosterService {
public:
    static bool importFile(const std::filesystem::path& path, Project& project, std::wstring* error = nullptr);
    static bool importFile(const std::filesystem::path& path, Character& character, std::wstring* error = nullptr);
    static bool importFile(const std::filesystem::path& path, Location& location, std::wstring* error = nullptr);
    static bool importFile(const std::filesystem::path& path, ReferenceItem& reference, std::wstring* error = nullptr);
    static void clear(Project& project);
    static void clear(Character& character);
    static void clear(Location& location);
    static void clear(ReferenceItem& reference);
    static std::vector<unsigned char> decode(const Project& project);
    static std::vector<unsigned char> decode(const Character& character);
    static std::vector<unsigned char> decode(const Location& location);
    static std::vector<unsigned char> decode(const ReferenceItem& reference);

private:
    static std::wstring encodeBase64(const std::vector<unsigned char>& bytes);
    static std::vector<unsigned char> decodeBase64(const std::wstring& text);
    static bool importEmbeddedFile(const std::filesystem::path& path,
                                   std::shared_ptr<const std::wstring>& data,
                                   std::wstring& format,
                                   const wchar_t* assetName,
                                   std::wstring* error);
};

}  // namespace mezozoy
