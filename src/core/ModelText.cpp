#include "ModelText.h"

#include <algorithm>
#include <cwctype>

namespace mezozoy {
namespace {

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

template <typename T>
struct Entry {
    T value;
    const wchar_t* key;
    const wchar_t* name;
};

constexpr Entry<ProjectDocumentType> DocumentEntries[] = {
    {ProjectDocumentType::Folder, L"Folder", L"Папка"},
    {ProjectDocumentType::Screenplay, L"Screenplay", L"Сценарий"},
    {ProjectDocumentType::Episode, L"Episode", L"Эпизод"},
    {ProjectDocumentType::Season, L"Season", L"Сезон"},
    {ProjectDocumentType::TitlePage, L"TitlePage", L"Титульная страница"},
    {ProjectDocumentType::Synopsis, L"Synopsis", L"Синопсис"},
    {ProjectDocumentType::Treatment, L"Treatment", L"Тритмент"},
    {ProjectDocumentType::Outline, L"Outline", L"План"},
    {ProjectDocumentType::BeatSheet, L"BeatSheet", L"Бит-лист"},
    {ProjectDocumentType::Novel, L"Novel", L"Роман"},
    {ProjectDocumentType::ComicBook, L"ComicBook", L"Комикс"},
    {ProjectDocumentType::StagePlay, L"StagePlay", L"Пьеса"},
    {ProjectDocumentType::AudioDrama, L"AudioDrama", L"Аудиодрама"},
    {ProjectDocumentType::Notes, L"Notes", L"Заметки"},
    {ProjectDocumentType::MindMap, L"MindMap", L"Карта идей"},
    {ProjectDocumentType::ReferenceGallery, L"ReferenceGallery", L"Галерея референсов"},
};

constexpr Entry<BreakdownCategory> BreakdownEntries[] = {
    {BreakdownCategory::Character, L"Character", L"Персонаж"}, {BreakdownCategory::Extra, L"Extra", L"Массовка"},
    {BreakdownCategory::Location, L"Location", L"Локация"}, {BreakdownCategory::Prop, L"Prop", L"Реквизит"},
    {BreakdownCategory::Costume, L"Costume", L"Костюм"}, {BreakdownCategory::Makeup, L"Makeup", L"Грим"},
    {BreakdownCategory::Vfx, L"VFX", L"VFX"}, {BreakdownCategory::Sfx, L"SFX", L"SFX"},
    {BreakdownCategory::Vehicle, L"Vehicle", L"Транспорт"}, {BreakdownCategory::Animal, L"Animal", L"Животное"},
    {BreakdownCategory::Weapon, L"Weapon", L"Оружие"}, {BreakdownCategory::SetDressing, L"SetDressing", L"Оформление площадки"},
    {BreakdownCategory::Sound, L"Sound", L"Звук"}, {BreakdownCategory::Music, L"Music", L"Музыка"},
    {BreakdownCategory::Stunt, L"Stunt", L"Трюк"}, {BreakdownCategory::Note, L"Note", L"Заметка"},
    {BreakdownCategory::Custom, L"Custom", L"Другое"},
};

constexpr Entry<ReviewMarkType> ReviewEntries[] = {
    {ReviewMarkType::HighlightText, L"HighlightText", L"Цвет текста"},
    {ReviewMarkType::HighlightBackground, L"HighlightBackground", L"Подсветка"},
    {ReviewMarkType::Comment, L"Comment", L"Комментарий"},
    {ReviewMarkType::Suggestion, L"Suggestion", L"Предложение"},
    {ReviewMarkType::Deletion, L"Deletion", L"Удаление"},
    {ReviewMarkType::Insertion, L"Insertion", L"Вставка"},
};

template <typename T, std::size_t N>
std::wstring NameOf(T value, const Entry<T> (&entries)[N]) {
    for (const auto& entry : entries) if (entry.value == value) return entry.name;
    return entries[0].name;
}

template <typename T, std::size_t N>
std::wstring KeyOf(T value, const Entry<T> (&entries)[N]) {
    for (const auto& entry : entries) if (entry.value == value) return entry.key;
    return entries[0].key;
}

template <typename T, std::size_t N>
T Parse(const std::wstring& value, const Entry<T> (&entries)[N]) {
    const std::wstring lower = Lower(value);
    for (const auto& entry : entries) if (lower == Lower(entry.key) || lower == Lower(entry.name)) return entry.value;
    return entries[0].value;
}

}  // namespace

std::wstring ProjectDocumentTypeName(ProjectDocumentType type) { return NameOf(type, DocumentEntries); }
std::wstring ProjectDocumentTypeKey(ProjectDocumentType type) { return KeyOf(type, DocumentEntries); }
ProjectDocumentType ParseProjectDocumentType(const std::wstring& value) { return Parse(value, DocumentEntries); }
const std::vector<ProjectDocumentType>& AllProjectDocumentTypes() {
    static const std::vector<ProjectDocumentType> values = [] { std::vector<ProjectDocumentType> result; for (const auto& entry : DocumentEntries) result.push_back(entry.value); return result; }();
    return values;
}

std::wstring BreakdownCategoryName(BreakdownCategory category) { return NameOf(category, BreakdownEntries); }
std::wstring BreakdownCategoryKey(BreakdownCategory category) { return KeyOf(category, BreakdownEntries); }
BreakdownCategory ParseBreakdownCategory(const std::wstring& value) { return Parse(value, BreakdownEntries); }
const std::vector<BreakdownCategory>& AllBreakdownCategories() {
    static const std::vector<BreakdownCategory> values = [] { std::vector<BreakdownCategory> result; for (const auto& entry : BreakdownEntries) result.push_back(entry.value); return result; }();
    return values;
}

std::wstring ReviewMarkTypeName(ReviewMarkType type) { return NameOf(type, ReviewEntries); }
std::wstring ReviewMarkTypeKey(ReviewMarkType type) { return KeyOf(type, ReviewEntries); }
ReviewMarkType ParseReviewMarkType(const std::wstring& value) { return Parse(value, ReviewEntries); }
const std::vector<ReviewMarkType>& AllReviewMarkTypes() {
    static const std::vector<ReviewMarkType> values = [] { std::vector<ReviewMarkType> result; for (const auto& entry : ReviewEntries) result.push_back(entry.value); return result; }();
    return values;
}

}  // namespace mezozoy
