#include "ScriptPage.h"

#include "Win32Util.h"
#include "UiStyle.h"
#include "../core/Utf.h"
#include "../services/ScreenplayDocumentModel.h"
#include "../services/ScriptService.h"

#include <windowsx.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <iomanip>
#include <sstream>

namespace mezozoy::ui {
namespace {

constexpr ScriptLineType EditorFormats[] = {
    ScriptLineType::SceneHeading, ScriptLineType::Action, ScriptLineType::Character,
    ScriptLineType::Parenthetical, ScriptLineType::Dialogue, ScriptLineType::Transition
};

int FormatChoice(ScriptLineType type) {
    const auto found = std::find(std::begin(EditorFormats), std::end(EditorFormats), type);
    return found == std::end(EditorFormats) ? 1 : static_cast<int>(found - std::begin(EditorFormats));
}

HWND Label(HWND parent, HINSTANCE instance, const wchar_t* text) {
    return CreateChild(L"STATIC", text, SS_LEFT, 0, parent, instance);
}

HWND Button(HWND parent, HINSTANCE instance, const wchar_t* text, int id) {
    return CreateChild(L"BUTTON", text, BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, id, parent, instance);
}

struct RtfBuffer {
    std::string bytes;
    std::size_t offset = 0;
};

DWORD CALLBACK StreamOutRtf(DWORD_PTR cookie, LPBYTE buffer, LONG bytes, LONG* written) {
    auto* data = reinterpret_cast<RtfBuffer*>(cookie);
    data->bytes.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(bytes));
    *written = bytes;
    return 0;
}

DWORD CALLBACK StreamInRtf(DWORD_PTR cookie, LPBYTE buffer, LONG bytes, LONG* read) {
    auto* data = reinterpret_cast<RtfBuffer*>(cookie);
    const std::size_t available = data->offset < data->bytes.size() ? data->bytes.size() - data->offset : 0;
    const std::size_t count = std::min<std::size_t>(available, static_cast<std::size_t>(bytes));
    if (count) std::copy_n(data->bytes.data() + data->offset, count, reinterpret_cast<char*>(buffer));
    data->offset += count;
    *read = static_cast<LONG>(count);
    return 0;
}

std::wstring ReadRtf(HWND editor) {
    RtfBuffer buffer;
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&buffer);
    stream.pfnCallback = StreamOutRtf;
    SendMessageW(editor, EM_STREAMOUT, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    std::wstring result;
    result.reserve(buffer.bytes.size());
    for (const unsigned char byte : buffer.bytes) result.push_back(static_cast<wchar_t>(byte));
    // RichEdit may include a C-string terminator in EM_STREAMOUT data. Keeping
    // it would make the surrounding .mzoy XML invalid even though the RTF is
    // otherwise intact.
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

bool WriteRtf(HWND editor, const std::wstring& rtf) {
    if (rtf.empty()) return false;
    RtfBuffer buffer;
    buffer.bytes.reserve(rtf.size());
    for (const wchar_t value : rtf) buffer.bytes.push_back(static_cast<char>(value & 0xff));
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&buffer);
    stream.pfnCallback = StreamInRtf;
    SendMessageW(editor, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    return stream.dwError == 0;
}

void ApplyEditorTextColor(HWND editor, COLORREF color) {
    CHARRANGE selection{};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    CHARRANGE all{0, -1};
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = color;
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
}

struct ParagraphRange {
    LONG start = 0;
    LONG end = 0;
    std::wstring text;
};

std::wstring EditorTextRange(HWND editor, LONG start, LONG end) {
    if (end <= start) return {};
    std::wstring value(static_cast<std::size_t>(end - start) + 1, L'\0');
    TEXTRANGEW range{{start, end}, value.data()};
    const LRESULT copied = SendMessageW(editor, EM_GETTEXTRANGE, 0, reinterpret_cast<LPARAM>(&range));
    value.resize(static_cast<std::size_t>(std::max<LRESULT>(0, copied)));
    return value;
}

std::vector<ParagraphRange> ParagraphsInRange(HWND editor, LONG contentStart, LONG contentEnd) {
    std::vector<ParagraphRange> result;
    if (contentEnd < contentStart) return result;
    const LONG total = GetWindowTextLengthW(editor);
    contentStart = std::clamp<LONG>(contentStart, 0, total);
    contentEnd = std::clamp<LONG>(contentEnd, contentStart, total);
    const std::wstring content = EditorTextRange(editor, contentStart, contentEnd);
    std::size_t paragraphStart = 0;
    while (paragraphStart <= content.size()) {
        std::size_t paragraphEnd = paragraphStart;
        while (paragraphEnd < content.size() && content[paragraphEnd] != L'\r' && content[paragraphEnd] != L'\n') ++paragraphEnd;
        const LONG start = contentStart + static_cast<LONG>(paragraphStart);
        const LONG end = contentStart + static_cast<LONG>(paragraphEnd);
        result.push_back({start, end, content.substr(paragraphStart, paragraphEnd - paragraphStart)});
        if (paragraphEnd == content.size()) break;
        paragraphStart = paragraphEnd + 1;
        if (content[paragraphEnd] == L'\r' && paragraphStart < content.size() && content[paragraphStart] == L'\n') ++paragraphStart;
    }
    return result;
}

LONG LineLengthFromPosition(HWND editor, LONG start) {
    const auto paragraphs = ParagraphsInRange(editor, start, GetWindowTextLengthW(editor));
    return paragraphs.empty() ? 0 : paragraphs.front().end - start;
}

std::wstring CanonicalText(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == L'\r') {
            if (index + 1 < value.size() && value[index + 1] == L'\n') ++index;
            result.push_back(L'\n');
        } else {
            result.push_back(value[index]);
        }
    }
    return result;
}

std::wstring TextAfterFirstLine(const std::wstring& value) {
    const std::wstring canonical = CanonicalText(value);
    const std::size_t newline = canonical.find(L'\n');
    return newline == std::wstring::npos ? std::wstring{} : canonical.substr(newline + 1);
}

void ReplaceEditorFirstLine(HWND editor, const std::wstring& title) {
    CHARRANGE previousSelection{};
    SendMessageW(editor, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&previousSelection));
    const LONG oldLength = LineLengthFromPosition(editor, 0);
    const LONG newLength = static_cast<LONG>(title.size());
    const LONG delta = newLength - oldLength;

    SendMessageW(editor, WM_SETREDRAW, FALSE, 0);
    CHARRANGE firstLine{0, oldLength};
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&firstLine));
    SendMessageW(editor, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(title.c_str()));

    auto translatePosition = [oldLength, newLength, delta](LONG value) {
        if (value <= oldLength) return std::min(value, newLength);
        return value + delta;
    };
    previousSelection.cpMin = translatePosition(previousSelection.cpMin);
    previousSelection.cpMax = translatePosition(previousSelection.cpMax);
    SendMessageW(editor, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&previousSelection));
    SendMessageW(editor, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(editor, nullptr, FALSE);
}

std::size_t SceneHeadingPrefixLength(const std::wstring& value) {
    const std::wstring upper = utf::ToUpper(utf::Trim(value));
    static constexpr const wchar_t* prefixes[] = {
        L"ИНТ./НАТ.", L"INT./EXT.", L"ИНТ.", L"НАТ.", L"INT.", L"EXT."
    };
    for (const wchar_t* prefix : prefixes) {
        const std::size_t length = std::wcslen(prefix);
        if (upper.starts_with(prefix)) return length;
    }
    return std::wstring::npos;
}

bool IsSceneHeading(const std::wstring& value) {
    return SceneHeadingPrefixLength(value) != std::wstring::npos;
}

std::wstring ExtractLocationName(const std::wstring& value) {
    std::wstring text = utf::Trim(value);
    const std::size_t prefixLength = SceneHeadingPrefixLength(text);
    if (prefixLength == std::wstring::npos) return {};
    text = utf::Trim(text.substr(prefixLength));
    std::size_t separator = text.find(L'—');
    if (separator == std::wstring::npos) separator = text.find(L'–');
    if (separator == std::wstring::npos) separator = text.find(L" - ");
    if (separator != std::wstring::npos) text = text.substr(0, separator);
    return utf::Trim(text);
}

std::wstring FormatRuntime(double minutes) {
    const long long totalSeconds = std::max<long long>(0, std::llround(minutes * 60.0));
    std::wostringstream value;
    value << totalSeconds / 60 << L':' << std::setfill(L'0') << std::setw(2) << totalSeconds % 60;
    return value.str();
}

std::wstring DisplayName(std::wstring value) {
    value = utf::ToLower(utf::Trim(value));
    bool wordStart = true;
    for (wchar_t& character : value) {
        if (std::iswalpha(character)) {
            if (wordStart) {
                const std::wstring upper = utf::ToUpper(std::wstring(1, character));
                if (!upper.empty()) character = upper.front();
            }
            wordStart = false;
        } else {
            wordStart = std::iswspace(character) || character == L'-';
        }
    }
    return value;
}

}  // namespace

