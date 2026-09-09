#include "Utf.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace mezozoy::utf {

std::string ToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring FromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        const int fallbackSize = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring fallback(static_cast<std::size_t>(std::max(0, fallbackSize)), L'\0');
        if (fallbackSize > 0) MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), fallback.data(), fallbackSize);
        return fallback;
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot open file");
    std::ostringstream output;
    output << stream.rdbuf();
    std::string bytes = output.str();
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    return bytes;
}

bool WriteFileAtomic(const std::filesystem::path& path, const std::string& bytes, std::wstring* error) {
    try {
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        const std::filesystem::path temporary = path.wstring() + L".tmp";
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream) throw std::runtime_error("cannot create temporary file");
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            stream.flush();
            if (!stream.good()) throw std::runtime_error("cannot write temporary file");
        }
        if (std::filesystem::exists(path)) {
            const std::filesystem::path backup = path.wstring() + L".bak";
            std::error_code ignored;
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ignored);
        }
        // Replace atomically without deleting the destination first. If the
        // operation fails, the original project and its backup stay intact.
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "cannot replace project file");
        }
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = FromUtf8(ex.what());
        return false;
    }
}

std::wstring Trim(std::wstring value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t c) { return std::iswspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t c) { return std::iswspace(c) != 0; }).base();
    if (first >= last) return {};
    return std::wstring(first, last);
}

std::wstring ToUpper(std::wstring value) {
    if (value.empty()) return value;
    std::wstring mapped(value.size(), L'\0');
    const int count = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE, value.data(), static_cast<int>(value.size()),
                                    mapped.data(), static_cast<int>(mapped.size()), nullptr, nullptr, 0);
    if (count > 0) return mapped;
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return value;
}

std::wstring ToLower(std::wstring value) {
    if (value.empty()) return value;
    std::wstring mapped(value.size(), L'\0');
    const int count = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_LOWERCASE, value.data(), static_cast<int>(value.size()),
                                    mapped.data(), static_cast<int>(mapped.size()), nullptr, nullptr, 0);
    if (count > 0) return mapped;
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

std::vector<std::wstring> SplitLines(const std::wstring& value) {
    std::vector<std::wstring> lines;
    std::wstring current;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const wchar_t c = value[i];
        if (c == L'\r' || c == L'\n') {
            if (c == L'\r' && i + 1 < value.size() && value[i + 1] == L'\n') ++i;
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    lines.push_back(current);
    return lines;
}

std::wstring JoinLines(const std::vector<std::wstring>& lines, const std::wstring& separator) {
    std::wstring result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result += separator;
        result += lines[i];
    }
    return result;
}

std::wstring FileNameSafe(std::wstring value) {
    static const std::wstring invalid = L"<>:\"/\\|?*";
    for (auto& c : value) if (invalid.find(c) != std::wstring::npos) c = L'_';
    value = Trim(value);
    return value.empty() ? L"Проект" : value;
}

}  // namespace mezozoy::utf
