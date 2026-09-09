#include "ProjectDocument.h"

#include "ScreenplayDocumentModel.h"

#include "../core/AppVersion.h"
#include "../core/Utf.h"
#include "RecoveryService.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {

std::wstring Timestamp(bool fileSafe = false) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &value);
    std::wostringstream output;
    output << std::put_time(&local, fileSafe ? L"%Y-%m-%d_%H-%M-%S" : L"%Y-%m-%dT%H:%M:%S");
    return output.str();
}

void CreateBackup(const std::filesystem::path& file, std::size_t maximumCount) {
    if (maximumCount == 0) return;
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return;
    const auto directory = file.parent_path() / L"Mezozoy Backups";
    std::filesystem::create_directories(directory, ec);
    if (ec) return;
    const auto backup = directory / (file.stem().wstring() + L"_" + Timestamp(true) + L".mzoy");
    std::filesystem::copy_file(file, backup, std::filesystem::copy_options::overwrite_existing, ec);

    std::vector<std::filesystem::directory_entry> entries;
    const std::wstring prefix = file.stem().wstring() + L"_";
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == L".mzoy" && entry.path().filename().wstring().starts_with(prefix))
            entries.push_back(entry);
    }
    if (entries.size() <= maximumCount) return;
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        std::error_code leftError, rightError;
        return left.last_write_time(leftError) < right.last_write_time(rightError);
    });
    for (std::size_t index = 0; index + maximumCount < entries.size(); ++index) std::filesystem::remove(entries[index].path(), ec);
}

}  // namespace