ScriptPage::ScriptPage(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document), settings_(settings) {
    LoadLibraryW(L"Msftedit.dll");

    header_ = Label(hwnd_, instance_, L"Новый проект");
    addButton_ = Button(hwnd_, instance_, L"+ Сцена", IdAddScene);
    deleteButton_ = Button(hwnd_, instance_, L"Удалить", IdDeleteScene);
    paperLetterButton_ = Button(hwnd_, instance_, L"Hollywood / US Letter", IdPaperLetter);
    paperA4Button_ = Button(hwnd_, instance_, L"A4", IdPaperA4);
    currentSceneButton_ = Button(hwnd_, instance_, L"Фокус сцены", IdCurrentScene);
    fullScriptButton_ = Button(hwnd_, instance_, L"Цельный сценарий", IdFullScript);
    saveButton_ = Button(hwnd_, instance_, L"Сохранить", IdSave);
    sceneList_ = CreateChild(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                             IdSceneList, hwnd_, instance_);
    editorFrame_ = CreateChild(L"STATIC", L"", SS_OWNERDRAW, 0, hwnd_, instance_);
    editor_ = CreateChild(MSFTEDIT_CLASS, L"", ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL |
                          WS_TABSTOP | ES_NOHIDESEL | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                          IdEditor, hwnd_, instance_);

    pagedView_.attach(editor_);
    pagedView_.setTheme(theme_);
    properties_ = Label(hwnd_, instance_, L"Свойства сцены");
    HWND titleLabel = Label(hwnd_, instance_, L"Название");
    title_ = CreateChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, IdTitle, hwnd_, instance_, WS_EX_CLIENTEDGE);
    HWND summaryLabel = Label(hwnd_, instance_, L"Описание");
    summary_ = CreateChild(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                           IdSummary, hwnd_, instance_, WS_EX_CLIENTEDGE);
    HWND formatting = Label(hwnd_, instance_, L"Форматирование");
    const std::pair<const wchar_t*, int> formats[] = {
        {L"Заголовок сцены", IdHeading}, {L"Действие", IdAction}, {L"Персонаж", IdCharacter},
        {L"Ремарка", IdParenthetical}, {L"Реплика", IdDialogue}, {L"Переход", IdTransition}
    };
    for (const auto& item : formats) remember(Button(hwnd_, instance_, item.first, item.second));
    HWND formatChooser = CreateChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
                                     IdFormatChooser, hwnd_, instance_);
    for (const auto& item : formats)
        SendMessageW(formatChooser, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.first));
    remember(formatChooser);
    remember(Button(hwnd_, instance_, L"Свойства", IdInspectorToggle));
    for (const auto& item : {std::pair{L"−", IdZoomOut}, {L"По ширине", IdZoomFit}, {L"+", IdZoomIn},
                            {L"↶", IdUndo}, {L"↷", IdRedo}}) remember(Button(hwnd_, instance_, item.first, item.second));
    SetWindowTheme(sceneList_, L"", L"");

    formatLabel_ = Label(hwnd_, instance_, L"Формат: Описание действия");
    SetWindowLongPtrW(formatLabel_, GWLP_ID, IdFormatLabel);
    wordCounter_ = Label(hwnd_, instance_, L"Сцена: 0 слов · 0,0 стр · 0,0 мин");
    projectCounter_ = Label(hwnd_, instance_, L"Проект: 0 слов · 0,0 стр · 0,0 мин");
    chronometer_ = CreateChild(L"STATIC", L"Хрон.: 0:00 | 0:00", SS_RIGHT | SS_CENTERIMAGE,
                               IdChronometer, hwnd_, instance_);
    detectionLabel_ = Label(hwnd_, instance_, L"");
    addDetected_ = Button(hwnd_, instance_, L"Добавить", IdAddDetected);
    ignoreDetected_ = Button(hwnd_, instance_, L"Игнорировать", IdIgnoreDetected);
    autocompleteList_ = CreateChild(L"LISTBOX", L"", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                                    LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL,
                                    IdAutocompleteList, hwnd_, instance_);
    SetWindowTheme(autocompleteList_, L"", L"");

    for (HWND control : {header_, addButton_, deleteButton_, paperLetterButton_, paperA4Button_, currentSceneButton_, fullScriptButton_, saveButton_,
                         sceneList_, editorFrame_, editor_, properties_, titleLabel, title_, summaryLabel, summary_, formatting,
                         formatLabel_, wordCounter_, projectCounter_, chronometer_, detectionLabel_, addDetected_, ignoreDetected_, autocompleteList_}) {
        remember(control, control == header_ || control == properties_ || control == formatting);
    }

    ShowWindow(autocompleteList_, SW_HIDE);
    style::SetIcon(saveButton_,style::Icon::Save,L"Сохранить сценарий");
    style::SetIcon(GetDlgItem(hwnd_,IdUndo),style::Icon::Undo,L"Отменить");
    style::SetIcon(GetDlgItem(hwnd_,IdRedo),style::Icon::Redo,L"Повторить");
    ShowWindow(detectionLabel_, SW_HIDE);
    ShowWindow(addDetected_, SW_HIDE);
    ShowWindow(ignoreDetected_, SW_HIDE);
    SendMessageW(autocompleteList_, LB_SETITEMHEIGHT, 0, 28);

    recreateScriptFont();
    SendMessageW(editor_, EM_SETBKGNDCOLOR, 0, theme_.page);
    CHARFORMAT2W colors{};
    colors.cbSize = sizeof(colors);
    colors.dwMask = CFM_COLOR;
    colors.crTextColor = theme_.text;
    SendMessageW(editor_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&colors));
    SendMessageW(editor_, EM_SETLIMITTEXT, 0, 16 * 1024 * 1024);
    SendMessageW(editor_, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_KEYEVENTS | ENM_PROTECTED);
    SetWindowSubclass(editor_, EditorSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(sceneList_, SceneListSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(autocompleteList_, AutocompleteListSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
    updatePaperButtons();

    document_.subscribe([this](DocumentChange change) {
        if (IsWindow(hwnd_)) refreshFromDocument(change);
    });
    refreshFromDocument();
}

ScriptPage::~ScriptPage() {
    KillTimer(hwnd_, SyncTimer);
    clearPageBreaks();
    if (editor_) RemoveWindowSubclass(editor_, EditorSubclassProc, 1);
    if (sceneList_) RemoveWindowSubclass(sceneList_, SceneListSubclassProc, 1);
    if (autocompleteList_) RemoveWindowSubclass(autocompleteList_, AutocompleteListSubclassProc, 1);
    if (scriptFont_) DeleteObject(scriptFont_);
}

void ScriptPage::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    pagedView_.setTheme(theme);
    recreateScriptFont();
    SendMessageW(autocompleteList_, LB_SETITEMHEIGHT, 0,
                 std::max(24, static_cast<int>(std::lround(28.0f * uiScale_))));
    SendMessageW(editor_, EM_SETBKGNDCOLOR, 0, theme_.page);
    ApplyEditorTextColor(editor_, theme_.text);
    InvalidateRect(autocompleteList_, nullptr, TRUE);
    InvalidateRect(editorFrame_, nullptr, TRUE);
    InvalidateRect(editor_, nullptr, TRUE);
    for (const auto& pageBreak : pageBreaks_) InvalidateRect(pageBreak.window, nullptr, TRUE);
}

void ScriptPage::recreateScriptFont() {
    if (scriptFont_) DeleteObject(scriptFont_);
    HDC dc = GetDC(hwnd_);
    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    if (dc) ReleaseDC(hwnd_, dc);
    constexpr float size = 12.0f;
    scriptFont_ = CreateFontW(-MulDiv(static_cast<int>(size), dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Courier New");
    if (editor_) SetFont(editor_, scriptFont_);
}

bool ScriptPage::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (!draw) return false;
    if (draw->hwndItem == currentSceneButton_ || draw->hwndItem == fullScriptButton_) {
        const bool first = draw->hwndItem == currentSceneButton_;
        const bool active = (editorMode_ == EditorMode::CurrentScene) == first;
        RECT other{}; GetWindowRect(first ? fullScriptButton_ : currentSceneButton_, &other);
        RECT shell = draw->rcItem;
        if (first) shell.right += other.right-other.left; else shell.left -= other.right-other.left;
        style::Fill(draw->hDC,draw->rcItem,theme_.background);
        style::Surface(draw->hDC,shell,theme_.panel,theme_.border);
        RECT segment=draw->rcItem; InflateRect(&segment,-3,-3);
        const bool hot=GetPropW(draw->hwndItem,style::Hot)!=nullptr;
        if(active || hot) style::Surface(draw->hDC,segment,active?theme_.selection:theme_.panelAlt,
                                        active?Theme::Blend(theme_.panel,theme_.accent,55):theme_.panelAlt,4);
        SelectObject(draw->hDC,uiFont_); SetBkMode(draw->hDC,TRANSPARENT);
        SetTextColor(draw->hDC,active?theme_.text:theme_.textMuted);
        DrawTextW(draw->hDC,first?L"Сцена":L"Весь сценарий",-1,&draw->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        return true;
    }
    if (draw->hwndItem == sceneList_ && draw->CtlType == ODT_LISTBOX) {
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        HBRUSH brush = CreateSolidBrush(selected ? theme_.selection : theme_.panel);
        FillRect(draw->hDC, &draw->rcItem, brush);
        DeleteObject(brush);
        if (draw->itemID >= document_.project().scenes.size()) return true;
        const Scene& scene = document_.project().scenes[draw->itemID];
        const int pad = static_cast<int>(12 * uiScale_);
        if (selected) {
            RECT stripe = draw->rcItem; stripe.right = stripe.left + 3;
            brush = CreateSolidBrush(theme_.accent); FillRect(draw->hDC, &stripe, brush); DeleteObject(brush);
        }
        SetBkMode(draw->hDC, TRANSPARENT);
        SelectObject(draw->hDC, uiFont_);
        SetTextColor(draw->hDC, selected ? theme_.text : theme_.textMuted);
        RECT title = draw->rcItem;
        title.left += pad; title.right -= pad; title.top += pad; title.bottom = title.top + static_cast<int>(23 * uiScale_);
        std::wstring text = std::to_wstring(scene.orderIndex + 1) + L".  " + scene.title;
        DrawTextW(draw->hDC, text.c_str(), -1, &title, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT summary = title; summary.top = title.bottom + 3; summary.bottom = draw->rcItem.bottom - pad;
        SetTextColor(draw->hDC, theme_.textMuted);
        text = scene.summary.empty() ? L"Сцена" : scene.summary;
        DrawTextW(draw->hDC, text.c_str(), -1, &summary, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT separator = draw->rcItem; separator.top = separator.bottom - 1;
        brush = CreateSolidBrush(theme_.border); FillRect(draw->hDC, &separator, brush); DeleteObject(brush);
        return true;
    }
    if (draw->hwndItem == autocompleteList_ && draw->CtlType == ODT_LISTBOX) {
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        HBRUSH background = CreateSolidBrush(selected ? theme_.selection : theme_.panelAlt);
        FillRect(draw->hDC, &draw->rcItem, background);
        DeleteObject(background);
        if (draw->itemID != static_cast<UINT>(-1)) {
            if (selected) {
                RECT accent = draw->rcItem;
                accent.right = accent.left + std::max(3, static_cast<int>(std::lround(3.0f * uiScale_)));
                HBRUSH accentBrush = CreateSolidBrush(theme_.accent);
                FillRect(draw->hDC, &accent, accentBrush);
                DeleteObject(accentBrush);
            }
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, theme_.text);
            SelectObject(draw->hDC, uiFont_);
            RECT textRect = draw->rcItem;
            textRect.left += std::max(10, static_cast<int>(std::lround(12.0f * uiScale_)));
            textRect.right -= 8;
            const int length = static_cast<int>(SendMessageW(autocompleteList_, LB_GETTEXTLEN, draw->itemID, 0));
            std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
            if (length >= 0) {
                SendMessageW(autocompleteList_, LB_GETTEXT, draw->itemID,
                             reinterpret_cast<LPARAM>(value.data()));
                value.resize(static_cast<std::size_t>(length));
            } else {
                value.clear();
            }
            DrawTextW(draw->hDC, value.c_str(), -1, &textRect,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        return true;
    }
    const auto pageBreak = std::find_if(pageBreaks_.begin(), pageBreaks_.end(),
        [&](const PageBreakOverlay& item) { return item.window == draw->hwndItem; });
    if (pageBreak != pageBreaks_.end()) {
        HBRUSH background = CreateSolidBrush(theme_.background);
        FillRect(draw->hDC, &draw->rcItem, background);
        DeleteObject(background);
        HPEN line = CreatePen(PS_SOLID, 1, theme_.border);
        HGDIOBJ oldPen = SelectObject(draw->hDC, line);
        MoveToEx(draw->hDC, draw->rcItem.left, draw->rcItem.top, nullptr);
        LineTo(draw->hDC, draw->rcItem.right, draw->rcItem.top);
        MoveToEx(draw->hDC, draw->rcItem.left, draw->rcItem.bottom - 1, nullptr);
        LineTo(draw->hDC, draw->rcItem.right, draw->rcItem.bottom - 1);
        SelectObject(draw->hDC, oldPen);
        DeleteObject(line);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, theme_.textMuted);
        RECT label = draw->rcItem;
        DrawTextW(draw->hDC, WindowText(draw->hwndItem).c_str(), -1, &label,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return true;
    }
    if (draw->hwndItem != editorFrame_) return false;
    HBRUSH page = CreateSolidBrush(theme_.page);
    FillRect(draw->hDC, &draw->rcItem, page);
    DeleteObject(page);
    HBRUSH border = CreateSolidBrush(theme_.border);
    FrameRect(draw->hDC, &draw->rcItem, border);
    DeleteObject(border);
    return true;
}

void ScriptPage::onLayout(int width, int height) {
    hideAutocomplete();
    const auto px = [this](int value) { return std::max(1, static_cast<int>(std::lround(value * uiScale_))); };
    const bool inspectorVisible = inspectorOverride_.value_or(width >= px(1280));
    const bool navigatorVisible = width >= px(680);
    const int left = navigatorVisible ? std::min(px(leftWidth_), width / 4) : 0;
    const int right = inspectorVisible ? std::min(px(rightWidth_), width / 4) : 0;
    const int center = width - left - right;
    const int top = px(56), bottom = px(34);
    auto place = [&](HWND control, int x, int y, int w, int h, bool visible = true) {
        ShowWindow(control, visible ? SW_SHOWNA : SW_HIDE);
        if (visible) SetWindowPos(control, nullptr, x, y, std::max(1,w), std::max(1,h), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(header_, px(16), px(16), left - px(24), px(24), navigatorVisible);
    place(sceneList_, 0, top, left, height - top, navigatorVisible);
    SendMessageW(sceneList_, LB_SETITEMHEIGHT, 0, px(78));
    int x = left + px(12);
    auto tool = [&](HWND button, int w, bool visible = true) {
        place(button, x, px(12), px(w), px(32), visible);
        if (visible) x += px(w + 5);
    };
    tool(addButton_, 86);
    tool(GetDlgItem(hwnd_, IdUndo), 32);
    tool(GetDlgItem(hwnd_, IdRedo), 32);
    tool(paperLetterButton_, 102, center >= px(600));
    tool(paperA4Button_, 42, center >= px(600));
    tool(currentSceneButton_, 80, center >= px(760));
    if (center >= px(760)) x -= px(5);
    tool(fullScriptButton_, 124, center >= px(760));
    tool(saveButton_, 36, center >= px(600));
    tool(GetDlgItem(hwnd_, IdInspectorToggle), 86, center >= px(420));
    ShowWindow(deleteButton_, SW_HIDE);
    ShowWindow(editorFrame_, SW_HIDE);
    place(editor_, left + 1, top, std::max(1, center - 2), height - top - bottom);
    pagedView_.resize();
    editorZoomNumerator_ = pagedView_.zoom();
    editorZoomDenominator_ = 100;
    place(formatLabel_, left + px(12), height - bottom + px(7), std::max(100, center - px(230)), px(22), inspectorVisible);
    const int choiceWidth = center >= px(600) ? std::min(px(260), center - px(400)) : center - px(190);
    place(GetDlgItem(hwnd_, IdFormatChooser), left + px(12), height - bottom + px(2), choiceWidth, px(220), !inspectorVisible);
    SendMessageW(GetDlgItem(hwnd_, IdFormatChooser), CB_SETCURSEL, FormatChoice(currentFormat_), 0);
    const int zoomX = left + center - px(186);
    place(GetDlgItem(hwnd_, IdZoomOut), zoomX, height - bottom + px(2), px(30), px(28));
    place(GetDlgItem(hwnd_, IdZoomFit), zoomX + px(34), height - bottom + px(2), px(108), px(28));
    place(GetDlgItem(hwnd_, IdZoomIn), zoomX + px(146), height - bottom + px(2), px(30), px(28));
    const int fieldX = width - right + px(16), fieldWidth = right - px(32);
    auto field = [&](HWND control, int y, int h) { place(control, fieldX, px(y), fieldWidth, px(h), inspectorVisible); };
    field(properties_, 18, 24);
    field(chronometer_, 46, 26);
    field(title_, 102, 32);
    field(summary_, 168, 80);
    for (HWND child = GetWindow(hwnd_, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        const auto text = WindowText(child);
        if (text == L"Название") field(child, 80, 20);
        else if (text == L"Описание") field(child, 146, 20);
        else if (text == L"Форматирование") field(child, 268, 24);
    }
    int y = 306;
    for (int id : {IdHeading, IdAction, IdCharacter, IdParenthetical, IdDialogue, IdTransition}) {
        field(GetDlgItem(hwnd_, id), y, 36);
        y += 42;
    }
    field(wordCounter_, y + 14, 38);
    field(projectCounter_, y + 54, 38);
    if (!inspectorVisible)
        place(chronometer_, left + center - px(360), height - bottom + px(3), px(180), px(26), center >= px(600));
    const bool detected = detectionKind_ != DetectionKind::None && inspectorVisible;
    place(detectionLabel_, fieldX, px(y + 100), fieldWidth, px(40), detected);
    place(addDetected_, fieldX, px(y + 144), px(86), px(30), detected);
    place(ignoreDetected_, fieldX + px(92), px(y + 144), px(108), px(30), detected);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void ScriptPage::commit() {
    syncEditorNow();
}

bool ScriptPage::handleCommand(int id, int code, HWND) {
    if (id == IdInspectorToggle && code == BN_CLICKED) {
        inspectorOverride_ = !IsWindowVisible(properties_);
        RECT client{};
        GetClientRect(hwnd_, &client);
        onLayout(client.right, client.bottom);
        return true;
    }
    if (id == IdFormatChooser && code == CBN_SELCHANGE) {
        const LRESULT selected = SendMessageW(GetDlgItem(hwnd_, IdFormatChooser), CB_GETCURSEL, 0, 0);
        if (selected == 0) createSceneFromCurrentParagraph();
        else if (selected > 0 && selected < static_cast<LRESULT>(std::size(EditorFormats)))
            applyFormat(EditorFormats[selected]);
        return true;
    }
    if (code == BN_CLICKED && (id == IdZoomOut || id == IdZoomFit || id == IdZoomIn)) {
        pagedView_.setZoom(id == IdZoomFit ? 0 : pagedView_.zoom() + (id == IdZoomOut ? -10 : 10));
        const auto label = id == IdZoomFit ? std::wstring(L"По ширине") : std::to_wstring(pagedView_.zoom()) + L"%";
        SetWindowTextW(GetDlgItem(hwnd_, IdZoomFit), label.c_str());
        return true;
    }
    if (code == BN_CLICKED && (id == IdUndo || id == IdRedo)) { undoEditor(id == IdRedo); SetFocus(editor_); return true; }
    if (id == IdSceneList && code == LBN_SELCHANGE) {
        selectScene(static_cast<int>(SendMessageW(sceneList_, LB_GETCURSEL, 0, 0)));
        return true;
    }
    if (id == IdAddScene && code == BN_CLICKED) {
        commit();
        document_.addScene();
        selectSelectedSceneTitleInEditor();
        return true;
    }
    if (id == IdDeleteScene && code == BN_CLICKED) {
        commit();
        if (!settings_.get().confirmDeleteScene ||
            MessageBoxW(hwnd_, L"Удалить выбранную сцену?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            document_.deleteSelectedScene();
        }
        return true;
    }
    if (id == IdPaperLetter && code == BN_CLICKED) { setPaper(ScreenplayPaper::HollywoodLetter); return true; }
    if (id == IdPaperA4 && code == BN_CLICKED) { setPaper(ScreenplayPaper::A4); return true; }
    if (id == IdCurrentScene && code == BN_CLICKED) { setEditorMode(EditorMode::CurrentScene); return true; }
    if (id == IdFullScript && code == BN_CLICKED) { setEditorMode(EditorMode::FullScript); return true; }
    if (id == IdSave && code == BN_CLICKED) { commit(); if (saveAction_) saveAction_(); return true; }
    if (id == IdEditor && code == EN_CHANGE && !loading_) {
        reconcileParagraphTypes();
        if (editorMode_ == EditorMode::FullScript) rebuildSceneStarts();
        editorDirty_ = true;
        editorPending_ = true;
        SetTimer(hwnd_, SyncTimer, 400, nullptr);
        if (editorMode_ == EditorMode::FullScript) syncSceneSelectionFromCaret();
        normalizeCurrentParagraphAppearance();
        if (currentParagraphIsSceneTitle()) {
            syncSceneTitleFromEditor();
            normalizeCurrentSceneTitleAppearance();
        }
        refreshPagedView(true);
        updateAutocompleteAndDetection();
        return true;
    }
    if (id == IdTitle && code == EN_CHANGE && !loading_) { syncTitle(); return true; }
    if (id == IdSummary && code == EN_CHANGE && !loading_) {
        if (Scene* scene = document_.selectedScenePtr()) {
            const std::wstring summary = WindowText(summary_);
            if (scene->summary != summary) {
                scene->summary = summary;
                document_.markChanged();
                populateScenes();
            }
        }
        return true;
    }
    if (id == IdHeading && code == BN_CLICKED) {
        createSceneFromCurrentParagraph();
        return true;
    }
    if (code == BN_CLICKED && (id == IdAction || id == IdCharacter || id == IdParenthetical ||
                               id == IdDialogue || id == IdTransition)) {
        ScriptLineType type = ScriptLineType::Action;
        switch (id) {
            case IdHeading: type = ScriptLineType::SceneHeading; break;
            case IdAction: type = ScriptLineType::Action; break;
            case IdCharacter: type = ScriptLineType::Character; break;
            case IdParenthetical: type = ScriptLineType::Parenthetical; break;
            case IdDialogue: type = ScriptLineType::Dialogue; break;
            case IdTransition: type = ScriptLineType::Transition; break;
        }
        applyFormat(type);
        return true;
    }
    if (id == IdAutocompleteList && code == LBN_DBLCLK) { acceptAutocomplete(); return true; }
    if (id == IdAddDetected && code == BN_CLICKED) { addDetectedItem(); return true; }
    if (id == IdIgnoreDetected && code == BN_CLICKED) { hideDetection(true); return true; }
    return false;
}

bool ScriptPage::handleNotify(NMHDR* header) {
    if (!header || header->hwndFrom != editor_) return false;
    if (header->code == EN_PROTECTED) return true;
    if (header->code == EN_SELCHANGE && !loading_) {
        if (editorMode_ == EditorMode::FullScript) syncSceneSelectionFromCaret();
        currentFormat_ = currentLineType();
        SendMessageW(GetDlgItem(hwnd_, IdFormatChooser), CB_SETCURSEL, FormatChoice(currentFormat_), 0);
        if (settings_.get().lineTypeIndicator)
            SetWindowTextW(formatLabel_, (L"Формат: " + ScriptService::typeName(currentFormat_)).c_str());
        updateAutocompleteAndDetection();
        positionPageBreaks();
    }
    return false;
}

LRESULT ScriptPage::onMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if ((message == WM_VSCROLL || message == WM_HSCROLL) && lParam &&
        (GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == 1190 || GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == 1191)) {
        SendMessageW(editor_, message, wParam, lParam);
        return 1;
    }
    // Lightweight automation query used by the packaged GUI smoke test. It
    // avoids passing pointers through a cross-process RichEdit WM_USER call.
    if (message == WM_APP + 71)
        return MAKELPARAM(pagedView_.zoom(), 100);
    if (message == WM_APP + 84) return static_cast<LRESULT>(pagedView_.document().scriptPageCount);
    if (message == WM_APP + 85) { undoEditor(wParam != 0); return 1; }
    if (message == WM_APP + 86) {
        POINTL point{};
        SendMessageW(editor_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), static_cast<LPARAM>(wParam));
        return MAKELPARAM(point.x, point.y);
    }
    if (message == WM_APP + 87) return currentLine().caret;
    if (message == WM_APP + 72) {
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_HIDDEN | CFM_PROTECTED;
        SendMessageW(editor_, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        return static_cast<LRESULT>(format.dwEffects & (CFE_HIDDEN | CFE_PROTECTED));
    }
    if (message == WM_APP + 74) {
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        SendMessageW(editor_, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        return static_cast<LRESULT>(format.crTextColor);
    }
    if (message == WM_APP + 75)
        return currentPaper() == ScreenplayPaper::A4 ? 1 : 0;
    if (message == WM_APP + 76) {
        const ScreenplayPageSpec spec = ScreenplayLayoutService::pageSpec(currentPaper());
        return MAKELPARAM(spec.bodyLineCapacity, 12);
    }
    if (message == WM_APP + 77) {
        SetFocus(editor_);
        return GetFocus() == editor_ ? 1 : 0;
    }
    if (message == WM_APP + 78) {
        updateAutocompleteAndDetection();
        const int count = static_cast<int>(SendMessageW(autocompleteList_, LB_GETCOUNT, 0, 0));
        return MAKELPARAM(count, IsWindowVisible(autocompleteList_) ? 1 : 0);
    }
    if (message == WM_APP + 79) {
        const LineContext line = currentLine();
        return MAKELPARAM(static_cast<int>(currentLineType()), line.lineIndex);
    }
    if (message == WM_APP + 80) {
        selectSelectedSceneTitleInEditor();
        return GetFocus() == editor_ ? 1 : 0;
    }
    if (message == WM_APP + 83) {
        if (IsWindowVisible(autocompleteList_) && SendMessageW(autocompleteList_, LB_GETCOUNT, 0, 0) > 0) {
            SetWindowPos(autocompleteList_, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(autocompleteList_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
        }
        return 1;
    }
    if (message == WM_APP + 73) {
        const std::size_t index = static_cast<std::size_t>(wParam);
        if (editorMode_ != EditorMode::FullScript || index >= document_.project().scenes.size()) return 0;
        const LONG start = sceneStart(index);
        if (start < 0) return 0;
        const LONG length = LineLengthFromPosition(editor_, start);
        CHARRANGE titleRange{start, start + length};
        SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&titleRange));
        SendMessageW(editor_, EM_SCROLLCARET, 0, 0);
        SetFocus(editor_);
        return 1;
    }
    if (message == WM_TIMER && wParam == SyncTimer) {
        // Capturing paragraph formats walks the RichEdit selection. While an
        // autocomplete popup is open that would move the effective caret and
        // make the popup disappear before the writer can accept it. Commit as
        // soon as the choice is closed instead.
        if (IsWindowVisible(autocompleteList_)) {
            SetWindowPos(autocompleteList_, HWND_TOP, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(autocompleteList_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
            SetTimer(hwnd_, SyncTimer, 200, nullptr);
            return 1;
        }
        if (editorPending_) syncEditorNow();
        if (GetFocus() == editor_) updateAutocompleteAndDetection();
        return 1;
    }
    if (message == WM_LBUTTONDOWN) {
        const int x = GET_X_LPARAM(lParam);
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        if (std::abs(x - leftWidth_) <= 6) { draggingLeftSplitter_ = true; SetCapture(hwnd_); return 1; }
        if (std::abs(x - (rect.right - rightWidth_)) <= 6) { draggingRightSplitter_ = true; SetCapture(hwnd_); return 1; }
    }
    if (message == WM_MOUSEMOVE && (draggingLeftSplitter_ || draggingRightSplitter_)) {
        RECT rect{};
        GetClientRect(hwnd_, &rect);
        const int x = GET_X_LPARAM(lParam);
        if (draggingLeftSplitter_) leftWidth_ = std::clamp(x, 200, std::max(200, static_cast<int>(rect.right - rightWidth_ - 520)));
        else rightWidth_ = std::clamp(static_cast<int>(rect.right - x), 240, std::max(240, static_cast<int>(rect.right - leftWidth_ - 520)));
        onLayout(rect.right, rect.bottom);
        return 1;
    }
    if (message == WM_LBUTTONUP && (draggingLeftSplitter_ || draggingRightSplitter_)) {
        draggingLeftSplitter_ = draggingRightSplitter_ = false;
        ReleaseCapture();
        return 1;
    }
    return 0;
}

void ScriptPage::refreshFromDocument(DocumentChange change) {
    if (loading_) return;
    const HWND focus = GetFocus();
    const bool editingThisPage = focus && (focus == hwnd_ || IsChild(hwnd_, focus));
    if (change == DocumentChange::Saved) return;
    if (change == DocumentChange::Selection && editorMode_ == EditorMode::FullScript) {
        // Moving the caret into another scene only changes the active model
        // object. Reloading RichEdit here would destroy the user's selection,
        // move the caret and make the continuous screenplay impossible to edit.
        const bool previousLoading = loading_;
        loading_ = true;
        populateScenes();
        loadProperties();
        updateCounters();
        loading_ = previousLoading;
        if (!syncingSceneSelectionFromEditor_) scrollToSelectedScene();
        return;
    }
    if (change == DocumentChange::Content && (syncingEditorToDocument_ ||
        (editingThisPage && (editorMode_ != EditorMode::FullScript || sceneStarts_.size() == document_.project().scenes.size())))) {
        const bool previousLoading = loading_;
        loading_ = true;
        populateScenes();
        // Do not rewrite a focused property control on every EN_CHANGE. Apart
        // from visible flicker, SetWindowText moves its caret back to the
        // beginning and makes ordinary typing appear in reverse order.
        if (focus != title_ && focus != summary_) loadProperties();
        updateCounters();
        loading_ = previousLoading;
        return;
    }

    loading_ = true;
    SetWindowTextW(header_, document_.project().title.c_str());
    recreateScriptFont();
    populateScenes();
    loadProperties();
    loadEditor();
    updateCounters();
    loading_ = false;

    if (change == DocumentChange::Selection && editorMode_ == EditorMode::FullScript) scrollToSelectedScene();
}

void ScriptPage::populateScenes() {
    const int current = static_cast<int>(document_.selectedScene());
    const int top = static_cast<int>(SendMessageW(sceneList_, LB_GETTOPINDEX, 0, 0));
    SendMessageW(sceneList_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(sceneList_, LB_RESETCONTENT, 0, 0);
    for (const auto& scene : document_.project().scenes) {
        const std::wstring text = sceneListText(scene);
        SendMessageW(sceneList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
    }
    if (!document_.project().scenes.empty()) SendMessageW(sceneList_, LB_SETCURSEL, current, 0);
    SendMessageW(sceneList_, LB_SETTOPINDEX, std::max(0, top), 0);
    SendMessageW(sceneList_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(sceneList_, nullptr, TRUE);
}

void ScriptPage::loadProperties() {
    const Scene* scene = document_.selectedScenePtr();
    SetWindowTextW(title_, scene ? scene->title.c_str() : L"");
    SetWindowTextW(summary_, scene ? scene->summary.c_str() : L"");
}

void ScriptPage::loadEditor() {
    ScopedRedrawLock redraw(editor_, RDW_INVALIDATE | RDW_FRAME);
    if (!historyRestoring_) { undo_.clear(); redo_.clear(); lastTyping_ = 0; }
    const bool previousLoading = loading_;
    loading_ = true;
    SendMessageW(editor_, EM_SETREADONLY, FALSE, 0);
    SendMessageW(title_, EM_SETREADONLY, FALSE, 0);
    SendMessageW(summary_, EM_SETREADONLY, FALSE, 0);

    if (editorMode_ == EditorMode::FullScript) {
        SetWindowTextW(editor_, document_.fullScript().c_str());
    } else if (Scene* scene = document_.selectedScenePtr()) {
        bool loadedRtf = false;
        if (!scene->rtf.empty()) {
            loadedRtf = WriteRtf(editor_, scene->rtf);
            if (loadedRtf && CanonicalText(WindowText(editor_)) != CanonicalText(scene->text)) {
                const std::wstring existingText = WindowText(editor_);
                if (TextAfterFirstLine(existingText) == TextAfterFirstLine(scene->text)) {
                    const LONG firstLineLength = static_cast<LONG>(SendMessageW(editor_, EM_LINELENGTH, 0, 0));
                    SendMessageW(editor_, EM_SETSEL, 0, firstLineLength);
                    SendMessageW(editor_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(scene->title.c_str()));
                    loadedRtf = CanonicalText(WindowText(editor_)) == CanonicalText(scene->text);
                    if (loadedRtf) scene->rtf = ReadRtf(editor_);
                } else {
                    loadedRtf = false;
                }
            }
        }
        if (!loadedRtf) SetWindowTextW(editor_, scene->text.c_str());
    } else {
        SetWindowTextW(editor_, L"");
    }
    std::wstring formatMap;
    if (editorMode_ == EditorMode::FullScript) {
        formatMap = ScreenplayDocumentModel::fromProject(document_.project()).formatMap();
    } else if (const Scene* scene = document_.selectedScenePtr()) {
        formatMap = scene->screenplayFormats;
    }
    initializeParagraphTypes(formatMap);
    ApplyEditorTextColor(editor_, theme_.text);
    applyStoredFormatting();
    rebuildSceneStarts();
    rebuildPageBreaks();
    pagedView_.resetScroll();
    editorDirty_ = false;
    editorPending_ = false;
    KillTimer(hwnd_, SyncTimer);
    SetWindowTextW(currentSceneButton_, L"Сцена");
    SetWindowTextW(fullScriptButton_, L"Весь сценарий");
    loading_ = previousLoading;
}

void ScriptPage::selectScene(int index) {
    saveCurrentEditorToScene();
    if (index < 0 || index >= static_cast<int>(document_.project().scenes.size())) return;
    document_.setSelectedScene(static_cast<std::size_t>(index),
                               settings_.get().keepScriptCardsSync && settings_.get().selectMatchingCard);
    loading_ = true;
    loadProperties();
    if (editorMode_ == EditorMode::CurrentScene) loadEditor();
    loading_ = false;
    if (editorMode_ == EditorMode::FullScript) scrollToSelectedScene();
    updateCounters();
}

void ScriptPage::showSceneContextMenu(POINT screenPoint) {
    int selected = static_cast<int>(SendMessageW(sceneList_, LB_GETCURSEL, 0, 0));
    if (screenPoint.x != -1 || screenPoint.y != -1) {
        POINT client = screenPoint;
        ScreenToClient(sceneList_, &client);
        const LRESULT item = SendMessageW(sceneList_, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y));
        if (!HIWORD(item) && LOWORD(item) < document_.project().scenes.size()) {
            selected = LOWORD(item);
            SendMessageW(sceneList_, LB_SETCURSEL, selected, 0);
            selectScene(selected);
        }
    } else if (selected >= 0) {
        RECT itemRect{};
        SendMessageW(sceneList_, LB_GETITEMRECT, selected, reinterpret_cast<LPARAM>(&itemRect));
        screenPoint = {itemRect.left + 18, itemRect.bottom};
        ClientToScreen(sceneList_, &screenPoint);
    }
    if (selected < 0 || selected >= static_cast<int>(document_.project().scenes.size())) return;

    enum : int { Rename = 7401, Duplicate, Copy, Cut, Paste, MoveUp, MoveDown, Delete };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, Rename, L"Переименовать");
    AppendMenuW(menu, MF_STRING, Duplicate, L"Дублировать");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, Copy, L"Копировать сцену\tCtrl+C");
    AppendMenuW(menu, MF_STRING, Cut, L"Вырезать сцену\tCtrl+X");
    AppendMenuW(menu, sceneClipboard_ ? MF_STRING : MF_STRING | MF_GRAYED, Paste, L"Вставить сцену\tCtrl+V");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, selected > 0 ? MF_STRING : MF_STRING | MF_GRAYED, MoveUp, L"Переместить выше");
    AppendMenuW(menu, selected + 1 < static_cast<int>(document_.project().scenes.size()) ? MF_STRING : MF_STRING | MF_GRAYED,
                MoveDown, L"Переместить ниже");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, document_.project().scenes.size() > 1 ? MF_STRING : MF_STRING | MF_GRAYED, Delete, L"Удалить\tDelete");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == Rename) { SetFocus(title_); SendMessageW(title_, EM_SETSEL, 0, -1); }
    else if (command == Duplicate) duplicateSelectedScene();
    else if (command == Copy) copySelectedScene(false);
    else if (command == Cut) copySelectedScene(true);
    else if (command == Paste) pasteScene();
    else if (command == MoveUp) moveSelectedScene(-1);
    else if (command == MoveDown) moveSelectedScene(1);
    else if (command == Delete) SendMessageW(hwnd_, WM_COMMAND, MAKEWPARAM(IdDeleteScene, BN_CLICKED), reinterpret_cast<LPARAM>(deleteButton_));
}

void ScriptPage::duplicateSelectedScene() {
    saveCurrentEditorToScene();
    document_.duplicateScene(document_.selectedScene());
    if (editorMode_ == EditorMode::FullScript) scrollToSelectedScene();
}

void ScriptPage::moveSelectedScene(int delta) {
    saveCurrentEditorToScene();
    document_.moveScene(document_.selectedScene(), delta);
}

void ScriptPage::copySelectedScene(bool cut) {
    saveCurrentEditorToScene();
    const Scene* scene = document_.selectedScenePtr();
    if (!scene) return;
    sceneClipboard_ = *scene;
    if (cut && document_.project().scenes.size() > 1) {
        if (!settings_.get().confirmDeleteScene ||
            MessageBoxW(hwnd_, L"Вырезать выбранную сцену из проекта?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) == IDYES)
            document_.deleteSelectedScene();
    }
}

void ScriptPage::pasteScene() {
    if (!sceneClipboard_) return;
    saveCurrentEditorToScene();
    document_.checkpoint(L"Вставка сцены");
    Project& project = document_.editProject();
    Scene copy = *sceneClipboard_;
    int nextId = project.nextId();
    copy.id = nextId++;
    copy.title += L" — копия";
    copy.cardX += 36.0f;
    copy.cardY += 36.0f;
    copy.selected = false;
    for (auto& note : copy.notes) note.id = nextId++;
    for (auto& item : copy.breakdownItems) { item.id = nextId++; item.sceneId = copy.id; }
    for (auto& comment : copy.reviewComments) { comment.id = nextId++; comment.sceneId = copy.id; }
    const std::size_t insertAt = std::min(document_.selectedScene() + 1, project.scenes.size());
    project.scenes.insert(project.scenes.begin() + static_cast<std::ptrdiff_t>(insertAt), std::move(copy));
    project.normalizeOrder();
    document_.markChanged();
    document_.setSelectedScene(insertAt);
    if (editorMode_ == EditorMode::FullScript) scrollToSelectedScene();
}

void ScriptPage::saveCurrentEditorToScene() {
    KillTimer(hwnd_, SyncTimer);
    editorPending_ = false;
    if (loading_ || !editorDirty_) return;

    // Page gaps belong to the viewport, not to the screenplay RTF. Reset the
    // physical paragraph styles before capturing rich text, then rebuild the
    // visual page stack from the committed layout below.
    if (!pageBreaks_.empty()) {
        const bool previousLoading = loading_;
        loading_ = true;
        applyStoredFormatting();
        clearPageBreaks();
        loading_ = previousLoading;
    }

    if (editorMode_ == EditorMode::FullScript) {
        const std::wstring text = WindowText(editor_);
        // Reading paragraph formats temporarily changes the RichEdit
        // selection. Suppress caret-to-scene synchronization while doing so,
        // otherwise the selected scene would walk through the whole script.
        const bool previousLoading = loading_;
        loading_ = true;
        const std::wstring formatMap = captureFullScriptFormats();
        loading_ = previousLoading;
        std::wstring error;
        syncingEditorToDocument_ = true;
        const bool applied = document_.applyStructuredScreenplay(text, formatMap, &error);
        syncingEditorToDocument_ = false;
        if (!applied) {
            editorDirty_ = true;
            MessageBoxW(hwnd_, (error + L"\n\nТекст оставлен в редакторе и не был записан поверх сцен.").c_str(),
                        L"Mezozoy — защита сценария", MB_OK | MB_ICONWARNING);
            return;
        }
        editorDirty_ = false;
        populateScenes();
        loadProperties();
        const bool previousLoadingAfterApply = loading_;
        loading_ = true;
        applyStoredFormatting();
        rebuildSceneStarts();
        rebuildPageBreaks();
        loading_ = previousLoadingAfterApply;
        updateCounters();
        return;
    }

    Scene* scene = document_.selectedScenePtr();
    if (!scene) return;

    const std::wstring text = WindowText(editor_);
    const std::wstring rtf = ReadRtf(editor_);
    const std::wstring formats = captureFocusedSceneFormats();
    bool changed = scene->text != text || scene->rtf != rtf || scene->screenplayFormats != formats;
    scene->text = text;
    scene->rtf = rtf;
    scene->screenplayFormats = formats;

    const auto lines = utf::SplitLines(text);
    const std::wstring firstLine = lines.empty() ? std::wstring{} : utf::Trim(lines.front());
    if (!firstLine.empty() && scene->title != firstLine) {
        scene->title = firstLine;
        scene->titleAutoGenerated = false;
        changed = true;
    }
    editorDirty_ = false;
    if (changed) {
        syncingEditorToDocument_ = true;
        document_.markChanged();
        syncingEditorToDocument_ = false;
    }
    populateScenes();
    const HWND focus = GetFocus();
    if (focus != title_ && focus != summary_) loadProperties();
    const bool previousLoading = loading_;
    loading_ = true;
    applyStoredFormatting();
    rebuildPageBreaks();
    loading_ = previousLoading;
    updateCounters();
}

void ScriptPage::syncEditorNow() {
    saveCurrentEditorToScene();
}

void ScriptPage::syncTitle() {
    Scene* scene = document_.selectedScenePtr();
    if (!scene) return;
    const std::wstring title = utf::Trim(WindowText(title_));
    if (title == scene->title) return;
    checkpointEditor();

    const DWORD titleSelection = static_cast<DWORD>(SendMessageW(title_, EM_GETSEL, 0, 0));
    saveCurrentEditorToScene();

    // saveCurrentEditorToScene may have refreshed properties while another
    // edit was pending. Keep exactly what the user typed and preserve the
    // caret in the title field.
    loading_ = true;
    if (WindowText(title_) != title) SetWindowTextW(title_, title.c_str());
    SendMessageW(title_, EM_SETSEL, LOWORD(titleSelection), HIWORD(titleSelection));
    loading_ = false;

    syncingEditorToDocument_ = true;
    const bool renamed = document_.renameSelectedScene(title);
    syncingEditorToDocument_ = false;
    if (!renamed) return;

    loading_ = true;
    populateScenes();
    if (editorMode_ == EditorMode::CurrentScene) {
        ReplaceEditorFirstLine(editor_, title);
        if (Scene* renamedScene = document_.selectedScenePtr()) {
            renamedScene->text = WindowText(editor_);
            renamedScene->rtf = ReadRtf(editor_);
        }
        editorDirty_ = false;
    } else {
        const Scene* renamedScene = document_.selectedScenePtr();
        rebuildSceneStarts();
        const LONG start = renamedScene ? sceneStart(document_.selectedScene()) : -1;
        if (start < 0) {
            loadEditor();
        } else {
            CHARRANGE editorSelection{};
            SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&editorSelection));
            const LONG oldLength = LineLengthFromPosition(editor_, start);
            const LONG oldEnd = start + oldLength;
            const LONG newLength = static_cast<LONG>(title.size());
            const LONG delta = newLength - oldLength;
            const auto adjustPosition = [&](LONG position) {
                if (position <= start) return position;
                if (position >= oldEnd) return position + delta;
                return start + std::min(position - start, newLength);
            };

            SendMessageW(editor_, WM_SETREDRAW, FALSE, 0);
            CHARRANGE titleRange{start, oldEnd};
            SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&titleRange));
            SendMessageW(editor_, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(title.c_str()));
            applyParagraphStyle(start, start + newLength, ScriptLineType::SceneHeading);
            editorSelection.cpMin = adjustPosition(editorSelection.cpMin);
            editorSelection.cpMax = adjustPosition(editorSelection.cpMax);
            SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&editorSelection));
            SendMessageW(editor_, WM_SETREDRAW, TRUE, 0);
            InvalidateRect(editor_, nullptr, TRUE);
            rebuildSceneStarts();
            editorDirty_ = false;
            editorPending_ = false;
            KillTimer(hwnd_, SyncTimer);
        }
    }
    applyStoredFormatting();
    rebuildPageBreaks();
    loading_ = false;
    updateCounters();
}

void ScriptPage::updateCounters() {
    // Counters must not copy embedded reference images on each keystroke.
    refreshPagedView();
    const auto selected = document_.selectedScene();
    const Scene* scene = document_.selectedScenePtr();
    std::wstring sceneText = scene ? scene->text : L"";
    std::wstring sceneFormats = scene ? scene->screenplayFormats : L"";
    ScreenplayLayoutDocument projectLayout;
    if (editorMode_ == EditorMode::FullScript) {
        projectLayout = pagedView_.document();
        const auto start = sceneStart(selected);
        const auto next = sceneStart(selected + 1);
        if (start >= 0) {
            sceneText = EditorTextRange(editor_, start, next < 0 ? GetWindowTextLengthW(editor_) : next);
            const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
            sceneFormats.clear();
            for (std::size_t i = 0; i < paragraphs.size(); ++i)
                if (paragraphs[i].start >= start && (next < 0 || paragraphs[i].start < next))
                    sceneFormats += ScriptService::formatCode(typeAtLine(static_cast<int>(i)));
        }
    } else {
        projectLayout = ScreenplayLayoutService{}.layout(document_.project(), currentPaper(), false);
        sceneText = WindowText(editor_);
        sceneFormats = captureFocusedSceneFormats();
    }
    const auto sceneLayout = ScreenplayLayoutService{}.layoutText(sceneText, sceneFormats, currentPaper());
    const std::wstring sceneValue = L"Сцена: " + std::to_wstring(ScriptService::countWords(sceneText)) + L" слов";
    const std::wstring projectValue = L"Сценарий: " + std::to_wstring(projectLayout.scriptPageCount) + L" стр.";
    SetWindowTextW(wordCounter_, sceneValue.c_str());
    SetWindowTextW(projectCounter_, projectValue.c_str());
    const std::wstring runtime = L"Хрон.: " + FormatRuntime(sceneLayout.durationMinutes) + L" | " + FormatRuntime(projectLayout.durationMinutes);
    SetWindowTextW(chronometer_, runtime.c_str());
}

void ScriptPage::scrollToSelectedScene() {
    if (!document_.selectedScenePtr()) return;
    const LONG start = sceneStart(document_.selectedScene());
    if (start >= 0) {
        SendMessageW(editor_, EM_SETSEL, start, start);
        SendMessageW(editor_, EM_SCROLLCARET, 0, 0);
    }
}

void ScriptPage::setEditorMode(EditorMode mode) {
    if (editorMode_ == mode) return;
    ScopedRedrawLock surface(hwnd_, RDW_INVALIDATE | RDW_ALLCHILDREN);
    ScopedRedrawLock paper(editor_, RDW_INVALIDATE | RDW_FRAME);
    hideAutocomplete();
    saveCurrentEditorToScene();
    editorMode_ = mode;
    loading_ = true;
    loadEditor();
    loading_ = false;
    updateCounters();
    if (mode == EditorMode::FullScript) scrollToSelectedScene();
    InvalidateRect(currentSceneButton_,nullptr,FALSE);
    InvalidateRect(fullScriptButton_,nullptr,FALSE);
}

ScreenplayPaper ScriptPage::currentPaper() const {
    return ScreenplayLayoutService::paperFromSetting(settings_.get().screenplayPaper);
}

void ScriptPage::updatePaperButtons() {
    const ScreenplayPaper paper = currentPaper();
    SetWindowTextW(paperLetterButton_,L"US Letter");
    SetWindowTextW(paperA4Button_,L"A4");
    SetPropW(paperLetterButton_,style::Active,reinterpret_cast<HANDLE>(paper==ScreenplayPaper::HollywoodLetter?1:0));
    SetPropW(paperA4Button_,style::Active,reinterpret_cast<HANDLE>(paper==ScreenplayPaper::A4?1:0));
}

void ScriptPage::setPaper(ScreenplayPaper paper) {
    saveCurrentEditorToScene();
    settings_.edit().screenplayPaper = ScreenplayLayoutService::paperSetting(paper);
    settings_.save();
    updatePaperButtons();
    const bool previousLoading = loading_;
    loading_ = true;
    applyStoredFormatting();
    rebuildPageBreaks();
    loading_ = previousLoading;
    updateCounters();
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
    SetFocus(editor_);
}

void ScriptPage::fitScriptViewport() {
    saveCurrentEditorToScene();
    leftWidth_ = 280;
    rightWidth_ = 310;
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
    SetFocus(editor_);
}

void ScriptPage::clearPageBreaks() {
    for (const PageBreakOverlay& pageBreak : pageBreaks_) {
        if (IsWindow(pageBreak.window)) DestroyWindow(pageBreak.window);
    }
    pageBreaks_.clear();
}

void ScriptPage::rebuildPageBreaks() {
    clearPageBreaks();
    refreshPagedView();
}

void ScriptPage::positionPageBreaks() {
    pagedView_.selectionChanged();
}

void ScriptPage::refreshPagedView(bool reveal) {
    pagedView_.setContent(WindowText(editor_), captureFullScriptFormats(), currentPaper());
    pagedView_.selectionChanged(reveal);
}

ScriptPage::EditorState ScriptPage::editorState() {
    EditorState state;
    state.text = WindowText(editor_);
    state.formats = captureFullScriptFormats();
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&state.selection));
    state.scenes = document_.project().scenes;
    return state;
}

void ScriptPage::checkpointEditor(bool typing) {
    if (loading_ || historyRestoring_ || historyDepth_ > 0) return;
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    const auto now = GetTickCount64();
    const bool continues = typing && lastTyping_ && now - lastTyping_ < 900 &&
        selection.cpMin == selection.cpMax && selection.cpMax == lastTypingCaret_;
    if (!continues) {
        auto state = editorState();
        if (undo_.empty() || undo_.back().text != state.text || undo_.back().formats != state.formats)
            undo_.push_back(std::move(state));
        if (undo_.size() > 64) undo_.erase(undo_.begin());
    }
    redo_.clear();
    lastTyping_ = typing ? now : 0;
    lastTypingCaret_ = typing ? selection.cpMax + 1 : -1;
}

void ScriptPage::undoEditor(bool redo) {
    auto& source = redo ? redo_ : undo_;
    auto& target = redo ? undo_ : redo_;
    if (source.empty()) {
        if (redo) document_.redo(); else document_.undo();
        return;
    }
    target.push_back(editorState());
    EditorState state = std::move(source.back());
    source.pop_back();
    historyRestoring_ = true;
    loading_ = true;
    hideAutocomplete();
    SetWindowTextW(editor_, state.text.c_str());
    initializeParagraphTypes(state.formats);
    document_.editProject().scenes = std::move(state.scenes);
    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&state.selection));
    rebuildSceneStarts();
    editorDirty_ = true;
    loading_ = false;
    lastTyping_ = 0;
    syncEditorNow();
    syncingEditorToDocument_ = true;
    document_.markChanged();
    syncingEditorToDocument_ = false;
    refreshPagedView(true);
    historyRestoring_ = false;
}

bool ScriptPage::undoText(bool redo) {
    if ((redo ? redo_ : undo_).empty()) return false;
    undoEditor(redo);
    return true;
}

void ScriptPage::applyFormat(ScriptLineType type) {
    formatCurrentParagraph(type);
    currentFormat_ = type;
    SendMessageW(GetDlgItem(hwnd_, IdFormatChooser), CB_SETCURSEL, FormatChoice(currentFormat_), 0);
    SetWindowTextW(formatLabel_, (L"Формат: " + ScriptService::typeName(type)).c_str());
    updateAutocompleteAndDetection();
}

std::size_t ScriptPage::sceneIndexAtCaret() const {
    if (editorMode_ != EditorMode::FullScript) return document_.selectedScene();
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (sceneStarts_.empty()) return document_.selectedScene();
    const auto found = std::upper_bound(sceneStarts_.begin(), sceneStarts_.end(), selection.cpMax);
    return found == sceneStarts_.begin() ? 0
        : static_cast<std::size_t>(std::distance(sceneStarts_.begin(), found) - 1);
}

void ScriptPage::rebuildSceneStarts() {
    sceneStarts_.clear();
    if (!editor_ || editorMode_ != EditorMode::FullScript) return;
    reconcileParagraphTypes();
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        if (typeAtLine(static_cast<int>(index)) == ScriptLineType::SceneHeading &&
            !utf::Trim(paragraphs[index].text).empty())
            sceneStarts_.push_back(paragraphs[index].start);
    }
    if (sceneStarts_.empty() && !paragraphs.empty()) {
        const auto first = std::find_if(paragraphs.begin(), paragraphs.end(), [](const ParagraphRange& paragraph) {
            return !utf::Trim(paragraph.text).empty();
        });
        const std::size_t index = first == paragraphs.end()
            ? 0 : static_cast<std::size_t>(std::distance(paragraphs.begin(), first));
        setTypeAtLine(static_cast<int>(index), ScriptLineType::SceneHeading);
        sceneStarts_.push_back(paragraphs[index].start);
    }
}

LONG ScriptPage::sceneStart(std::size_t index) const {
    return index < sceneStarts_.size() ? sceneStarts_[index] : -1;
}

void ScriptPage::syncSceneSelectionFromCaret() {
    if (loading_ || editorMode_ != EditorMode::FullScript || document_.project().scenes.empty()) return;
    const std::size_t index = sceneIndexAtCaret();
    if (index >= document_.project().scenes.size() || index == document_.selectedScene()) return;
    syncingSceneSelectionFromEditor_ = true;
    document_.setSelectedScene(index,
                               settings_.get().keepScriptCardsSync && settings_.get().selectMatchingCard);
    syncingSceneSelectionFromEditor_ = false;
}

void ScriptPage::syncSceneTitleFromEditor() {
    if (loading_ || !currentParagraphIsSceneTitle()) return;
    const std::size_t index = sceneIndexAtCaret();
    if (index >= document_.project().scenes.size()) return;
    const std::wstring title = utf::Trim(currentLine().text);
    if (title.empty() || title == document_.project().scenes[index].title) return;

    // Scene text remains the source of truth, but its first line can update
    // the lightweight scene model immediately. This gives the navigator,
    // inspector and card board live synchronization without adding one undo
    // history entry per typed character.
    ScriptService service;
    Scene& scene = document_.editProject().scenes[index];
    if (!service.renameScene(document_.editProject(), index, title)) return;
    scene.rtf.clear();
    syncingEditorToDocument_ = true;
    document_.markChanged();
    syncingEditorToDocument_ = false;
}

void ScriptPage::normalizeCurrentSceneTitleAppearance() {
    if (!currentParagraphIsSceneTitle()) return;
    const LineContext line = currentLine();
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    const bool previousLoading = loading_;
    loading_ = true;
    applyParagraphStyle(line.start, line.end, ScriptLineType::SceneHeading);
    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    loading_ = previousLoading;
}

void ScriptPage::normalizeCurrentParagraphAppearance() {
    if (!editor_) return;
    const LineContext line = currentLine();
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));

    // RichEdit can inherit the system default (black) after Enter, paste or a
    // protected scene boundary. Re-assert only the visible paragraph's text
    // colour and protection flags. The hidden scene id stays untouched.
    const bool previousLoading = loading_;
    loading_ = true;
    CHARRANGE visibleRange{line.start, line.end};
    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&visibleRange));
    CHARFORMAT2W character{};
    character.cbSize = sizeof(character);
    character.dwMask = CFM_COLOR | CFM_HIDDEN | CFM_PROTECTED;
    character.dwEffects = 0;
    character.crTextColor = currentLineType() == ScriptLineType::Note ? theme_.accent : theme_.text;
    SendMessageW(editor_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&character));

    SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (selection.cpMin == selection.cpMax)
        SendMessageW(editor_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&character));
    loading_ = previousLoading;
}

