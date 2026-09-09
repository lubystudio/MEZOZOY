#include "BreakdownService.h"

#include "../core/ModelText.h"
#include "../core/Utf.h"

#include <algorithm>
#include <sstream>

namespace mezozoy {

BreakdownItem* BreakdownService::add(Project& project, int sceneId, BreakdownCategory category, const std::wstring& name,
                                     const std::wstring& selectedText) const {
    for (auto& scene : project.scenes) if (scene.id == sceneId) {
        BreakdownItem item; item.id = project.nextId(); item.sceneId = sceneId; item.category = category;
        item.name = utf::Trim(name).empty() ? L"Новый элемент" : utf::Trim(name); item.description = selectedText;
        scene.breakdownItems.push_back(std::move(item)); return &scene.breakdownItems.back();
    }
    return nullptr;
}

bool BreakdownService::remove(Project& project, int sceneId, int itemId) const {
    for (auto& scene : project.scenes) if (scene.id == sceneId) {
        const auto oldSize = scene.breakdownItems.size();
        scene.breakdownItems.erase(std::remove_if(scene.breakdownItems.begin(), scene.breakdownItems.end(),
            [&](const BreakdownItem& item) { return item.id == itemId; }), scene.breakdownItems.end());
        return scene.breakdownItems.size() != oldSize;
    }
    return false;
}

std::wstring BreakdownService::report(const Project& project) const {
    std::wostringstream output; output << L"РАЗБОР ПРОЕКТА: " << project.title << L"\r\n\r\n";
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        const auto& scene = project.scenes[index];
        if (scene.breakdownItems.empty()) continue;
        output << index + 1 << L". " << scene.title << L"\r\n";
        for (const auto& item : scene.breakdownItems)
            output << L"  [" << BreakdownCategoryName(item.category) << L"] " << item.name << L" — " << item.status
                   << (item.assignedTo.empty() ? L"" : L" · " + item.assignedTo) << L"\r\n";
        output << L"\r\n";
    }
    return output.str();
}

}  // namespace mezozoy