namespace mezozoy {

Project& ProjectDocument::editProject() {
    return project_;
}

void ProjectDocument::createNew() {
    project_ = CreateDefaultProject();
    filePath_.clear();
    dirty_ = false;
    selectFirstScene();
    clearHistory();
    ++revision_;
    notify(DocumentChange::Replaced);
}

bool ProjectDocument::open(const std::filesystem::path& path, std::wstring* error) {
    Project loaded;
    if (!serializer_.load(path, loaded, error)) return false;
    project_ = std::move(loaded);
    filePath_ = path;
    dirty_ = false;
    selectFirstScene();
    clearHistory();
    ++revision_;
    notify(DocumentChange::Replaced);
    return true;
}

bool ProjectDocument::openRecovered(const std::filesystem::path& originalPath, const std::filesystem::path& recoveryPath,
                                    std::wstring* error) {
    Project loaded;
    if (!serializer_.load(recoveryPath, loaded, error)) return false;
    project_ = std::move(loaded);
    filePath_ = originalPath;
    dirty_ = true;
    selectFirstScene();
    clearHistory();
    ++revision_;
    notify(DocumentChange::Replaced);
    return true;
}

bool ProjectDocument::save(std::wstring* error) {
    if (filePath_.empty()) {
        if (error) *error = L"Для проекта еще не выбран файл.";
        return false;
    }
    project_.ensureDocumentStructure();
    project_.normalizeOrder();
    project_.projectFormatVersion = L"0.9";
    if (project_.appVersionCreated.empty()) project_.appVersionCreated = AppVersion;
    project_.appVersionLastSaved = AppVersion;
    project_.lastSavedAt = Timestamp();
    if (backupsEnabled_) CreateBackup(filePath_, maxBackups_);
    if (!serializer_.save(filePath_, project_, error)) return false;
    std::error_code autosaveError;
    std::filesystem::remove(RecoveryService::AutosavePath(filePath_), autosaveError);
    dirty_ = false;
    notify(DocumentChange::Saved);
    return true;
}

bool ProjectDocument::saveAs(const std::filesystem::path& path, std::wstring* error) {
    const auto old = filePath_;
    filePath_ = path;
    if (!save(error)) { filePath_ = old; return false; }
    return true;
}

bool ProjectDocument::autosave(std::wstring* error) {
    if (!dirty_ || filePath_.empty()) return true;
    Project snapshot = project_;
    snapshot.ensureDocumentStructure();
    snapshot.normalizeOrder();
    snapshot.projectFormatVersion = L"0.9";
    snapshot.appVersionLastSaved = AppVersion;
    snapshot.lastSavedAt = Timestamp();
    const std::filesystem::path autosavePath = RecoveryService::AutosavePath(filePath_);
    return serializer_.save(autosavePath, snapshot, error);
}

void ProjectDocument::setBackupPolicy(bool enabled, std::size_t maxBackups) {
    backupsEnabled_ = enabled;
    maxBackups_ = std::min<std::size_t>(maxBackups, 1000);
}

void ProjectDocument::markChanged() {
    dirty_ = true;
    ++revision_;
    notify(DocumentChange::Content);
}

void ProjectDocument::setSelectedScene(std::size_t index, bool selectMatchingCard) {
    selectedSceneId_ = project_.scenes.empty() ? 0 : project_.scenes[std::min(index, project_.scenes.size() - 1)].id;
    synchronizeSelectionFlags(selectMatchingCard);
    notify(DocumentChange::Selection);
}

bool ProjectDocument::setSelectedSceneById(int sceneId, bool selectMatchingCard) {
    const std::size_t index = sceneIndexById(sceneId);
    if (index >= project_.scenes.size()) return false;
    setSelectedScene(index, selectMatchingCard);
    return true;
}

std::size_t ProjectDocument::sceneIndexById(int sceneId) const {
    for (std::size_t index = 0; index < project_.scenes.size(); ++index) if (project_.scenes[index].id == sceneId) return index;
    return project_.scenes.size();
}

std::size_t ProjectDocument::selectedScene() const {
    if (project_.scenes.empty()) return 0;
    const std::size_t index = sceneIndexById(selectedSceneId_);
    return index < project_.scenes.size() ? index : 0;
}

int ProjectDocument::selectedSceneId() const {
    if (project_.scenes.empty()) return 0;
    const std::size_t index = selectedScene();
    return index < project_.scenes.size() ? project_.scenes[index].id : 0;
}

Scene* ProjectDocument::sceneById(int sceneId) {
    const std::size_t index = sceneIndexById(sceneId);
    return index < project_.scenes.size() ? &project_.scenes[index] : nullptr;
}

const Scene* ProjectDocument::sceneById(int sceneId) const {
    const std::size_t index = sceneIndexById(sceneId);
    return index < project_.scenes.size() ? &project_.scenes[index] : nullptr;
}

Character* ProjectDocument::characterById(int characterId) {
    for (auto& item : project_.characters) if (item.id == characterId) return &item;
    return nullptr;
}

const Character* ProjectDocument::characterById(int characterId) const {
    for (const auto& item : project_.characters) if (item.id == characterId) return &item;
    return nullptr;
}

bool ProjectDocument::updateCharacter(int characterId, const std::function<bool(Character&)>& update,
                                      const std::wstring& historyLabel) {
    Character* item = characterById(characterId);
    if (!item || !update) return false;

    Project before;
    const bool keepHistory = !historyLabel.empty() && historyLimit_ > 0;
    if (keepHistory) before = project_;
    if (!update(*item)) return false;

    if (keepHistory) {
        undoHistory_.push_back({std::move(before), selectedSceneId(), historyLabel});
        trimHistory(undoHistory_);
        redoHistory_.clear();
    }
    markChanged();
    return true;
}

Location* ProjectDocument::locationById(int locationId) {
    for (auto& item : project_.locations) if (item.id == locationId) return &item;
    return nullptr;
}

const Location* ProjectDocument::locationById(int locationId) const {
    for (const auto& item : project_.locations) if (item.id == locationId) return &item;
    return nullptr;
}

bool ProjectDocument::updateLocation(int locationId, const std::function<bool(Location&)>& update,
                                     const std::wstring& historyLabel) {
    Location* item = locationById(locationId);
    if (!item || !update) return false;

    Project before;
    const bool keepHistory = !historyLabel.empty() && historyLimit_ > 0;
    if (keepHistory) before = project_;
    if (!update(*item)) return false;

    if (keepHistory) {
        undoHistory_.push_back({std::move(before), selectedSceneId(), historyLabel});
        trimHistory(undoHistory_);
        redoHistory_.clear();
    }
    markChanged();
    return true;
}

ReferenceItem* ProjectDocument::referenceById(int referenceId) {
    for (auto& item : project_.references) if (item.id == referenceId) return &item;
    return nullptr;
}

const ReferenceItem* ProjectDocument::referenceById(int referenceId) const {
    for (const auto& item : project_.references) if (item.id == referenceId) return &item;
    return nullptr;
}

bool ProjectDocument::updateReference(int referenceId, const std::function<bool(ReferenceItem&)>& update,
                                      const std::wstring& historyLabel) {
    ReferenceItem* item = referenceById(referenceId);
    if (!item || !update) return false;

    Project before;
    const bool keepHistory = !historyLabel.empty() && historyLimit_ > 0;
    if (keepHistory) before = project_;
    if (!update(*item)) return false;

    if (keepHistory) {
        undoHistory_.push_back({std::move(before), selectedSceneId(), historyLabel});
        trimHistory(undoHistory_);
        redoHistory_.clear();
    }
    markChanged();
    return true;
}

bool ProjectDocument::updateProject(const std::function<bool(Project&)>& update,
                                    const std::wstring& historyLabel) {
    if (!update) return false;

    Project before;
    const bool keepHistory = !historyLabel.empty() && historyLimit_ > 0;
    if (keepHistory) before = project_;
    if (!update(project_)) return false;

    if (keepHistory) {
        undoHistory_.push_back({std::move(before), selectedSceneId(), historyLabel});
        trimHistory(undoHistory_);
        redoHistory_.clear();
    }
    markChanged();
    return true;
}

Scene* ProjectDocument::selectedScenePtr() {
    return sceneById(selectedSceneId());
}

const Scene* ProjectDocument::selectedScenePtr() const {
    return sceneById(selectedSceneId());
}

Scene& ProjectDocument::addScene() {
    return addSceneAfter(project_.scenes.size());
}

Scene& ProjectDocument::addSceneAfter(std::size_t index) {
    checkpoint(L"Создание сцены");
    Scene scene;
    scene.id = project_.nextId();
    scene.title = L"НОВАЯ СЦЕНА";
    scene.text = scene.title + L"\r\n\r\nОписание действия.";
    scene.summary = L"Описание действия.";
    if (index < project_.scenes.size()) scene.documentId = project_.scenes[index].documentId;
    else if (const Scene* current = selectedScenePtr()) scene.documentId = current->documentId;
    if (scene.documentId == 0 && !project_.documents.empty()) scene.documentId = project_.documents.front().id;
    scene.createdAt = Timestamp();
    scene.updatedAt = scene.createdAt;
    const std::size_t insertAt = project_.scenes.empty() ? 0 : std::min(index + 1, project_.scenes.size());
    project_.scenes.insert(project_.scenes.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(scene));
    project_.normalizeOrder();
    selectedSceneId_ = project_.scenes[insertAt].id;
    markChanged();
    setSelectedSceneById(selectedSceneId_);
    return *sceneById(selectedSceneId_);
}

Scene& ProjectDocument::duplicateScene(std::size_t index) {
    if (index >= project_.scenes.size()) return addScene();
    checkpoint(L"Дублирование сцены");
    Scene copy = project_.scenes[index];
    int nextId = project_.nextId();
    copy.id = nextId++;
    copy.title += L" — копия";
    copy.cardX += 36.0f;
    copy.cardY += 36.0f;
    copy.selected = false;
    copy.createdAt = Timestamp();
    copy.updatedAt = copy.createdAt;
    for (auto& note : copy.notes) note.id = nextId++;
    for (auto& item : copy.breakdownItems) { item.id = nextId++; item.sceneId = copy.id; }
    for (auto& review : copy.reviewComments) { review.id = nextId++; review.sceneId = copy.id; }
    project_.scenes.insert(project_.scenes.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(copy));
    project_.normalizeOrder();
    selectedSceneId_ = project_.scenes[index + 1].id;
    markChanged();
    setSelectedSceneById(selectedSceneId_);
    return *sceneById(selectedSceneId_);
}

bool ProjectDocument::moveScene(std::size_t index, int delta) {
    if (index >= project_.scenes.size() || delta == 0) return false;
    const auto destinationValue = static_cast<long long>(index) + delta;
    if (destinationValue < 0 || destinationValue >= static_cast<long long>(project_.scenes.size())) return false;
    checkpoint(L"Перестановка сцены");
    const std::size_t destination = static_cast<std::size_t>(destinationValue);
    const int movedSceneId = project_.scenes[index].id;
    std::swap(project_.scenes[index], project_.scenes[destination]);
    project_.normalizeOrder();
    selectedSceneId_ = movedSceneId;
    markChanged();
    setSelectedSceneById(movedSceneId);
    return true;
}

bool ProjectDocument::deleteSelectedScene() {
    return deleteScene(selectedScene());
}

bool ProjectDocument::deleteScene(std::size_t index) {
    if (project_.scenes.size() <= 1 || index >= project_.scenes.size()) return false;
    checkpoint(L"Удаление сцены");
    const int deletedId = project_.scenes[index].id;
    project_.scenes.erase(project_.scenes.begin() + static_cast<std::ptrdiff_t>(index));
    if (selectedSceneId_ == deletedId || sceneIndexById(selectedSceneId_) >= project_.scenes.size())
        selectedSceneId_ = project_.scenes[std::min(index, project_.scenes.size() - 1)].id;
    project_.normalizeOrder();
    markChanged();
    setSelectedSceneById(selectedSceneId_);
    return true;
}

std::wstring ProjectDocument::fullScript() const {
    return scripts_.buildFullScript(project_);
}

void ProjectDocument::applyFullScript(const std::wstring& text) {
    if (text == fullScript()) return;
    std::vector<std::wstring> previousTexts;
    previousTexts.reserve(project_.scenes.size());
    for (const auto& scene : project_.scenes) previousTexts.push_back(scene.text);
    scripts_.applyFullScript(project_, text);
    for (std::size_t index = 0; index < project_.scenes.size() && index < previousTexts.size(); ++index) {
        if (project_.scenes[index].text != previousTexts[index]) {
            project_.scenes[index].rtf.clear();
            project_.scenes[index].screenplayFormats = scripts_.buildFormatMap(project_.scenes[index].text);
        }
    }
    markChanged();
}

bool ProjectDocument::applyStructuredScreenplay(const std::wstring& text,
                                                const std::wstring& formatMap,
                                                std::wstring* error) {
    const int previousSelectedId = selectedSceneId();
    const std::size_t previousSelectedIndex = selectedScene();
    std::vector<int> previousIds;
    std::vector<std::wstring> previousTitles;
    std::vector<std::wstring> previousTexts;
    std::vector<std::wstring> previousFormats;
    previousIds.reserve(project_.scenes.size());
    previousTitles.reserve(project_.scenes.size());
    previousTexts.reserve(project_.scenes.size());
    previousFormats.reserve(project_.scenes.size());
    for (const Scene& scene : project_.scenes) {
        previousIds.push_back(scene.id);
        previousTitles.push_back(scene.title);
        previousTexts.push_back(scene.text);
        previousFormats.push_back(scene.screenplayFormats);
    }

    ScreenplayDocumentModel model = ScreenplayDocumentModel::parse(text, formatMap, project_, error);
    if (model.empty() || !model.apply(project_, error)) return false;

    bool changed = project_.scenes.size() != previousIds.size();
    for (std::size_t index = 0; !changed && index < project_.scenes.size(); ++index) {
        const Scene& scene = project_.scenes[index];
        changed = scene.id != previousIds[index] || scene.title != previousTitles[index] ||
                  scene.text != previousTexts[index] || scene.screenplayFormats != previousFormats[index];
    }

    if (sceneById(previousSelectedId)) {
        selectedSceneId_ = previousSelectedId;
    } else if (!project_.scenes.empty()) {
        selectedSceneId_ = project_.scenes[std::min(previousSelectedIndex, project_.scenes.size() - 1)].id;
    } else {
        selectedSceneId_ = 0;
    }
    synchronizeSelectionFlags();
    if (changed) markChanged();
    if (error) error->clear();
    return true;
}

bool ProjectDocument::renameSelectedScene(const std::wstring& title) {
    const std::size_t index = selectedScene();
    if (index >= project_.scenes.size() || project_.scenes[index].title == title) return false;
    checkpoint(L"Переименование сцены");
    if (!scripts_.renameScene(project_, index, title)) return false;
    markChanged();
    return true;
}

void ProjectDocument::checkpoint(const std::wstring& label) {
    if (historyLimit_ == 0) return;
    undoHistory_.push_back({project_, selectedSceneId(), label.empty() ? L"Изменение проекта" : label});
    trimHistory(undoHistory_);
    redoHistory_.clear();
}

bool ProjectDocument::undo(std::wstring* label) {
    if (undoHistory_.empty()) return false;
    HistoryEntry entry = std::move(undoHistory_.back());
    undoHistory_.pop_back();
    redoHistory_.push_back({project_, selectedSceneId(), entry.label});
    trimHistory(redoHistory_);
    project_ = std::move(entry.project);
    selectedSceneId_ = entry.selectedSceneId;
    if (!sceneById(selectedSceneId_)) selectFirstScene();
    else synchronizeSelectionFlags();
    dirty_ = true;
    ++revision_;
    if (label) *label = entry.label;
    notify(DocumentChange::Replaced);
    return true;
}

bool ProjectDocument::redo(std::wstring* label) {
    if (redoHistory_.empty()) return false;
    HistoryEntry entry = std::move(redoHistory_.back());
    redoHistory_.pop_back();
    undoHistory_.push_back({project_, selectedSceneId(), entry.label});
    trimHistory(undoHistory_);
    project_ = std::move(entry.project);
    selectedSceneId_ = entry.selectedSceneId;
    if (!sceneById(selectedSceneId_)) selectFirstScene();
    else synchronizeSelectionFlags();
    dirty_ = true;
    ++revision_;
    if (label) *label = entry.label;
    notify(DocumentChange::Replaced);
    return true;
}

void ProjectDocument::setHistoryLimit(std::size_t limit) {
    historyLimit_ = std::min<std::size_t>(limit, 1000);
    trimHistory(undoHistory_);
    trimHistory(redoHistory_);
}

void ProjectDocument::clearHistory() {
    undoHistory_.clear();
    redoHistory_.clear();
}

void ProjectDocument::trimHistory(std::vector<HistoryEntry>& history) {
    if (historyLimit_ == 0) {
        history.clear();
        return;
    }
    if (history.size() > historyLimit_)
        history.erase(history.begin(), history.begin() + static_cast<std::ptrdiff_t>(history.size() - historyLimit_));
}

void ProjectDocument::subscribe(Listener listener) {
    listeners_.push_back(std::move(listener));
}

void ProjectDocument::notify(DocumentChange change) {
    for (const auto& listener : listeners_) if (listener) listener(change);
}

void ProjectDocument::selectFirstScene() {
    selectedSceneId_ = project_.scenes.empty() ? 0 : project_.scenes.front().id;
    synchronizeSelectionFlags();
}

void ProjectDocument::synchronizeSelectionFlags(bool selectMatchingCard) {
    for (auto& scene : project_.scenes) scene.selected = false;
    for (auto& section : project_.boardSections) section.selected = false;
    if (selectMatchingCard) {
        if (Scene* scene = sceneById(selectedSceneId_)) scene->selected = true;
    }
}

}  // namespace mezozoy