bool ScriptPage::currentParagraphIsSceneTitle() const {
    const LineContext line = currentLine();
    if (editorMode_ == EditorMode::CurrentScene) return line.lineIndex == 0;
    return std::find(sceneStarts_.begin(), sceneStarts_.end(), line.start) != sceneStarts_.end();
}

void ScriptPage::selectSelectedSceneTitleInEditor() {
    const Scene* scene = document_.selectedScenePtr();
    if (!scene) return;
    LONG start = 0;
    if (editorMode_ == EditorMode::FullScript) {
        start = sceneStart(document_.selectedScene());
        if (start < 0) return;
    }
    const LONG end = start + static_cast<LONG>(scene->title.size());
    SetFocus(editor_);
    SendMessageW(editor_, EM_SETSEL, start, end);
    SendMessageW(editor_, EM_SCROLLCARET, 0, 0);
}

void ScriptPage::createSceneFromCurrentParagraph() {
    if (editorMode_ == EditorMode::CurrentScene) {
        saveCurrentEditorToScene();
        document_.addSceneAfter(document_.selectedScene());
        selectSelectedSceneTitleInEditor();
        return;
    }

    if (currentParagraphIsSceneTitle()) {
        applyFormat(ScriptLineType::SceneHeading);
        return;
    }

    saveCurrentEditorToScene();
    document_.checkpoint(L"Создание сцены");
    LineContext line = currentLine();
    if (utf::Trim(line.text).empty()) {
        SendMessageW(editor_, EM_SETSEL, line.start, line.end);
        SendMessageW(editor_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"НОВАЯ СЦЕНА"));
    }
    applyFormat(ScriptLineType::SceneHeading);
    saveCurrentEditorToScene();
    rebuildSceneStarts();
    const std::size_t index = std::min(sceneIndexAtCaret(), document_.project().scenes.size() - 1);
    syncingSceneSelectionFromEditor_ = true;
    document_.setSelectedScene(index, settings_.get().keepScriptCardsSync && settings_.get().selectMatchingCard);
    syncingSceneSelectionFromEditor_ = false;
    loading_ = true;
    populateScenes();
    loadProperties();
    loading_ = false;
}

