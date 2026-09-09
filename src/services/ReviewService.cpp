#include "ReviewService.h"

#include "../core/ModelText.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace mezozoy {
namespace {
std::wstring Now() {
    const auto now = std::chrono::system_clock::now(); const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local{}; localtime_s(&local, &raw); std::wostringstream value; value << std::put_time(&local, L"%Y-%m-%dT%H:%M:%S"); return value.str();
}
}

ReviewComment* ReviewService::add(Project& project, int sceneId, ReviewMarkType type, int startIndex, int length,
                                  const std::wstring& selectedText, const std::wstring& comment, const std::wstring& author) const {
    for (auto& scene : project.scenes) if (scene.id == sceneId) {
        ReviewComment item; item.id = project.nextId(); item.sceneId = sceneId; item.type = type; item.startIndex = std::max(0, startIndex);
        item.length = std::max(0, length); item.selectedText = selectedText; item.commentText = comment; item.authorName = author; item.createdAt = Now();
        scene.reviewComments.push_back(std::move(item)); return &scene.reviewComments.back();
    }
    return nullptr;
}

bool ReviewService::remove(Project& project, int sceneId, int commentId) const {
    for (auto& scene : project.scenes) if (scene.id == sceneId) {
        const auto size = scene.reviewComments.size(); scene.reviewComments.erase(std::remove_if(scene.reviewComments.begin(), scene.reviewComments.end(),
            [&](const ReviewComment& item) { return item.id == commentId; }), scene.reviewComments.end()); return size != scene.reviewComments.size();
    }
    return false;
}

bool ReviewService::setResolved(Project& project, int sceneId, int commentId, bool resolved) const {
    for (auto& scene : project.scenes) if (scene.id == sceneId) for (auto& item : scene.reviewComments) if (item.id == commentId) {
        if (item.resolved == resolved) return false; item.resolved = resolved; return true;
    }
    return false;
}

std::wstring ReviewService::report(const Project& project, bool includeResolved) const {
    std::wostringstream output; output << L"РЕЦЕНЗИРОВАНИЕ: " << project.title << L"\r\n\r\n";
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        bool header = false;
        for (const auto& item : project.scenes[index].reviewComments) {
            if (!includeResolved && item.resolved) continue;
            if (!header) { output << index + 1 << L". " << project.scenes[index].title << L"\r\n"; header = true; }
            output << L"  [" << ReviewMarkTypeName(item.type) << L"] " << item.commentText;
            if (!item.authorName.empty()) output << L" — " << item.authorName;
            if (item.resolved) output << L" (решено)";
            output << L"\r\n";
        }
        if (header) output << L"\r\n";
    }
    return output.str();
}

}  // namespace mezozoy
