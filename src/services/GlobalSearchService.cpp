#include "GlobalSearchService.h"

#include "../core/Utf.h"

#include <algorithm>

namespace mezozoy {
namespace {

bool Match(const std::wstring& value, const std::wstring& query) { return utf::ToLower(value).find(query) != std::wstring::npos; }

std::wstring Preview(const std::wstring& value, const std::wstring& query) {
    std::wstring compact = value;
    std::replace(compact.begin(), compact.end(), L'\r', L' '); std::replace(compact.begin(), compact.end(), L'\n', L' ');
    const std::wstring lower = utf::ToLower(compact); const std::size_t position = lower.find(query);
    const std::size_t start = position == std::wstring::npos || position < 38 ? 0 : position - 38;
    std::wstring result = compact.substr(start, 120); if (start) result = L"…" + result; if (start + 120 < compact.size()) result += L"…"; return result;
}

}  // namespace

std::vector<SearchResult> GlobalSearchService::search(const Project& project, const std::wstring& rawQuery, std::size_t limit) const {
    const std::wstring query = utf::ToLower(utf::Trim(rawQuery)); std::vector<SearchResult> result; if (query.empty()) return result;
    auto add = [&](SearchResult item) { if (result.size() < limit) result.push_back(std::move(item)); };
    for (const auto& scene : project.scenes) {
        if (Match(scene.title, query) || Match(scene.text, query) || Match(scene.summary, query))
            add({SearchResultKind::Scene, scene.id, scene.id, L"Сцены", scene.title, Preview(scene.text + L" " + scene.summary, query)});
        for (const auto& item : scene.breakdownItems) if (Match(item.name, query) || Match(item.description, query))
            add({SearchResultKind::Breakdown, item.id, scene.id, L"Разбор", item.name, Preview(item.description, query)});
        for (const auto& item : scene.reviewComments) if (Match(item.commentText, query) || Match(item.selectedText, query))
            add({SearchResultKind::Review, item.id, scene.id, L"Рецензирование", scene.title, Preview(item.commentText + L" " + item.selectedText, query)});
        for (const auto& item : scene.notes) if (Match(item.title, query) || Match(item.text, query))
            add({SearchResultKind::Scene, item.id, scene.id, L"Заметки сцен", item.title, Preview(item.text, query)});
    }
    for (const auto& item : project.characters) if (Match(item.name, query) || Match(item.description, query) || Match(item.aliases, query))
        add({SearchResultKind::Character, item.id, 0, L"Персонажи", item.name, Preview(item.description, query)});
    for (const auto& item : project.locations) if (Match(item.name, query) || Match(item.description, query) || Match(item.notes, query))
        add({SearchResultKind::Location, item.id, 0, L"Локации", item.name, Preview(item.description + L" " + item.notes, query)});
    for (const auto& item : project.worlds) if (Match(item.name, query) || Match(item.description, query) || Match(item.notes, query))
        add({SearchResultKind::World, item.id, 0, L"Миры", item.name, Preview(item.description + L" " + item.notes, query)});
    for (const auto& item : project.references) if (Match(item.title, query) || Match(item.description, query) || Match(item.tags, query))
        add({SearchResultKind::Reference, item.id, item.linkedSceneId, L"Референсы", item.title, Preview(item.description + L" " + item.tags, query)});
    return result;
}

}  // namespace mezozoy