ScriptPage::LineContext ScriptPage::currentLine() const {
    LineContext context;
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    context.caret = selection.cpMax;
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        if (context.caret <= paragraphs[index].end || index + 1 == paragraphs.size()) {
            context.lineIndex = static_cast<int>(index);
            context.start = paragraphs[index].start;
            context.end = paragraphs[index].end;
            context.text = paragraphs[index].text;
            break;
        }
    }
    return context;
}

ScriptLineType ScriptPage::currentLineType() const {
    return typeAtLine(currentLine().lineIndex);
}

void ScriptPage::cycleFormat(bool backwards) {
    static constexpr ScriptLineType order[] = {
        ScriptLineType::Action, ScriptLineType::Character, ScriptLineType::Dialogue,
        ScriptLineType::Parenthetical, ScriptLineType::Transition, ScriptLineType::SceneHeading
    };
    const ScriptLineType current = currentLineType();
    int index = 0;
    for (int i = 0; i < static_cast<int>(std::size(order)); ++i) if (order[i] == current) index = i;
    index = (index + (backwards ? -1 : 1) + static_cast<int>(std::size(order))) % static_cast<int>(std::size(order));
    applyFormat(order[index]);
}

void ScriptPage::formatCurrentParagraph(ScriptLineType type) {
    checkpointEditor();
    reconcileParagraphTypes();
    CHARRANGE previousSelection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&previousSelection));
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    if (previousSelection.cpMin != previousSelection.cpMax) {
        int affected = 0;
        for (std::size_t i = 0; i < paragraphs.size(); ++i) {
            if (paragraphs[i].end < previousSelection.cpMin || paragraphs[i].start >= previousSelection.cpMax) continue;
            ++affected;
        }
        if (affected > 1) {
            for (std::size_t i = 0; i < paragraphs.size(); ++i)
                if (paragraphs[i].end >= previousSelection.cpMin && paragraphs[i].start < previousSelection.cpMax)
                    paragraphTypes_[i] = type;
            editorDirty_ = editorPending_ = true;
            rebuildSceneStarts();
            refreshPagedView();
            SetTimer(hwnd_, SyncTimer, 400, nullptr);
            SetFocus(editor_);
            return;
        }
    }
    LineContext line = currentLine();
    std::wstring transformed = line.text;
    if (type == ScriptLineType::SceneHeading || type == ScriptLineType::Character || type == ScriptLineType::Transition || type == ScriptLineType::Shot)
        transformed = utf::ToUpper(transformed);
    if (type == ScriptLineType::Parenthetical) {
        transformed = utf::Trim(transformed);
        if (transformed.empty()) transformed = L"()";
        if (!transformed.empty() && transformed.front() != L'(') transformed.insert(transformed.begin(), L'(');
        if (!transformed.empty() && transformed.back() != L')') transformed.push_back(L')');
    }
    if (type == ScriptLineType::Transition) {
        transformed = utf::Trim(transformed);
        if (!transformed.empty() && transformed.back() != L':') transformed.push_back(L':');
    }

    pendingParagraphType_ = type;
    pendingParagraphLine_ = line.lineIndex;
    if (transformed != line.text) {
        SendMessageW(editor_, EM_SETSEL, line.start, line.end);
        SendMessageW(editor_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(transformed.c_str()));
        line.end = line.start + static_cast<LONG>(transformed.size());
    }
    reconcileParagraphTypes();
    pendingParagraphType_.reset();
    pendingParagraphLine_ = -1;
    setTypeAtLine(line.lineIndex, type);

    applyParagraphStyle(line.start, line.end, type);
    if (type == ScriptLineType::Parenthetical && transformed == L"()") {
        SendMessageW(editor_, EM_SETSEL, line.start + 1, line.start + 1);
    } else {
        if (type == ScriptLineType::Parenthetical && transformed != line.text && !line.text.starts_with(L"(")) {
            ++previousSelection.cpMin; ++previousSelection.cpMax;
        }
        previousSelection.cpMin = std::min(previousSelection.cpMin, line.end);
        previousSelection.cpMax = std::min(previousSelection.cpMax, line.end);
        SendMessageW(editor_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&previousSelection));
    }
    editorDirty_ = true;
    editorPending_ = true;
    SetTimer(hwnd_, SyncTimer, 400, nullptr);
    if (editorMode_ == EditorMode::FullScript && type == ScriptLineType::SceneHeading)
        rebuildSceneStarts();
    refreshPagedView(true);
    SetFocus(editor_);
}

