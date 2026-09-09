#pragma once

#include "../core/Models.h"

#include <optional>
#include <vector>

namespace mezozoy {

enum class ProjectClipboardKind {
    None,
    Scenes,
    BoardItems,
    Character,
    Location,
    World,
    Reference,
    Relation,
    Folder,
    Document
};

struct ProjectClipboard {
    ProjectClipboardKind kind = ProjectClipboardKind::None;
    std::vector<Scene> scenes;
    std::vector<BoardSection> sections;
    std::optional<Character> character;
    std::optional<Location> location;
    std::optional<WorldItem> world;
    std::optional<ReferenceItem> reference;
    std::optional<RelationshipLink> relation;
    std::optional<DevFolder> folder;
    std::vector<Character> folderCharacters;
    std::vector<Location> folderLocations;
    std::optional<ProjectDocumentEntry> document;

    bool empty() const { return kind == ProjectClipboardKind::None; }

    void clear() {
        *this = ProjectClipboard{};
    }
};

inline Scene CloneSceneForProject(Project& project, const Scene& source, float offset = 36.0f) {
    Scene copy = source;
    int nextId = project.nextId();
    copy.id = nextId++;
    copy.title += L" — копия";
    copy.cardX += offset;
    copy.cardY += offset;
    copy.selected = false;
    for (auto& note : copy.notes) note.id = nextId++;
    for (auto& item : copy.breakdownItems) { item.id = nextId++; item.sceneId = copy.id; }
    for (auto& review : copy.reviewComments) { review.id = nextId++; review.sceneId = copy.id; }
    return copy;
}

}  // namespace mezozoy
