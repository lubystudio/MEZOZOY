#pragma once

#include <filesystem>
#include <string>

namespace mezozoy::utf {

std::string ToUtf8(const std::wstring& value);
std::wstring FromUtf8(const std::string& value);
std::string ReadFile(const std::filesystem::path& path);
bool WriteFileAtomic(const std::filesystem::path& path, const std::string& bytes, std::wstring* error = nullptr);
std::wstring Trim(std::wstring value);
std::wstring ToUpper(std::wstring value);
std::wstring ToLower(std::wstring value);
std::vector<std::wstring> SplitLines(const std::wstring& value);
std::wstring JoinLines(const std::vector<std::wstring>& lines, const std::wstring& separator = L"\r\n");
std::wstring FileNameSafe(std::wstring value);

}  // namespace mezozoy::utf