void ScriptPage::applyParagraphStyle(LONG, LONG, ScriptLineType) {
    // Semantic styles belong to the document, never to RichEdit's undo queue.
}

void ScriptPage::initializeParagraphTypes(const std::wstring& formatMap) {
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    paragraphTypes_.clear();
    paragraphTexts_.clear();
    paragraphTypes_.reserve(paragraphs.size());
    paragraphTexts_.reserve(paragraphs.size());

    const bool storedMapMatches = formatMap.size() == paragraphs.size();
    ScriptService service;
    ScriptLineType previous = ScriptLineType::Empty;
    for (std::size_t index = 0; index < paragraphs.size(); ++index) {
        paragraphTexts_.push_back(paragraphs[index].text);
        ScriptLineType type = storedMapMatches
            ? ScriptService::typeFromFormatCode(formatMap[index]) : ScriptLineType::Empty;
        if (type == ScriptLineType::Empty && !utf::Trim(paragraphs[index].text).empty()) {
            type = index == 0 ? ScriptLineType::SceneHeading
                              : service.classify(paragraphs[index].text, previous);
        }
        if (index == 0 && !utf::Trim(paragraphs[index].text).empty())
            type = ScriptLineType::SceneHeading;
        paragraphTypes_.push_back(type);
        previous = type;
    }

    if (!paragraphTypes_.empty()) currentFormat_ = typeAtLine(currentLine().lineIndex);
}

