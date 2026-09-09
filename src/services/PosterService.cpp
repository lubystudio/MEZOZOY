#include "PosterService.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace mezozoy {
namespace {

constexpr std::size_t MaxPosterBytes = 12U * 1024U * 1024U;
constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool HasSupportedSignature(const std::vector<unsigned char>& bytes, std::wstring& format) {
    if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 0x50 && bytes[2] == 0x4E && bytes[3] == 0x47 &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        format = L"png";
        return true;
    }
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        format = L"jpeg";
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') {
        format = L"bmp";
        return true;
    }
    if (bytes.size() >= 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == '8' &&
        (bytes[4] == '7' || bytes[4] == '9') && bytes[5] == 'a') {
        format = L"gif";
        return true;
    }
    return false;
}

}  // namespace

bool PosterService::importFile(const std::filesystem::path& path, Project& project, std::wstring* error) {
    return importEmbeddedFile(path, project.posterImageData, project.posterImageFormat, L"Афиша", error);
}

bool PosterService::importFile(const std::filesystem::path& path, Character& character, std::wstring* error) {
    if (!importEmbeddedFile(path, character.avatarImageData, character.avatarImageFormat, L"Фотография", error)) return false;
    character.avatarPath = path.filename().wstring();
    return true;
}

bool PosterService::importFile(const std::filesystem::path& path, Location& location, std::wstring* error) {
    if (!importEmbeddedFile(path, location.imageData, location.imageFormat, L"Фотография", error)) return false;
    location.imagePath = path.filename().wstring();
    return true;
}

bool PosterService::importFile(const std::filesystem::path& path, ReferenceItem& reference, std::wstring* error) {
    if (!importEmbeddedFile(path, reference.imageData, reference.imageFormat, L"Референс", error)) return false;
    reference.imagePath = path.filename().wstring();
    return true;
}

bool PosterService::importEmbeddedFile(const std::filesystem::path& path,
                                       std::shared_ptr<const std::wstring>& data,
                                       std::wstring& format,
                                       const wchar_t* assetName,
                                       std::wstring* error) {
    try {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) {
            if (error) *error = L"Не удалось открыть изображение.";
            return false;
        }
        stream.seekg(0, std::ios::end);
        const std::streamoff length = stream.tellg();
        if (length <= 0) {
            if (error) *error = L"Файл изображения пуст.";
            return false;
        }
        if (length > static_cast<std::streamoff>(MaxPosterBytes)) {
            if (error) *error = std::wstring(assetName) + L" слишком большая. Максимальный размер файла: 12 МБ.";
            return false;
        }
        stream.seekg(0, std::ios::beg);
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
        if (!stream) {
            if (error) *error = L"Не удалось прочитать изображение полностью.";
            return false;
        }

        std::wstring detectedFormat;
        if (!HasSupportedSignature(bytes, detectedFormat)) {
            if (error) *error = L"Поддерживаются изображения PNG, JPG, BMP и GIF.";
            return false;
        }
        data = std::make_shared<const std::wstring>(encodeBase64(bytes));
        format = std::move(detectedFormat);
        return true;
    } catch (...) {
        if (error) *error = L"Не удалось импортировать изображение.";
        return false;
    }
}

void PosterService::clear(Project& project) {
    project.posterImageData.reset();
    project.posterImageFormat.clear();
}

void PosterService::clear(Character& character) {
    character.avatarImageData.reset();
    character.avatarImageFormat.clear();
    character.avatarPath.clear();
}

void PosterService::clear(Location& location) {
    location.imageData.reset();
    location.imageFormat.clear();
    location.imagePath.clear();
}

void PosterService::clear(ReferenceItem& reference) {
    reference.imageData.reset();
    reference.imageFormat.clear();
    reference.imagePath.clear();
}

std::vector<unsigned char> PosterService::decode(const Project& project) {
    return project.posterImageData ? decodeBase64(*project.posterImageData) : std::vector<unsigned char>{};
}

std::vector<unsigned char> PosterService::decode(const Character& character) {
    return character.avatarImageData ? decodeBase64(*character.avatarImageData) : std::vector<unsigned char>{};
}

std::vector<unsigned char> PosterService::decode(const Location& location) {
    return location.imageData ? decodeBase64(*location.imageData) : std::vector<unsigned char>{};
}

std::vector<unsigned char> PosterService::decode(const ReferenceItem& reference) {
    return reference.imageData ? decodeBase64(*reference.imageData) : std::vector<unsigned char>{};
}

std::wstring PosterService::encodeBase64(const std::vector<unsigned char>& bytes) {
    std::wstring result;
    result.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const unsigned value = static_cast<unsigned>(bytes[index]) << 16 |
                               (index + 1 < bytes.size() ? static_cast<unsigned>(bytes[index + 1]) << 8 : 0U) |
                               (index + 2 < bytes.size() ? static_cast<unsigned>(bytes[index + 2]) : 0U);
        result.push_back(static_cast<wchar_t>(Alphabet[(value >> 18) & 63]));
        result.push_back(static_cast<wchar_t>(Alphabet[(value >> 12) & 63]));
        result.push_back(index + 1 < bytes.size() ? static_cast<wchar_t>(Alphabet[(value >> 6) & 63]) : L'=');
        result.push_back(index + 2 < bytes.size() ? static_cast<wchar_t>(Alphabet[value & 63]) : L'=');
    }
    return result;
}

std::vector<unsigned char> PosterService::decodeBase64(const std::wstring& text) {
    static int table[256]{};
    static bool initialized = false;
    if (!initialized) {
        std::fill(std::begin(table), std::end(table), -1);
        for (int index = 0; index < 64; ++index) table[static_cast<unsigned char>(Alphabet[index])] = index;
        initialized = true;
    }
    std::vector<unsigned char> result;
    result.reserve((text.size() / 4) * 3);
    unsigned value = 0;
    int bits = -8;
    for (wchar_t character : text) {
        if (character == L'=') break;
        if (character < 0 || character > 255) continue;
        const int decoded = table[static_cast<unsigned char>(character)];
        if (decoded < 0) continue;
        value = (value << 6) | static_cast<unsigned>(decoded);
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<unsigned char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

}  // namespace mezozoy
