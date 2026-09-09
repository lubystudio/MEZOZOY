#pragma once

#include "Page.h"
#include "PagedScriptView.h"
#include "../services/ProjectDocument.h"
#include "../services/ScreenplayLayoutService.h"
#include "../services/SettingsService.h"

#include <richedit.h>

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mezozoy::ui {

class ScriptPage final : public NativePage {
public:
    ScriptPage(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme);
    ~ScriptPage() override;
    void onLayout(int width, int height) override;
    void commit() override;
    bool handleCommand(int id, int code, HWND source) override;
    bool handleNotify(NMHDR* header) override;
    LRESULT onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void applyAppearance(const Theme& theme, float uiScale) override;
    void refreshFromDocument(DocumentChange change = DocumentChange::Replaced);
    void focusEditor();
    bool undoText(bool redo = false);
    void setSaveAction(std::function<bool()> action) { saveAction_ = std::move(action); }

private:
    enum class EditorMode { CurrentScene, FullScript };
    enum class DetectionKind { None, Character, Location };
    enum class AutocompleteKind { None, Character, HeadingPrefix, Location, Transition };
    struct LineContext {
        LONG start = 0;
        LONG end = 0;
        LONG caret = 0;
        int lineIndex = 0;
        std::wstring text;
    };
    enum : int {
        IdSceneList = 1101, IdAddScene, IdDeleteScene, IdEditor, IdTitle, IdSummary,
        IdHeading, IdParticipants, IdAction, IdCharacter, IdParenthetical, IdDialogue, IdTransition, IdNote, IdShot, IdLyrics,
        IdPaperLetter, IdCurrentScene, IdFullScript, IdSave, IdSearch, IdSearchPrevious, IdSearchNext, IdSearchClose,
        IdTodoList, IdAutocompleteList, IdAddDetected, IdIgnoreDetected, IdFormatLabel, IdPaperA4,
        IdChronometer, IdZoomOut, IdZoomFit, IdZoomIn, IdUndo, IdRedo, IdFormatChooser, IdInspectorToggle
    };

    ProjectDocument& document_;
    SettingsService& settings_;
    HWND header_{};
    HWND addButton_{};
    HWND deleteButton_{};
    HWND paperLetterButton_{};
    HWND paperA4Button_{};
    HWND currentSceneButton_{};
    HWND fullScriptButton_{};
    HWND saveButton_{};
    std::optional<bool> inspectorOverride_;
    HWND sceneList_{};
    HWND editorFrame_{};
    HWND editor_{};
    PagedScriptView pagedView_;
    void refreshPagedView(bool reveal = false);
    struct EditorState {
        std::wstring text, formats;
        CHARRANGE selection{};
        std::vector<Scene> scenes;
    };
    std::vector<EditorState> undo_, redo_;
    ULONGLONG lastTyping_ = 0;
    LONG lastTypingCaret_ = -1;
    bool historyRestoring_ = false;
    int historyDepth_ = 0;
    EditorState editorState();
    void checkpointEditor(bool typing = false);
    void undoEditor(bool redo = false);
    LONG dismissedCaret_ = -1;
    std::wstring dismissedText_;
    HWND properties_{};
    HWND title_{};
    HWND summary_{};
    HWND wordCounter_{};
    HWND projectCounter_{};
    HWND chronometer_{};
    HWND formatLabel_{};
    HWND autocompleteList_{};
    HWND detectionLabel_{};
    HWND addDetected_{};
    HWND ignoreDetected_{};
    HFONT scriptFont_{};
    std::function<bool()> saveAction_;
    std::vector<std::wstring> suggestions_;
    std::set<std::wstring> ignoredDetections_;
    std::wstring detectedName_;
    DetectionKind detectionKind_ = DetectionKind::None;
    AutocompleteKind autocompleteKind_ = AutocompleteKind::None;
    EditorMode editorMode_ = EditorMode::FullScript;
    ScriptLineType currentFormat_ = ScriptLineType::Action;
    ScriptLineType smartEnterSource_ = ScriptLineType::Action;
    LineContext lineBeforeEnter_{};
    LONG suggestionStart_ = 0;
    LONG suggestionEnd_ = 0;
    bool loading_ = false;
    bool syncingEditorToDocument_ = false;
    bool syncingSceneSelectionFromEditor_ = false;
    bool editorDirty_ = false;
    bool editorPending_ = false;
    bool pendingSmartEnter_ = false;
    bool suppressTabCharacter_ = false;
    bool suppressReturnCharacter_ = false;
    bool draggingLeftSplitter_ = false;
    bool draggingRightSplitter_ = false;
    int editorZoomNumerator_ = 0;
    int editorZoomDenominator_ = 0;
    std::vector<ScriptLineType> paragraphTypes_;
    std::vector<std::wstring> paragraphTexts_;
    std::optional<ScriptLineType> pendingParagraphType_;
    int pendingParagraphLine_ = -1;
    std::vector<LONG> sceneStarts_;
    struct PageBreakOverlay {
        HWND window{};
        LONG characterPosition = 0;
        int pageNumber = 0;
    };
    std::vector<PageBreakOverlay> pageBreaks_;
    std::optional<Scene> sceneClipboard_;
    int leftWidth_ = 250;
    int rightWidth_ = 258;
    static constexpr UINT_PTR SyncTimer = 8101;

    static LRESULT CALLBACK EditorSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                               UINT_PTR subclassId, DWORD_PTR reference);
    static LRESULT CALLBACK SceneListSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                                  UINT_PTR subclassId, DWORD_PTR reference);
    static LRESULT CALLBACK AutocompleteListSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                                         UINT_PTR subclassId, DWORD_PTR reference);

    void populateScenes();
    void loadProperties();
    void loadEditor();
    void selectScene(int index);
    void showSceneContextMenu(POINT screenPoint);
    void duplicateSelectedScene();
    void moveSelectedScene(int delta);
    void copySelectedScene(bool cut);
    void pasteScene();
    void saveCurrentEditorToScene();
    void syncEditorNow();
    void syncTitle();
    void updateCounters();
    void scrollToSelectedScene();
    void setEditorMode(EditorMode mode);
    void fitScriptViewport();
    ScreenplayPaper currentPaper() const;
    void setPaper(ScreenplayPaper paper);
    void updatePaperButtons();
    void rebuildPageBreaks();
    void clearPageBreaks();
    void positionPageBreaks();
    void applyFormat(ScriptLineType type);
    void createSceneFromCurrentParagraph();
    bool currentParagraphIsSceneTitle() const;
    std::size_t sceneIndexAtCaret() const;
    void syncSceneSelectionFromCaret();
    void syncSceneTitleFromEditor();
    void rebuildSceneStarts();
    LONG sceneStart(std::size_t index) const;
    void normalizeCurrentSceneTitleAppearance();
    void normalizeCurrentParagraphAppearance();
    void selectSelectedSceneTitleInEditor();
    void formatCurrentParagraph(ScriptLineType type);
    void applyParagraphStyle(LONG start, LONG end, ScriptLineType type);
    void initializeParagraphTypes(const std::wstring& formatMap);
    void reconcileParagraphTypes();
    ScriptLineType typeAtLine(int lineIndex) const;
    void setTypeAtLine(int lineIndex, ScriptLineType type);
    void applyStoredFormatting();
    std::wstring captureFullScriptFormats();
    std::wstring captureFocusedSceneFormats();
    ScriptLineType currentLineType() const;
    void cycleFormat(bool backwards);
    LineContext currentLine() const;
    void updateAutocompleteAndDetection();
    void showAutocomplete(const std::vector<std::wstring>& values, LONG replaceStart, LONG replaceEnd,
                          AutocompleteKind kind);
    void hideAutocomplete();
    bool acceptAutocomplete();
    void moveAutocompleteSelection(int delta);
    void detectCompletedLine(const LineContext& line);
    void showDetection(DetectionKind kind, const std::wstring& name);
    void hideDetection(bool ignore);
    void addDetectedItem();
    void recreateScriptFont();
    bool drawCustomItem(DRAWITEMSTRUCT* draw) override;
    std::wstring sceneListText(const Scene& scene) const;
};

}  // namespace mezozoy::ui