void ScriptPage::reconcileParagraphTypes() {
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    std::vector<std::wstring> nextTexts;
    nextTexts.reserve(paragraphs.size());
    for (const ParagraphRange& paragraph : paragraphs) nextTexts.push_back(paragraph.text);

    if (paragraphTypes_.size() != paragraphTexts_.size() || paragraphTypes_.empty()) {
        initializeParagraphTypes({});
        return;
    }
    if (nextTexts == paragraphTexts_) return;

    std::size_t prefix = 0;
    while (prefix < paragraphTexts_.size() && prefix < nextTexts.size() &&
           paragraphTexts_[prefix] == nextTexts[prefix]) ++prefix;

    std::size_t suffix = 0;
    while (suffix < paragraphTexts_.size() - prefix && suffix < nextTexts.size() - prefix &&
           paragraphTexts_[paragraphTexts_.size() - suffix - 1] == nextTexts[nextTexts.size() - suffix - 1]) {
        ++suffix;
    }

    std::vector<ScriptLineType> nextTypes(nextTexts.size(), ScriptLineType::Empty);
    for (std::size_t index = 0; index < prefix; ++index) nextTypes[index] = paragraphTypes_[index];
    for (std::size_t offset = 0; offset < suffix; ++offset) {
        nextTypes[nextTypes.size() - offset - 1] = paragraphTypes_[paragraphTypes_.size() - offset - 1];
    }

    const std::size_t oldMiddle = paragraphTexts_.size() - prefix - suffix;
    const std::size_t nextMiddle = nextTexts.size() - prefix - suffix;
    ScriptService service;
    for (std::size_t offset = 0; offset < nextMiddle; ++offset) {
        const std::size_t index = prefix + offset;
        const ScriptLineType previous = index > 0 ? nextTypes[index - 1] : ScriptLineType::Empty;
        if (offset < oldMiddle) {
            nextTypes[index] = paragraphTypes_[prefix + offset];
            if (nextTypes[index] == ScriptLineType::Empty && !utf::Trim(nextTexts[index]).empty())
                nextTypes[index] = index == 0 ? ScriptLineType::SceneHeading
                                              : service.classify(nextTexts[index], previous);
        } else if (utf::Trim(nextTexts[index]).empty()) {
            nextTypes[index] = ScriptService::nextTypeAfterEnter(previous);
        } else {
            nextTypes[index] = index == 0 ? ScriptLineType::SceneHeading
                                          : service.classify(nextTexts[index], previous);
        }
    }

    if (pendingParagraphType_ && pendingParagraphLine_ >= 0 &&
        static_cast<std::size_t>(pendingParagraphLine_) < nextTypes.size()) {
        nextTypes[static_cast<std::size_t>(pendingParagraphLine_)] = *pendingParagraphType_;
    }

    paragraphTexts_ = std::move(nextTexts);
    paragraphTypes_ = std::move(nextTypes);
}

