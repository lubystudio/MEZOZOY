#pragma once

#include "../core/Models.h"
#include "ProjectSerializer.h"
#include "ScriptService.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace mezozoy {

enum class DocumentChange {
    Replaced,
    Content,
    Selection,
    Saved
};

class ProjectDocument {
public:
    using Listener = std::function<void(DocumentChange)>;

    const Project& project() const { return project_; }
    // Compatibility escape hatch for older pages. New feature code should use
    // the ID-based accessors below so every view observes one document change.
    Project& editProject();
    const std::filesystem::path& filePath() const { return filePath_; }
    bool dirty() const { return dirty_; }
    std::size_t selectedScene() const;
    int selectedSceneId() const;
    std::uint64_t revision() const { return revision_; }

    void createNew();
    bool open(const std::filesystem::path& path, std::wstring* error = nullptr);
    bool openRecovered(const std::filesystem::path& originalPath, const std::filesystem::path& recoveryPath,
                       std::wstring* error = nullptr);
    bool save(std::wstring* error = nullptr);
    bool saveAs(const std::filesystem::path& path, std::wstring* error = nullptr);
    bool autosave(std::wstring* error = nullptr);
    void setBackupPolicy(bool enabled, std::size_t maxBackups);
    void markChanged();
    void setSelectedScene(std::size_t index, bool selectMatchingCard = true);
    bool setSelectedSceneById(int sceneId, bool selectMatchingCard = true);
    std::size_t sceneIndexById(int sceneId) const;
    Scene* sceneById(int sceneId);
    const Scene* sceneById(int sceneId) const;
    Character* characterById(int characterId);
    const Character* characterById(int characterId) const;
    bool updateCharacter(int characterId, const std::function<bool(Character&)>& update,
                         const std::wstring& historyLabel = {});
    Location* locationById(int locationId);
    const Location* locationById(int locationId) const;
    bool updateLocation(int locationId, const std::function<bool(Location&)>& update,
                        const std::wstring& historyLabel = {});
    ReferenceItem* referenceById(int referenceId);
    const ReferenceItem* referenceById(int referenceId) const;
    bool updateReference(int referenceId, const std::function<bool(ReferenceItem&)>& update,
                         const std::wstring& historyLabel = {});
    bool updateProject(const std::function<bool(Project&)>& update,
                       const std::wstring& historyLabel = {});
    Scene* selectedScenePtr();
    const Scene* selectedScenePtr() const;
    Scene& addScene();
    Scene& addSceneAfter(std::size_t index);
    Scene& duplicateScene(std::size_t index);
    bool moveScene(std::size_t index, int delta);
    bool deleteScene(std::size_t index);
    bool deleteSelectedScene();
    std::wstring fullScript() const;
    void applyFullScript(const std::wstring& text);
    bool applyStructuredScreenplay(const std::wstring& text, const std::wstring& formatMap,
                                   std::wstring* error = nullptr);
    bool renameSelectedScene(const std::wstring& title);
    void checkpoint(const std::wstring& label);
    bool undo(std::wstring* label = nullptr);
    bool redo(std::wstring* label = nullptr);
    bool canUndo() const { return !undoHistory_.empty(); }
    bool canRedo() const { return !redoHistory_.empty(); }
    void setHistoryLimit(std::size_t limit);
    std::size_t historyLimit() const { return historyLimit_; }
    void clearHistory();
    void subscribe(Listener listener);

private:
    struct HistoryEntry {
        Project project;
        int selectedSceneId = 0;
        std::wstring label;
    };

    Project project_ = CreateDefaultProject();
    std::filesystem::path filePath_;
    bool dirty_ = false;
    int selectedSceneId_ = 0;
    std::uint64_t revision_ = 0;
    ProjectSerializer serializer_;
    ScriptService scripts_;
    std::vector<Listener> listeners_;
    std::vector<HistoryEntry> undoHistory_;
    std::vector<HistoryEntry> redoHistory_;
    std::size_t historyLimit_ = 100;
    bool backupsEnabled_ = true;
    std::size_t maxBackups_ = 20;

    void notify(DocumentChange change);
    void trimHistory(std::vector<HistoryEntry>& history);
    void selectFirstScene();
    void synchronizeSelectionFlags(bool selectMatchingCard = true);
};

}  // namespace mezozoy
