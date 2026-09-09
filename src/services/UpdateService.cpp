#include "UpdateService.h"

#include "../core/Utf.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <memory>
#include <vector>

namespace mezozoy {
namespace {

struct InternetHandleCloser {
    void operator()(void* handle) const {
        if (handle) WinHttpCloseHandle(static_cast<HINTERNET>(handle));
    }
};

using InternetHandle = std::unique_ptr<void, InternetHandleCloser>;

std::optional<std::string> JsonString(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t position = json.find(needle);
    if (position == std::string::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string::npos) return std::nullopt;
    position = json.find('"', position + 1);
    if (position == std::string::npos) return std::nullopt;
    ++position;

    std::string result;
    while (position < json.size()) {
        const char value = json[position++];
        if (value == '"') return result;
        if (value != '\\') {
            result.push_back(value);
            continue;
        }
        if (position >= json.size()) return std::nullopt;
        const char escaped = json[position++];
        if (escaped == '"' || escaped == '\\' || escaped == '/') result.push_back(escaped);
        else if (escaped == 'b') result.push_back('\b');
        else if (escaped == 'f') result.push_back('\f');
        else if (escaped == 'n') result.push_back('\n');
        else if (escaped == 'r') result.push_back('\r');
        else if (escaped == 't') result.push_back('\t');
        else return std::nullopt;
    }
    return std::nullopt;
}

std::vector<unsigned long long> VersionParts(std::wstring value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](wchar_t c) {
        return c == L'v' || c == L'V' || std::iswspace(c) != 0;
    }), value.end());

    std::vector<unsigned long long> result;
    std::size_t position = 0;
    while (position < value.size()) {
        if (!std::iswdigit(value[position])) break;
        unsigned long long part = 0;
        while (position < value.size() && std::iswdigit(value[position])) {
            part = std::min<unsigned long long>(part * 10 + static_cast<unsigned>(value[position] - L'0'), 1000000);
            ++position;
        }
        result.push_back(part);
        if (position == value.size()) break;
        if (value[position] != L'.') break;
        ++position;
    }
    return result;
}

bool SafeRepository(const std::wstring& repository) {
    if (repository.empty() || repository.size() > 160 || repository.front() == L'/' || repository.back() == L'/') return false;
    if (std::count(repository.begin(), repository.end(), L'/') != 1) return false;
    return std::all_of(repository.begin(), repository.end(), [](wchar_t c) {
        return std::iswalnum(c) != 0 || c == L'-' || c == L'_' || c == L'.' || c == L'/';
    });
}

std::wstring CaseFold(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

}  // namespace

bool UpdateService::isNewerVersion(const std::wstring& candidate, const std::wstring& current) {
    auto left = VersionParts(candidate);
    auto right = VersionParts(current);
    if (left.empty() || right.empty()) return false;
    const std::size_t count = std::max(left.size(), right.size());
    left.resize(count);
    right.resize(count);
    return std::lexicographical_compare(right.begin(), right.end(), left.begin(), left.end());
}

std::optional<UpdateInfo> UpdateService::parseRelease(const std::string& json,
                                                      const std::wstring& repository,
                                                      const std::wstring& currentVersion) {
    if (!SafeRepository(repository)) return std::nullopt;
    const auto tag = JsonString(json, "tag_name");
    const auto url = JsonString(json, "html_url");
    if (!tag || !url) return std::nullopt;

    const std::wstring version = utf::FromUtf8(*tag);
    const std::wstring releaseUrl = utf::FromUtf8(*url);
    const std::wstring expectedPrefix = L"https://github.com/" + repository + L"/releases/";
    if (CaseFold(releaseUrl).rfind(CaseFold(expectedPrefix), 0) != 0 ||
        !isNewerVersion(version, currentVersion)) return std::nullopt;
    return UpdateInfo{version, releaseUrl};
}

std::optional<UpdateInfo> UpdateService::check(const std::wstring& repository,
                                               const std::wstring& currentVersion) {
    if (!SafeRepository(repository)) return std::nullopt;

    wchar_t testResponsePath[32768]{};
    const DWORD testPathSize = GetEnvironmentVariableW(L"MEZOZOY_UPDATE_TEST_RESPONSE", testResponsePath,
                                                        static_cast<DWORD>(std::size(testResponsePath)));
    if (testPathSize > 0 && testPathSize < std::size(testResponsePath)) {
        try {
            return parseRelease(utf::ReadFile(testResponsePath), repository, currentVersion);
        } catch (...) {
            return std::nullopt;
        }
    }

    InternetHandle session(WinHttpOpen(L"Mezozoy Update Checker/1.0",
                                       WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return std::nullopt;
    WinHttpSetTimeouts(session.get(), 2500, 2500, 2500, 2500);

    InternetHandle connection(WinHttpConnect(session.get(), L"api.github.com",
                                              INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) return std::nullopt;

    const std::wstring path = L"/repos/" + repository + L"/releases/latest";
    InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                              WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                              WINHTTP_FLAG_SECURE));
    if (!request) return std::nullopt;

    const wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpAddRequestHeaders(request.get(), headers, static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) return std::nullopt;

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) || status != 200) return std::nullopt;

    std::string body;
    while (body.size() < 1024 * 1024) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) return std::nullopt;
        if (!available) break;
        const DWORD chunkSize = std::min<DWORD>(available, 64 * 1024);
        std::string chunk(chunkSize, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), chunk.data(), chunkSize, &read)) return std::nullopt;
        body.append(chunk.data(), read);
    }
    return parseRelease(body, repository, currentVersion);
}

}  // namespace mezozoy