ScriptLineType ScriptPage::typeAtLine(int lineIndex) const {
    if (lineIndex >= 0 && static_cast<std::size_t>(lineIndex) < paragraphTypes_.size())
        return paragraphTypes_[static_cast<std::size_t>(lineIndex)];
    return ScriptLineType::Action;
}

void ScriptPage::setTypeAtLine(int lineIndex, ScriptLineType type) {
    reconcileParagraphTypes();
    if (lineIndex < 0 || static_cast<std::size_t>(lineIndex) >= paragraphTypes_.size()) return;
    paragraphTypes_[static_cast<std::size_t>(lineIndex)] = type;
}

void ScriptPage::applyStoredFormatting() {
    reconcileParagraphTypes();
    refreshPagedView();
}

std::wstring ScriptPage::captureFullScriptFormats() {
    reconcileParagraphTypes();
    std::wstring formats;
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    formats.reserve(paragraphs.size());
    for (std::size_t line = 0; line < paragraphs.size(); ++line)
        formats.push_back(ScriptService::formatCode(typeAtLine(static_cast<int>(line))));
    return formats;
}

std::wstring ScriptPage::captureFocusedSceneFormats() {
    reconcileParagraphTypes();
    const auto paragraphs = ParagraphsInRange(editor_, 0, GetWindowTextLengthW(editor_));
    std::wstring formats;
    formats.reserve(paragraphs.size());
    for (std::size_t line = 0; line < paragraphs.size(); ++line)
        formats.push_back(ScriptService::formatCode(typeAtLine(static_cast<int>(line))));
    return formats;
}

void ScriptPage::updateAutocompleteAndDetection() {
    if (loading_ || !settings_.get().autocomplete || GetFocus() != editor_) {
        hideAutocomplete();
        return;
    }
    const LineContext line = currentLine();
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    if (selection.cpMin != selection.cpMax || (dismissedCaret_ == line.caret && dismissedText_ == line.text)) {
        hideAutocomplete(); return;
    }
    dismissedCaret_ = -1;
    const auto cursorOffset = static_cast<std::size_t>(std::clamp<LONG>(line.caret - line.start, 0, line.end - line.start));
    const std::wstring trimmed = utf::Trim(line.text.substr(0, cursorOffset));
    const std::wstring upper = utf::ToUpper(trimmed);
    std::vector<std::wstring> values;
    LONG replaceStart = line.start;
    LONG replaceEnd = line.end;

    AutocompleteKind kind = AutocompleteKind::None;
    const ScriptLineType lineType = currentLineType();
    const bool headingParagraph = lineType == ScriptLineType::SceneHeading;
    const std::size_t firstVisible = line.text.find_first_not_of(L" \t");
    const std::size_t visibleStart = firstVisible == std::wstring::npos ? line.text.size() : firstVisible;

    if (headingParagraph) {
        const std::size_t prefixLength = SceneHeadingPrefixLength(trimmed);
        if (prefixLength == std::wstring::npos) {
            static const std::wstring headingPrefixes[] = {L"ИНТ. ", L"НАТ. ", L"ИНТ./НАТ. "};
            for (const std::wstring& candidate : headingPrefixes) {
                const std::wstring comparable = utf::Trim(candidate);
                if ((upper.empty() || comparable.starts_with(upper)) && comparable != upper)
                    values.push_back(candidate);
            }
            replaceStart = line.start + static_cast<LONG>(visibleStart);
            replaceEnd = line.end;
            kind = AutocompleteKind::HeadingPrefix;
        } else {
            std::size_t begin = visibleStart + prefixLength;
            while (begin < line.text.size() && std::iswspace(line.text[begin])) ++begin;
            std::size_t end = line.text.find(L'—', begin);
            if (end == std::wstring::npos) end = line.text.find(L'–', begin);
            if (end == std::wstring::npos) end = line.text.find(L" - ", begin);
            if (end == std::wstring::npos) end = line.text.size();
            if (cursorOffset < begin || cursorOffset > end) { hideAutocomplete(); return; }
            const std::wstring locationPrefix = utf::ToUpper(utf::Trim(line.text.substr(begin, cursorOffset - begin)));
            for (const auto& location : document_.project().locations) {
                const std::wstring candidate = utf::ToUpper(utf::Trim(location.name));
                if (!candidate.empty() && (locationPrefix.empty() || candidate.starts_with(locationPrefix)) &&
                    candidate != locationPrefix)
                    values.push_back(candidate);
            }
            replaceStart = line.start + static_cast<LONG>(begin);
            replaceEnd = line.start + static_cast<LONG>(end);
            kind = AutocompleteKind::Location;
        }
    } else if (lineType == ScriptLineType::Character) {
        bool hasLetter = false;
        for (const wchar_t character : trimmed) if (std::iswalpha(character)) hasLetter = true;
        if (hasLetter && trimmed.size() <= 48) {
            for (const auto& character : document_.project().characters) {
                const std::wstring candidate = utf::ToUpper(utf::Trim(character.name));
                if (candidate.starts_with(upper) && candidate != upper) values.push_back(candidate);
            }
            for (std::size_t i = 0; i < paragraphTexts_.size(); ++i) {
                if (typeAtLine(static_cast<int>(i)) != ScriptLineType::Character || static_cast<int>(i) == line.lineIndex) continue;
                const auto candidate = utf::ToUpper(utf::Trim(paragraphTexts_[i]));
                if (!candidate.empty() && candidate.starts_with(upper) && candidate != upper &&
                    std::find(values.begin(), values.end(), candidate) == values.end()) values.push_back(candidate);
            }
            const std::size_t begin = line.text.find_first_not_of(L" \t");
            const std::size_t end = line.text.find_last_not_of(L" \t");
            replaceStart = line.start + static_cast<LONG>(begin == std::wstring::npos ? 0 : begin);
            replaceEnd = line.start + static_cast<LONG>(end == std::wstring::npos ? line.text.size() : end + 1);
            kind = AutocompleteKind::Character;
        }
    }

    static const std::wstring transitions[] = {
        L"CUT TO:", L"FADE TO:", L"SMASH CUT TO:", L"MATCH CUT TO:", L"ПЕРЕХОД:", L"ЗАТЕМНЕНИЕ:"
    };
    if (values.empty() && lineType == ScriptLineType::Transition && !upper.empty()) {
        for (const auto& candidate : transitions) if (candidate.starts_with(upper) && candidate != upper) values.push_back(candidate);
        if (!values.empty()) kind = AutocompleteKind::Transition;
    }

    if (values.empty()) hideAutocomplete(); else showAutocomplete(values, replaceStart, replaceEnd, kind);
}

void ScriptPage::showAutocomplete(const std::vector<std::wstring>& values, LONG replaceStart, LONG replaceEnd,
                                  AutocompleteKind kind) {
    const bool unchanged = values == suggestions_ && kind == autocompleteKind_;
    const int previous = unchanged ? static_cast<int>(SendMessageW(autocompleteList_, LB_GETCURSEL, 0, 0)) : 0;
    suggestions_ = values;
    autocompleteKind_ = kind;
    suggestionStart_ = replaceStart;
    suggestionEnd_ = replaceEnd;
    SendMessageW(autocompleteList_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(autocompleteList_, LB_RESETCONTENT, 0, 0);
    for (const auto& value : suggestions_) SendMessageW(autocompleteList_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(value.c_str()));
    SendMessageW(autocompleteList_, LB_SETCURSEL, std::max(0, previous), 0);
    CHARRANGE selection{};
    SendMessageW(editor_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    POINTL point{};
    SendMessageW(editor_, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&point), selection.cpMax);
    POINT mapped{point.x, point.y};
    MapWindowPoints(editor_, hwnd_, &mapped, 1);
    const int rows = std::min(6, static_cast<int>(suggestions_.size()));
    const int itemHeight = std::max(24, static_cast<int>(std::lround(28.0f * uiScale_)));
    RECT editorBounds{};
    GetWindowRect(editor_, &editorBounds);
    MapWindowPoints(HWND_DESKTOP, hwnd_, reinterpret_cast<POINT*>(&editorBounds), 2);
    const int editorLeft = static_cast<int>(editorBounds.left);
    const int editorTop = static_cast<int>(editorBounds.top);
    const int editorRight = static_cast<int>(editorBounds.right);
    const int editorBottom = static_cast<int>(editorBounds.bottom);
    const int availableWidth = std::max(1, editorRight - editorLeft - 12);
    const int popupWidth = std::min(360, availableWidth);
    const int popupHeight = std::min(std::max(1, rows) * itemHeight,
                                     std::max(itemHeight, editorBottom - editorTop - 12));
    const int minimumX = editorLeft + 6;
    const int maximumX = std::max(minimumX, editorRight - popupWidth - 6);
    const int popupX = std::clamp(static_cast<int>(mapped.x), minimumX, maximumX);
    const int caretY = static_cast<int>(mapped.y);
    const int belowY = caretY + itemHeight;
    const int popupY = belowY + popupHeight <= editorBottom - 6
        ? belowY
        : std::max(editorTop + 6, caretY - popupHeight - 4);
    SetWindowPos(autocompleteList_, HWND_TOP, popupX, popupY, popupWidth, popupHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowScrollBar(autocompleteList_, SB_VERT, suggestions_.size() > 6);
    SendMessageW(autocompleteList_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(autocompleteList_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
}

void ScriptPage::hideAutocomplete() {
    suggestions_.clear();
    autocompleteKind_ = AutocompleteKind::None;
    SendMessageW(autocompleteList_, LB_RESETCONTENT, 0, 0);
    ShowWindow(autocompleteList_, SW_HIDE);
}

bool ScriptPage::acceptAutocomplete() {
    if (!IsWindowVisible(autocompleteList_)) return false;
    const int count = static_cast<int>(SendMessageW(autocompleteList_, LB_GETCOUNT, 0, 0));
    if (count <= 0) return false;
    int index = static_cast<int>(SendMessageW(autocompleteList_, LB_GETCURSEL, 0, 0));
    if (index < 0 || index >= count) index = 0;
    const int length = static_cast<int>(SendMessageW(autocompleteList_, LB_GETTEXTLEN, index, 0));
    if (length < 0) return false;
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    SendMessageW(autocompleteList_, LB_GETTEXT, index, reinterpret_cast<LPARAM>(value.data()));
    value.resize(static_cast<std::size_t>(length));
    if (value.empty()) return false;
    hideAutocomplete();
    SendMessageW(editor_, EM_SETSEL, suggestionStart_, suggestionEnd_);
    SendMessageW(editor_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value.c_str()));
    SetFocus(editor_);
    // Rebuild after focus is restored. Live scene-title synchronization may
    // briefly move focus while the replacement is being processed, which
    // would otherwise close the second autocomplete stage (prefix -> place).
    updateAutocompleteAndDetection();
    PostMessageW(hwnd_, WM_APP + 83, 0, 0);
    return true;
}

void ScriptPage::moveAutocompleteSelection(int delta) {
    if (!IsWindowVisible(autocompleteList_)) return;
    const int count = static_cast<int>(SendMessageW(autocompleteList_, LB_GETCOUNT, 0, 0));
    if (count <= 0) return;
    int index = static_cast<int>(SendMessageW(autocompleteList_, LB_GETCURSEL, 0, 0));
    index = (index + delta + count) % count;
    SendMessageW(autocompleteList_, LB_SETCURSEL, index, 0);
}

void ScriptPage::detectCompletedLine(const LineContext& line) {
    const std::wstring value = utf::Trim(line.text);
    if (value.empty()) return;
    if (IsSceneHeading(value)) {
        const std::wstring location = ExtractLocationName(value);
        if (location.empty()) return;
        const std::wstring key = utf::ToUpper(location);
        const bool exists = std::any_of(document_.project().locations.begin(), document_.project().locations.end(), [&](const Location& item) {
            return utf::ToUpper(utf::Trim(item.name)) == key;
        });
        if (!exists) showDetection(DetectionKind::Location, location);
        return;
    }
    if (line.lineIndex == 0) return;
    ScriptService service;
    if (service.classify(value) != ScriptLineType::Character) return;
    const std::wstring key = utf::ToUpper(value);
    const bool exists = std::any_of(document_.project().characters.begin(), document_.project().characters.end(), [&](const Character& item) {
        return utf::ToUpper(utf::Trim(item.name)) == key;
    });
    if (!exists) showDetection(DetectionKind::Character, value);
}

void ScriptPage::showDetection(DetectionKind kind, const std::wstring& name) {
    const std::wstring key = (kind == DetectionKind::Character ? L"C:" : L"L:") + utf::ToUpper(utf::Trim(name));
    if (ignoredDetections_.contains(key)) return;
    detectionKind_ = kind;
    detectedName_ = utf::Trim(name);
    const std::wstring label = (kind == DetectionKind::Character ? L"Найден новый персонаж: " : L"Найдена новая локация: ") + DisplayName(detectedName_);
    SetWindowTextW(detectionLabel_, label.c_str());
    ShowWindow(detectionLabel_, SW_SHOW);
    ShowWindow(addDetected_, SW_SHOW);
    ShowWindow(ignoreDetected_, SW_SHOW);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

void ScriptPage::hideDetection(bool ignore) {
    if (ignore && detectionKind_ != DetectionKind::None) {
        ignoredDetections_.insert((detectionKind_ == DetectionKind::Character ? L"C:" : L"L:") + utf::ToUpper(utf::Trim(detectedName_)));
    }
    detectionKind_ = DetectionKind::None;
    detectedName_.clear();
    ShowWindow(detectionLabel_, SW_HIDE);
    ShowWindow(addDetected_, SW_HIDE);
    ShowWindow(ignoreDetected_, SW_HIDE);
    RECT rect{};
    GetClientRect(hwnd_, &rect);
    onLayout(rect.right, rect.bottom);
}

void ScriptPage::addDetectedItem() {
    if (detectionKind_ == DetectionKind::Character) {
        Character character;
        character.id = document_.project().nextId();
        character.name = DisplayName(detectedName_);
        document_.editProject().characters.push_back(std::move(character));
        document_.markChanged();
    } else if (detectionKind_ == DetectionKind::Location) {
        Location location;
        location.id = document_.project().nextId();
        location.name = DisplayName(detectedName_);
        document_.editProject().locations.push_back(std::move(location));
        document_.markChanged();
    }
    hideDetection(false);
    updateAutocompleteAndDetection();
}

std::wstring ScriptPage::sceneListText(const Scene& scene) const {
    const std::wstring title = utf::Trim(scene.title).empty() ? L"Без названия" : scene.title;
    std::wostringstream value;
    value << std::setfill(L'0') << std::setw(2) << scene.orderIndex + 1 << L". " << title << L"    ("
          << FormatRuntime(ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats, currentPaper())) << L")";
    return value.str();
}

void ScriptPage::focusEditor() { SetFocus(editor_); }

LRESULT CALLBACK ScriptPage::EditorSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                                UINT_PTR, DWORD_PTR reference) {
    auto* self = reinterpret_cast<ScriptPage*>(reference);
    if (!self) return DefSubclassProc(window, message, wParam, lParam);

    if (message == WM_MOUSEWHEEL || message == WM_VSCROLL || message == WM_HSCROLL)
        self->hideAutocomplete();
    if (const auto handled = self->pagedView_.message(message, wParam, lParam)) return *handled;
    if (message == WM_SIZE || message == WM_SETFONT || message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (message == WM_SETFOCUS) HideCaret(window);
        self->pagedView_.resize();
        self->pagedView_.selectionChanged();
        return result;
    }
    if (message == WM_UNDO || message == EM_UNDO || message == EM_REDO) {
        self->undoEditor(message == EM_REDO);
        return 1;
    }
    if (message == EM_CANUNDO) return self->undo_.empty() ? 0 : 1;
    if (message == EM_CANREDO) return self->redo_.empty() ? 0 : 1;
    const bool mutation = message == EM_REPLACESEL || message == WM_PASTE || message == WM_CUT ||
        message == WM_CLEAR || (message == WM_CHAR && (wParam >= 32 || (wParam == 13 && self->pendingSmartEnter_))) ||
        (message == WM_KEYDOWN && (wParam == VK_DELETE || wParam == VK_BACK || wParam == VK_RETURN));
    struct HistoryTransaction {
        int* depth{};
        ~HistoryTransaction() { if (depth) --*depth; }
    } transaction;
    if (mutation && !self->loading_) {
        // Return was checkpointed before WM_KEYDOWN, including accepting a
        // suggestion. Its WM_CHAR completes that same undoable operation.
        if (!(message == WM_CHAR && wParam == 13 && self->pendingSmartEnter_))
            self->checkpointEditor(message == WM_CHAR && wParam >= 32);
        ++self->historyDepth_;
        transaction.depth = &self->historyDepth_;
    }

    if (message == WM_VSCROLL || message == WM_MOUSEWHEEL || message == EM_SCROLLCARET) {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        self->positionPageBreaks();
        return result;
    }

    if (message == EM_REPLACESEL) {
        const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        // EN_CHANGE may be delivered before RichEdit collapses the replaced
        // selection to its final caret position. Re-evaluate the scene and
        // title once the mutation is complete; this also covers paste and
        // automation without depending on notification timing.
        if (!self->loading_) {
            self->reconcileParagraphTypes();
            if (self->editorMode_ == EditorMode::FullScript) self->rebuildSceneStarts();
            self->editorDirty_ = true;
            self->editorPending_ = true;
            SetTimer(self->hwnd_, SyncTimer, 400, nullptr);
            if (self->editorMode_ == EditorMode::FullScript) self->syncSceneSelectionFromCaret();
            self->normalizeCurrentParagraphAppearance();
            if (self->currentParagraphIsSceneTitle()) {
                self->syncSceneTitleFromEditor();
                self->normalizeCurrentSceneTitleAppearance();
            }
            self->refreshPagedView(true);
            self->updateAutocompleteAndDetection();
        }
        return result;
    }

    if (message == WM_KEYDOWN) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl && (wParam == 'Z' || wParam == 'Y')) {
            self->undoEditor(wParam == 'Y' || shift);
            return 0;
        }
        if (ctrl && wParam >= '1' && wParam <= '6') {
            static constexpr ScriptLineType formats[] = {
                ScriptLineType::SceneHeading, ScriptLineType::Action, ScriptLineType::Character,
                ScriptLineType::Parenthetical, ScriptLineType::Dialogue, ScriptLineType::Transition
            };
            self->applyFormat(formats[wParam - '1']);
            return 0;
        }
        if (IsWindowVisible(self->autocompleteList_)) {
            if (wParam == VK_UP) { self->moveAutocompleteSelection(-1); return 0; }
            if (wParam == VK_DOWN) { self->moveAutocompleteSelection(1); return 0; }
            if (wParam == VK_RETURN) {
                const AutocompleteKind acceptedKind = self->autocompleteKind_;
                const bool accepted = self->acceptAutocomplete();
                const bool completesParagraph = acceptedKind == AutocompleteKind::Character ||
                                                acceptedKind == AutocompleteKind::Location ||
                                                acceptedKind == AutocompleteKind::Transition;
                if (accepted && completesParagraph && self->settings_.get().smartEnter && !shift) {
                    // Enter accepts the suggested character name and performs
                    // the screenplay transition in one action. Requiring a
                    // second Enter here makes autocomplete fight Smart Enter.
                    self->lineBeforeEnter_ = self->currentLine();
                    self->smartEnterSource_ = self->currentLineType();
                    self->pendingSmartEnter_ = true;
                } else {
                    self->suppressReturnCharacter_ = true;
                }
                return 0;
            }
            if (wParam == VK_TAB) {
                self->acceptAutocomplete();
                self->suppressTabCharacter_ = true;
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                const auto line = self->currentLine();
                self->dismissedCaret_ = line.caret;
                self->dismissedText_ = line.text;
                self->hideAutocomplete(); return 0;
            }
        }
        if (self->pagedView_.navigate(wParam, shift, ctrl)) return 0;
        if (wParam == VK_TAB) {
            if (shift) self->cycleFormat(true);
            else self->applyFormat(ScriptService::nextTypeAfterTab(self->currentLineType()));
            self->suppressTabCharacter_ = true;
            return 0;
        }
        if (wParam == VK_RETURN && self->settings_.get().smartEnter && !shift) {
            self->lineBeforeEnter_ = self->currentLine();
            self->smartEnterSource_ = self->currentLineType();
            self->pendingSmartEnter_ = true;
            // RichEdit inserts Return on WM_KEYDOWN, not just WM_CHAR. Leave
            // it to the semantic transaction below, otherwise it is inserted twice.
            return 0;
        }
        if (wParam == VK_ESCAPE) self->hideAutocomplete();
    }

    if (message == WM_CHAR) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wParam == 26 || wParam == 25) return 0;
        if (ctrl && wParam >= '1' && wParam <= '6') return 0;
        if (wParam == L'\t' && self->suppressTabCharacter_) { self->suppressTabCharacter_ = false; return 0; }
        if (wParam == L'\r' && self->suppressReturnCharacter_) { self->suppressReturnCharacter_ = false; return 0; }
        if (wParam == L'\r' && self->pendingSmartEnter_) {
            ScriptLineType nextType = ScriptService::nextTypeAfterEnter(self->smartEnterSource_);
            const auto before = self->lineBeforeEnter_;
            const bool closingParenthetical = self->smartEnterSource_ == ScriptLineType::Parenthetical &&
                before.caret == before.end - 1 && before.text.ends_with(L")");
            if (before.caret == before.start && !before.text.empty())
                nextType = self->smartEnterSource_;
            self->pendingParagraphType_ = nextType;
            self->pendingParagraphLine_ = self->lineBeforeEnter_.lineIndex + 1;
            const bool previousLoading = self->loading_;
            self->loading_ = true;
            if (closingParenthetical) {
                CHARRANGE replace{before.end, before.end};
                SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&replace));
            }
            const LRESULT result = SendMessageW(window, EM_REPLACESEL, TRUE,
                reinterpret_cast<LPARAM>(L"\r"));
            self->loading_ = previousLoading;
            self->pendingSmartEnter_ = false;
            self->reconcileParagraphTypes();
            self->pendingParagraphType_.reset();
            self->pendingParagraphLine_ = -1;
            self->applyFormat(nextType);
            self->detectCompletedLine(self->lineBeforeEnter_);
            return result;
        }
    }
    const LRESULT result = DefSubclassProc(window, message, wParam, lParam);
    if (mutation && !self->loading_) self->refreshPagedView(true);
    return result;
}

LRESULT CALLBACK ScriptPage::SceneListSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                                    UINT_PTR, DWORD_PTR reference) {
    auto* self = reinterpret_cast<ScriptPage*>(reference);
    if (!self) return DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_KEYDOWN) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (wParam == VK_F2) {
            SetFocus(self->title_);
            SendMessageW(self->title_, EM_SETSEL, 0, -1);
            return 0;
        }
        if (wParam == VK_DELETE) {
            SendMessageW(self->hwnd_, WM_COMMAND, MAKEWPARAM(IdDeleteScene, BN_CLICKED), reinterpret_cast<LPARAM>(self->deleteButton_));
            return 0;
        }
        if (wParam == VK_RETURN) { self->focusEditor(); return 0; }
        if (ctrl && wParam == 'C') { self->copySelectedScene(false); return 0; }
        if (ctrl && wParam == 'X') { self->copySelectedScene(true); return 0; }
        if (ctrl && wParam == 'V') { self->pasteScene(); return 0; }
        if (ctrl && wParam == 'D') { self->duplicateSelectedScene(); return 0; }
    }
    if (message == WM_CONTEXTMENU) {
        self->showSceneContextMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
        return 0;
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK ScriptPage::AutocompleteListSubclassProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                                           UINT_PTR subclassId, DWORD_PTR reference) {
    auto* self = reinterpret_cast<ScriptPage*>(reference);
    if (!self) return DefSubclassProc(window, message, wParam, lParam);
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_LBUTTONDOWN) {
        const DWORD hit = static_cast<DWORD>(SendMessageW(window, LB_ITEMFROMPOINT, 0, lParam));
        if (!HIWORD(hit)) SendMessageW(window, LB_SETCURSEL, LOWORD(hit), 0);
        SetCapture(window);
        return 0;
    }
    if (message == WM_LBUTTONUP) {
        const DWORD hit = static_cast<DWORD>(SendMessageW(window, LB_ITEMFROMPOINT, 0, lParam));
        const int index = static_cast<int>(LOWORD(hit));
        const bool outside = HIWORD(hit) != 0;
        if (GetCapture() == window) ReleaseCapture();
        if (!outside && index >= 0 && index < static_cast<int>(self->suggestions_.size()) &&
            IsWindowVisible(window)) {
            SendMessageW(window, LB_SETCURSEL, index, 0);
            self->acceptAutocomplete();
        }
        return 0;
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, AutocompleteListSubclassProc, subclassId);
    return DefSubclassProc(window, message, wParam, lParam);
}

}  // namespace mezozoy::ui
