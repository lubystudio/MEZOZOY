#include "HomePage.h"

#include "Win32Util.h"
#include "UiStyle.h"
#include "../core/Utf.h"
#include "../services/PosterService.h"
#include "../services/ProjectSerializer.h"
#include "../services/ScriptService.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>

namespace mezozoy::ui {
namespace {

enum PosterMenuCommand { ChangePoster = 7601, RemovePoster, OpenProject };

Gdiplus::Color Color(COLORREF value, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(value), GetGValue(value), GetBValue(value));
}

void AddRoundedPath(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rectangle, float radius) {
    const float diameter = radius * 2.0f;
    path.AddArc(rectangle.X, rectangle.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rectangle.GetRight() - diameter, rectangle.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rectangle.GetRight() - diameter, rectangle.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rectangle.X, rectangle.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

bool Contains(const RECT& rectangle, POINT point) {
    return point.x >= rectangle.left && point.x < rectangle.right && point.y >= rectangle.top && point.y < rectangle.bottom;
}

std::wstring ModifiedLabel(const std::filesystem::file_time_type& value) {
    const auto systemValue = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        value - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t fileTime = std::chrono::system_clock::to_time_t(systemValue);
    const std::time_t nowTime = std::time(nullptr);
    std::tm fileLocal{};
    std::tm nowLocal{};
    localtime_s(&fileLocal, &fileTime);
    localtime_s(&nowLocal, &nowTime);
    std::wostringstream output;
    if (fileLocal.tm_year == nowLocal.tm_year && fileLocal.tm_yday == nowLocal.tm_yday) {
        output << L"сегодня в " << std::put_time(&fileLocal, L"%H:%M");
    } else {
        output << std::put_time(&fileLocal, L"%d.%m.%Y в %H:%M");
    }
    return output.str();
}

std::wstring NormalizedPathKey(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error).lexically_normal().wstring();
    return utf::ToLower(absolute);
}

bool IsBackupPath(const std::filesystem::path& path) {
    for (const auto& part : path) if (_wcsicmp(part.c_str(), L"Mezozoy Backups") == 0) return true;
    const std::wstring name = utf::ToLower(path.filename().wstring());
    return name.ends_with(L".autosave.mzoy");
}

}  // namespace

HomePage::HomePage(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme)
    : NativePage(instance, parent, theme), document_(document), settings_(settings) {
    Gdiplus::GdiplusStartupInput startup;
    Gdiplus::GdiplusStartup(&gdiplusToken_, &startup, nullptr);

    quote_ = CreateChild(L"STATIC", L"«Пробуйте! Творите! Воплощайте! Ведь сила не в том, что способен разрушить, а в том, что способен создать!»",
                         SS_LEFT, 0, hwnd_, instance_);
    quoteAuthor_ = CreateChild(L"STATIC", L"МИХАИЛ ЛУБЯНОЙ", SS_LEFT, 0, hwnd_, instance_);
    createButton_ = CreateChild(L"BUTTON", L"+   СОЗДАТЬ ИСТОРИЮ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdCreate, hwnd_, instance_);
    openButton_ = CreateChild(L"BUTTON", L"▭   ОТКРЫТЬ ИСТОРИЮ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdOpen, hwnd_, instance_);
    instructionButton_ = CreateChild(L"BUTTON", L"?   ИНСТРУКЦИЯ", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdInstruction, hwnd_, instance_);
    quickCreate_ = CreateChild(L"BUTTON", L"+", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdQuickCreate, hwnd_, instance_);
    quickOpen_ = CreateChild(L"BUTTON", L"▭", BS_PUSHBUTTON | BS_FLAT | WS_TABSTOP, IdQuickOpen, hwnd_, instance_);
    openScript_ = CreateChild(L"BUTTON", L"Сценарий", BS_OWNERDRAW | WS_TABSTOP, IdOpenScript, hwnd_, instance_);
    openCards_ = CreateChild(L"BUTTON", L"Доска сцен", BS_OWNERDRAW | WS_TABSTOP, IdOpenCards, hwnd_, instance_);
    newScene_ = CreateChild(L"BUTTON", L"Новая сцена", BS_OWNERDRAW | WS_TABSTOP, IdNewScene, hwnd_, instance_);
    openDevelopment_ = CreateChild(L"BUTTON", L"Разработка", BS_OWNERDRAW | WS_TABSTOP, IdOpenDevelopment, hwnd_, instance_);

    remember(quote_, true);
    remember(quoteAuthor_);
    remember(createButton_, true);
    remember(openButton_, true);
    remember(instructionButton_, true);
    remember(quickCreate_, true);
    remember(quickOpen_, true);
    remember(openScript_, true);
    remember(openCards_, true);
    remember(newScene_, true);
    remember(openDevelopment_, true);
    document_.subscribe([this](DocumentChange change) {
        if (IsWindow(hwnd_) && (change == DocumentChange::Replaced || change == DocumentChange::Saved)) refreshRecent();
    });
    refreshRecent();
}

void HomePage::setProjectActive(bool active) {
    if (projectActive_ == active) return;
    projectActive_ = active;
    scrollOffset_ = 0;
    currentPoster_ = projectActive_ ? loadPoster(document_.project()) : nullptr;
    RECT client{};
    GetClientRect(hwnd_, &client);
    onLayout(client.right, client.bottom);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

HomePage::~HomePage() {
    releaseBackBuffer();
    currentPoster_.reset();
    recentProjects_.clear();
    if (gdiplusToken_) Gdiplus::GdiplusShutdown(gdiplusToken_);
}

void HomePage::applyAppearance(const Theme& theme, float uiScale) {
    NativePage::applyAppearance(theme, uiScale);
    InvalidateRect(hwnd_, nullptr, TRUE);
}

HBRUSH HomePage::controlColor(HDC dc, HWND control, UINT message) {
    if (control == quote_ || control == quoteAuthor_) {
        SetTextColor(dc, control == quoteAuthor_ ? theme_.textMuted : theme_.text);
        SetBkColor(dc, theme_.panel);
        SetBkMode(dc, TRANSPARENT);
        return panelBrush_;
    }
    return NativePage::controlColor(dc, control, message);
}

bool HomePage::drawCustomItem(DRAWITEMSTRUCT* draw) {
    if (draw && draw->CtlType == ODT_BUTTON && draw->CtlID >= IdCreate && draw->CtlID <= IdInstruction) {
        style::Button(draw, theme_, theme_.panel);
        return true;
    }
    if (!draw || draw->CtlType != ODT_BUTTON || draw->CtlID < IdOpenScript || draw->CtlID > IdOpenDevelopment) return false;
    style::Fill(draw->hDC, draw->rcItem, theme_.background);
    const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
    const bool focused = (draw->itemState & ODS_FOCUS) != 0;
    HBRUSH brush = CreateSolidBrush(pressed ? theme_.selection : theme_.panelAlt);
    HPEN pen = CreatePen(PS_SOLID, focused ? 2 : 1, focused ? theme_.accent : theme_.border);
    const HGDIOBJ oldBrush = SelectObject(draw->hDC, brush);
    const HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
    RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom, 8, 8);
    SelectObject(draw->hDC, oldPen);
    SelectObject(draw->hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    const wchar_t* title = L"";
    const wchar_t* note = L"";
    const wchar_t* icon = L"";
    switch (draw->CtlID) {
        case IdOpenScript: title = L"Сценарий"; note = L"Продолжить работу"; icon = L"▤"; break;
        case IdOpenCards: title = L"Доска сцен"; note = L"Планировать историю"; icon = L"▦"; break;
        case IdNewScene: title = L"Новая сцена"; note = L"Добавить в сценарий"; icon = L"+"; break;
        case IdOpenDevelopment: title = L"Разработка"; note = L"Герои и мир"; icon = L"◇"; break;
    }
    SetBkMode(draw->hDC, TRANSPARENT);
    RECT iconRect = draw->rcItem;
    iconRect.left += 14; iconRect.top += 13; iconRect.right = iconRect.left + 30; iconRect.bottom = iconRect.top + 30;
    HBRUSH iconBrush = CreateSolidBrush(theme_.selection);
    FillRect(draw->hDC, &iconRect, iconBrush);
    DeleteObject(iconBrush);
    SelectObject(draw->hDC, uiFontBold_);
    SetTextColor(draw->hDC, theme_.accent);
    DrawTextW(draw->hDC, icon, -1, &iconRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RECT titleRect = draw->rcItem;
    titleRect.left += 14; titleRect.right -= 10; titleRect.top += 52; titleRect.bottom = titleRect.top + 24;
    SetTextColor(draw->hDC, theme_.text);
    DrawTextW(draw->hDC, title, -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    RECT noteRect = draw->rcItem;
    noteRect.left += 14; noteRect.right -= 10; noteRect.top += 78; noteRect.bottom -= 8;
    SelectObject(draw->hDC, uiFont_);
    SetTextColor(draw->hDC, theme_.textMuted);
    DrawTextW(draw->hDC, note, -1, &noteRect, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_END_ELLIPSIS);
    return true;
}

void HomePage::onLayout(int width, int height) {
    width_ = width;
    height_ = height;
    if (projectActive_) {
        for (HWND control : {quote_, quoteAuthor_, createButton_, openButton_, instructionButton_}) ShowWindow(control, SW_HIDE);
        for (HWND control : {quickCreate_, quickOpen_, openScript_, openCards_, newScene_, openDevelopment_}) ShowWindow(control, SW_SHOW);
        layoutDashboard();
        InvalidateRect(hwnd_, nullptr, TRUE);
        return;
    }
    for (HWND control : {quote_, quoteAuthor_, createButton_, openButton_, instructionButton_, quickCreate_, quickOpen_}) ShowWindow(control, SW_SHOW);
    for (HWND control : {openScript_, openCards_, newScene_, openDevelopment_}) ShowWindow(control, SW_HIDE);
    SetWindowTextW(quickCreate_, L"+");
    SetWindowTextW(quickOpen_, L"▭");
    leftWidth_ = std::clamp(static_cast<int>(410.0f * uiScale_), 340, std::max(340, width - 560));
    const int padding = std::max(24, static_cast<int>(32.0f * uiScale_));
    SetWindowPos(quote_, nullptr, padding, 74, leftWidth_ - padding * 2, 112, SWP_NOZORDER);
    SetWindowPos(quoteAuthor_, nullptr, padding, 196, leftWidth_ - padding * 2, 22, SWP_NOZORDER);
    SetWindowPos(createButton_, nullptr, padding, 264, leftWidth_ - padding * 2, 44, SWP_NOZORDER);
    SetWindowPos(openButton_, nullptr, padding, 318, leftWidth_ - padding * 2, 44, SWP_NOZORDER);
    SetWindowPos(instructionButton_, nullptr, padding, 372, leftWidth_ - padding * 2, 44, SWP_NOZORDER);
    const int toolsX = leftWidth_ + std::max(24, (width - leftWidth_ - 104) / 2);
    SetWindowPos(quickCreate_, nullptr, toolsX, 16, 50, 48, SWP_NOZORDER);
    SetWindowPos(quickOpen_, nullptr, toolsX + 52, 16, 50, 48, SWP_NOZORDER);
    layoutCards();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void HomePage::layoutDashboard() {
    const int scale = std::max(1, static_cast<int>(std::lround(uiScale_ * 100.0f)));
    const auto px = [scale](int value) { return std::max(1, value * scale / 100); };
    const int pad = px(34);
    const int top = px(32) - scrollOffset_;
    const int compactButtonWidth = px(52);
    SetWindowPos(quickOpen_, nullptr, std::max(pad, width_ - pad - px(344)), top, px(196), px(42), SWP_NOZORDER);
    SetWindowTextW(quickOpen_, L"Открыть проект");
    SetWindowPos(quickCreate_, nullptr, std::max(pad, width_ - pad - px(136)), top, px(136), px(42), SWP_NOZORDER);
    SetWindowTextW(quickCreate_, L"Новый проект");
    (void)compactButtonWidth;

    const int quickTop = top + px(252);
    const int quickPanelWidth = std::max(px(480), (width_ - pad * 2) * 56 / 100);
    const int innerX = pad + px(16);
    const int gap = px(10);
    const int buttonWidth = std::max(px(104), (quickPanelWidth - px(32) - gap * 3) / 4);
    const int buttonHeight = px(112);
    SetWindowPos(openScript_, nullptr, innerX, quickTop + px(48), buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(openCards_, nullptr, innerX + buttonWidth + gap, quickTop + px(48), buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(newScene_, nullptr, innerX + (buttonWidth + gap) * 2, quickTop + px(48), buttonWidth, buttonHeight, SWP_NOZORDER);
    SetWindowPos(openDevelopment_, nullptr, innerX + (buttonWidth + gap) * 3, quickTop + px(48), buttonWidth, buttonHeight, SWP_NOZORDER);

    dashboardSceneRects_.clear();
    dashboardSceneIndices_.clear();
    const int scenesTop = top + px(476);
    const int sceneGap = px(12);
    const int sceneCount = std::min(5, static_cast<int>(document_.project().scenes.size()));
    const std::size_t sceneStart = document_.project().scenes.size() > static_cast<std::size_t>(sceneCount)
        ? document_.project().scenes.size() - static_cast<std::size_t>(sceneCount) : 0;
    const int sceneWidth = std::max(px(150), (width_ - pad * 2 - sceneGap * 4) / 5);
    for (int index = 0; index < sceneCount; ++index) {
        const int x = pad + index * (sceneWidth + sceneGap);
        dashboardSceneRects_.push_back({x, scenesTop + px(50), x + sceneWidth, scenesTop + px(216)});
        dashboardSceneIndices_.push_back(sceneStart + static_cast<std::size_t>(index));
    }
    contentHeight_ = std::max(height_, scenesTop + px(250) + scrollOffset_);
}

bool HomePage::handleCommand(int id, int code, HWND) {
    if ((id == IdCreate || id == IdQuickCreate) && code == BN_CLICKED) {
        if (create_) create_();
        return true;
    }
    if ((id == IdOpen || id == IdQuickOpen) && code == BN_CLICKED) {
        if (open_) open_();
        return true;
    }
    if (id == IdInstruction && code == BN_CLICKED) {
        MessageBoxW(hwnd_, L"Создайте новый проект или откройте существующий .mzoy.\n\n"
                           L"Чтобы добавить афишу, откройте Разработка → Сценарий → Титульная страница. "
                           L"Также можно нажать плюс на пустой обложке или карандаш на уже выбранной афише.",
                    L"Mezozoy: быстрый старт", MB_OK | MB_ICONINFORMATION);
        return true;
    }
    if (id == IdOpenScript && code == BN_CLICKED) {
        if (openScriptAction_) openScriptAction_();
        return true;
    }
    if (id == IdOpenCards && code == BN_CLICKED) {
        if (openCardsAction_) openCardsAction_();
        return true;
    }
    if (id == IdNewScene && code == BN_CLICKED) {
        if (newSceneAction_) newSceneAction_();
        return true;
    }
    if (id == IdOpenDevelopment && code == BN_CLICKED) {
        if (openDevelopmentAction_) openDevelopmentAction_();
        return true;
    }
    return false;
}

LRESULT HomePage::onMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paintInfo{};
        HDC dc = BeginPaint(hwnd_, &paintInfo);
        paintBuffered(dc);
        EndPaint(hwnd_, &paintInfo);
        return 1;
    }
    if (message == WM_PRINTCLIENT) {
        paintBuffered(reinterpret_cast<HDC>(wParam));
        return 1;
    }
    if (message == WM_MOUSEMOVE) {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (projectActive_) {
            int hovered = -1;
            for (std::size_t index = 0; index < dashboardSceneRects_.size(); ++index) {
                if (Contains(dashboardSceneRects_[index], point)) { hovered = static_cast<int>(index); break; }
            }
            if (hovered != hoveredDashboardScene_) {
                hoveredDashboardScene_ = hovered;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tracking);
            SetCursor(LoadCursorW(nullptr, hovered >= 0 ? IDC_HAND : IDC_ARROW));
            return 1;
        }
        const int hovered = hitProject(point);
        const bool actionHovered = hovered >= 0 && hitPosterAction(hovered, point);
        if (hovered != hoveredProject_ || actionHovered != posterActionHovered_) {
            const int previous = hoveredProject_;
            hoveredProject_ = hovered;
            posterActionHovered_ = actionHovered;
            invalidateProject(previous);
            if (hoveredProject_ != previous) invalidateProject(hoveredProject_);
        }
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tracking);
        SetCursor(LoadCursorW(nullptr, hovered >= 0 ? IDC_HAND : IDC_ARROW));
        return 1;
    }
    if (message == WM_MOUSELEAVE) {
        if (projectActive_) {
            hoveredDashboardScene_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 1;
        }
        const int previous = hoveredProject_;
        hoveredProject_ = -1;
        posterActionHovered_ = false;
        invalidateProject(previous);
        return 1;
    }
    if (message == WM_LBUTTONUP) {
        const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (projectActive_) {
            for (std::size_t index = 0; index < dashboardSceneRects_.size(); ++index) {
                if (Contains(dashboardSceneRects_[index], point)) {
                    if (openSceneAction_ && index < dashboardSceneIndices_.size()) openSceneAction_(dashboardSceneIndices_[index]);
                    return 1;
                }
            }
            return 1;
        }
        const int index = hitProject(point);
        if (index >= 0) {
            if (hitPosterAction(index, point)) choosePoster(index);
            else openProjectAt(index);
        }
        return 1;
    }
    if (message == WM_RBUTTONUP) {
        if (projectActive_) return 1;
        POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const int index = hitProject(client);
        if (index >= 0) {
            POINT screen = client;
            ClientToScreen(hwnd_, &screen);
            posterMenu(index, screen);
        }
        return 1;
    }
    if (message == WM_MOUSEWHEEL) {
        const int oldOffset = scrollOffset_;
        scrollOffset_ = std::clamp(scrollOffset_ - GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 96,
                                   0, std::max(0, contentHeight_ - height_));
        if (scrollOffset_ != oldOffset) {
            if (projectActive_) layoutDashboard(); else layoutCards();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 1;
    }
    return 0;
}

void HomePage::refreshRecent() {
    struct Candidate {
        std::filesystem::path path;
        std::filesystem::file_time_type modified{};
    };
    std::vector<Candidate> candidates;
    std::set<std::wstring> keys;
    auto addCandidate = [&](const std::filesystem::path& path) {
        if (path.empty() || IsBackupPath(path) || _wcsicmp(path.extension().c_str(), L".mzoy") != 0) return;
        const std::wstring key = NormalizedPathKey(path);
        if (!keys.insert(key).second) return;
        std::error_code error;
        candidates.push_back({path, std::filesystem::last_write_time(path, error)});
    };

    if (!document_.filePath().empty()) addCandidate(document_.filePath());
    const std::filesystem::path folder(settings_.get().projectsFolder);
    std::error_code error;
    if (std::filesystem::exists(folder, error)) {
        std::filesystem::recursive_directory_iterator iterator(folder, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (iterator != end) {
            if (!error && iterator->is_regular_file(error)) addCandidate(iterator->path());
            iterator.increment(error);
            if (error) error.clear();
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) { return left.modified > right.modified; });
    if (candidates.size() > 30) candidates.resize(30);

    recentProjects_.clear();
    if (document_.filePath().empty()) {
        RecentProject current;
        current.currentDocument = true;
        current.title = document_.project().title.empty() ? L"Без названия" : document_.project().title;
        current.subtitle = document_.project().logline.empty() ? L"Несохраненный проект" : document_.project().logline;
        current.modified = L"сейчас";
        current.poster = loadPoster(document_.project());
        recentProjects_.push_back(std::move(current));
    }

    ProjectSerializer serializer;
    for (const Candidate& candidate : candidates) {
        Project project;
        const bool current = !document_.filePath().empty() && NormalizedPathKey(candidate.path) == NormalizedPathKey(document_.filePath());
        if (current) project = document_.project();
        else {
            std::wstring ignored;
            if (!serializer.load(candidate.path, project, &ignored)) {
                project.title = candidate.path.stem().wstring();
            }
        }
        RecentProject item;
        item.path = candidate.path;
        item.currentDocument = current;
        item.title = project.title.empty() ? candidate.path.stem().wstring() : project.title;
        item.subtitle = !project.logline.empty() ? project.logline : (!project.genre.empty() ? project.genre : candidate.path.parent_path().filename().wstring());
        item.modified = ModifiedLabel(candidate.modified);
        item.poster = loadPoster(project);
        recentProjects_.push_back(std::move(item));
    }
    if (projectActive_) currentPoster_ = loadPoster(document_.project());
    scrollOffset_ = std::clamp(scrollOffset_, 0, std::max(0, contentHeight_ - height_));
    if (projectActive_) layoutDashboard(); else layoutCards();
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void HomePage::layoutCards() {
    const int rightStart = leftWidth_;
    const int padding = 36;
    const int available = std::max(260, width_ - rightStart - padding * 2);
    const int gap = 28;
    const int minimumCardWidth = 350;
    const int columns = std::max(1, std::min(3, (available + gap) / (minimumCardWidth + gap)));
    const int cardWidth = std::max(300, (available - gap * (columns - 1)) / columns);
    const int cardHeight = 212;
    const int top = 102 - scrollOffset_;
    for (std::size_t index = 0; index < recentProjects_.size(); ++index) {
        const int row = static_cast<int>(index) / columns;
        const int column = static_cast<int>(index) % columns;
        const int x = rightStart + padding + column * (cardWidth + gap);
        const int y = top + row * (cardHeight + gap);
        recentProjects_[index].card = {x, y, x + cardWidth, y + cardHeight};
        const int posterWidth = std::min(150, cardWidth * 39 / 100);
        if (recentProjects_[index].poster) {
            recentProjects_[index].posterAction = {x + posterWidth - 42, y + 10, x + posterWidth - 10, y + 42};
        } else {
            const int centerX = x + posterWidth / 2;
            const int centerY = y + cardHeight / 2;
            recentProjects_[index].posterAction = {centerX - 24, centerY - 24, centerX + 24, centerY + 24};
        }
    }
    const int rows = recentProjects_.empty() ? 0 : (static_cast<int>(recentProjects_.size()) + columns - 1) / columns;
    contentHeight_ = 102 + rows * (cardHeight + gap) + 32;
}

void HomePage::invalidateProject(int index) {
    if (index < 0 || index >= static_cast<int>(recentProjects_.size())) return;
    RECT dirty = recentProjects_[static_cast<std::size_t>(index)].card;
    InflateRect(&dirty, 8, 8);
    InvalidateRect(hwnd_, &dirty, FALSE);
}

void HomePage::paintBuffered(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const int width = std::max(1, static_cast<int>(client.right - client.left));
    const int height = std::max(1, static_cast<int>(client.bottom - client.top));
    if (!ensureBackBuffer(dc, width, height)) {
        paint(dc);
        return;
    }
    paint(backBufferDc_);
    BitBlt(dc, 0, 0, width, height, backBufferDc_, 0, 0, SRCCOPY);
}

bool HomePage::ensureBackBuffer(HDC referenceDc, int width, int height) {
    if (backBufferDc_ && backBufferBitmap_ && backBufferWidth_ == width && backBufferHeight_ == height) return true;
    releaseBackBuffer();
    backBufferDc_ = CreateCompatibleDC(referenceDc);
    if (!backBufferDc_) return false;
    backBufferBitmap_ = CreateCompatibleBitmap(referenceDc, width, height);
    if (!backBufferBitmap_) {
        releaseBackBuffer();
        return false;
    }
    backBufferPrevious_ = SelectObject(backBufferDc_, backBufferBitmap_);
    backBufferWidth_ = width;
    backBufferHeight_ = height;
    return true;
}

void HomePage::releaseBackBuffer() {
    if (backBufferDc_ && backBufferPrevious_) SelectObject(backBufferDc_, backBufferPrevious_);
    if (backBufferBitmap_) DeleteObject(backBufferBitmap_);
    if (backBufferDc_) DeleteDC(backBufferDc_);
    backBufferDc_ = nullptr;
    backBufferBitmap_ = nullptr;
    backBufferPrevious_ = nullptr;
    backBufferWidth_ = 0;
    backBufferHeight_ = 0;
}

void HomePage::paint(HDC dc) {
    if (projectActive_) {
        paintDashboard(dc);
        return;
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    HBRUSH background = CreateSolidBrush(theme_.background);
    FillRect(dc, &client, background);
    DeleteObject(background);
    RECT left{0, 0, leftWidth_, client.bottom};
    HBRUSH leftBrush = CreateSolidBrush(theme_.panel);
    FillRect(dc, &left, leftBrush);
    DeleteObject(leftBrush);

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    Gdiplus::Font heading(L"Segoe UI", 13.0f * uiScale_, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font titleFont(L"Segoe UI", 18.0f * uiScale_, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(L"Segoe UI", 13.0f * uiScale_, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 11.0f * uiScale_, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font actionIconFont(L"Segoe UI Symbol", 22.0f * uiScale_, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Color(theme_.text));
    Gdiplus::SolidBrush muted(Color(theme_.textMuted));
    Gdiplus::SolidBrush accent(Color(theme_.accent));
    Gdiplus::SolidBrush cardBrush(Color(theme_.panelAlt));
    Gdiplus::SolidBrush shadow(Color(RGB(0, 0, 0), 80));
    Gdiplus::Pen border(Color(theme_.border), 1.0f);
    Gdiplus::Pen hoverBorder(Color(theme_.accent), 1.0f);
    Gdiplus::StringFormat wrap;
    wrap.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
    wrap.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);

    const std::wstring header = recentProjects_.empty() ? L"ПРОЕКТЫ" : L"ПРОЕКТЫ  " + std::to_wstring(recentProjects_.size());
    graphics.DrawString(header.c_str(), -1, &heading, Gdiplus::PointF(static_cast<float>(leftWidth_ + 36), 76.0f), &muted);

    if (recentProjects_.empty()) {
        Gdiplus::RectF emptyArea(static_cast<float>(leftWidth_ + 36), 132.0f, static_cast<float>(std::max(200, width_ - leftWidth_ - 72)), 100.0f);
        graphics.DrawString(L"Здесь появятся ваши проекты. Создайте первую историю или откройте существующий файл .mzoy.",
                            -1, &bodyFont, emptyArea, &wrap, &muted);
        return;
    }

    for (std::size_t index = 0; index < recentProjects_.size(); ++index) {
        RecentProject& item = recentProjects_[index];
        if (item.card.bottom < 76 || item.card.top > height_) continue;
        const float x = static_cast<float>(item.card.left);
        const float y = static_cast<float>(item.card.top);
        const float cardWidth = static_cast<float>(item.card.right - item.card.left);
        const float cardHeight = static_cast<float>(item.card.bottom - item.card.top);
        Gdiplus::RectF shadowRect(x + 3.0f, y + 5.0f, cardWidth, cardHeight);
        Gdiplus::RectF cardRect(x, y, cardWidth, cardHeight);
        Gdiplus::GraphicsPath shadowPath;
        Gdiplus::GraphicsPath cardPath;
        AddRoundedPath(shadowPath, shadowRect, 6.0f);
        AddRoundedPath(cardPath, cardRect, 6.0f);
        graphics.FillPath(&shadow, &shadowPath);
        graphics.FillPath(&cardBrush, &cardPath);
        graphics.DrawPath(static_cast<int>(index) == hoveredProject_ ? &hoverBorder : &border, &cardPath);

        const float posterWidth = std::min(150.0f, cardWidth * 0.39f);
        const Gdiplus::RectF posterRect(x, y, posterWidth, cardHeight);
        const auto state = graphics.Save();
        graphics.SetClip(&cardPath);
        if (item.poster && item.poster->GetLastStatus() == Gdiplus::Ok) {
            const float sourceWidth = static_cast<float>(item.poster->GetWidth());
            const float sourceHeight = static_cast<float>(item.poster->GetHeight());
            const float scale = std::max(posterRect.Width / sourceWidth, posterRect.Height / sourceHeight);
            const float cropWidth = posterRect.Width / scale;
            const float cropHeight = posterRect.Height / scale;
            const float sourceX = (sourceWidth - cropWidth) * 0.5f;
            const float sourceY = (sourceHeight - cropHeight) * 0.5f;
            graphics.DrawImage(item.poster.get(), posterRect, sourceX, sourceY, cropWidth, cropHeight, Gdiplus::UnitPixel);
        } else {
            Gdiplus::LinearGradientBrush placeholder(posterRect, Color(theme_.selection), Color(theme_.background), Gdiplus::LinearGradientModeVertical);
            graphics.FillRectangle(&placeholder, posterRect);
        }
        graphics.Restore(state);

        const float textX = x + posterWidth + 17.0f;
        const float textWidth = std::max(80.0f, cardWidth - posterWidth - 30.0f);
        Gdiplus::RectF titleArea(textX, y + 14.0f, textWidth, 52.0f);
        Gdiplus::RectF subtitleArea(textX, y + 70.0f, textWidth, 58.0f);
        graphics.DrawString(item.title.c_str(), -1, &titleFont, titleArea, &wrap, &text);
        graphics.DrawString(item.subtitle.c_str(), -1, &bodyFont, subtitleArea, &wrap, &muted);
        graphics.DrawString(item.modified.c_str(), -1, &smallFont, Gdiplus::PointF(textX, y + 140.0f), &muted);

        const Gdiplus::RectF actionRect(static_cast<float>(item.posterAction.left), static_cast<float>(item.posterAction.top),
                                        static_cast<float>(item.posterAction.right - item.posterAction.left),
                                        static_cast<float>(item.posterAction.bottom - item.posterAction.top));
        const bool actionHovered = static_cast<int>(index) == hoveredProject_ && posterActionHovered_;
        Gdiplus::SolidBrush actionBackground(Color(actionHovered ? theme_.accent : theme_.panel, item.poster ? 224 : 242));
        Gdiplus::GraphicsPath actionPath;
        AddRoundedPath(actionPath, actionRect, item.poster ? 5.0f : 8.0f);
        graphics.FillPath(&actionBackground, &actionPath);
        graphics.DrawPath(actionHovered ? &hoverBorder : &border, &actionPath);
        Gdiplus::StringFormat actionFormat;
        actionFormat.SetAlignment(Gdiplus::StringAlignmentCenter);
        actionFormat.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        graphics.DrawString(item.poster ? L"✎" : L"+", -1, &actionIconFont, actionRect, &actionFormat, &text);

        if (item.currentDocument) {
            Gdiplus::SolidBrush currentBrush(Color(theme_.accent));
            graphics.FillEllipse(&currentBrush, x + cardWidth - 14.0f, y + 9.0f, 6.0f, 6.0f);
        }
    }
}

void HomePage::paintDashboard(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    HBRUSH background = CreateSolidBrush(theme_.background);
    FillRect(dc, &client, background);
    DeleteObject(background);

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

    const float scale = uiScale_;
    const float pad = 34.0f * scale;
    const float top = 32.0f * scale - static_cast<float>(scrollOffset_);
    const float gap = 12.0f * scale;
    const float available = std::max(420.0f, static_cast<float>(width_) - pad * 2.0f);

    Gdiplus::Font heroFont(L"Segoe UI", 28.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font titleFont(L"Segoe UI", 15.0f * scale, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font bodyFont(L"Segoe UI", 13.0f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font metricFont(L"Segoe UI", 24.0f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 11.0f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font tinyFont(L"Segoe UI", 10.0f * scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush text(Color(theme_.text));
    Gdiplus::SolidBrush muted(Color(theme_.textMuted));
    Gdiplus::SolidBrush accent(Color(theme_.accent));
    Gdiplus::SolidBrush panel(Color(theme_.panel));
    Gdiplus::SolidBrush panelAlt(Color(theme_.panelAlt));
    Gdiplus::SolidBrush subtleAccent(Color(theme_.accent, 34));
    Gdiplus::SolidBrush shadow(Color(RGB(0, 0, 0), 92));
    Gdiplus::Pen border(Color(theme_.border), 1.0f);
    Gdiplus::Pen accentPen(Color(theme_.accent), 2.0f * scale);
    Gdiplus::StringFormat wrap;
    wrap.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);
    wrap.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);

    const Project& project = document_.project();
    const StatisticsSnapshot snapshot = statistics_.analyze(project);
    graphics.DrawString(project.title.empty() ? L"Без названия" : project.title.c_str(), -1, &heroFont,
                        Gdiplus::PointF(pad, top + 2.0f * scale), &text);
    graphics.DrawString(L"Текущий проект", -1, &bodyFont, Gdiplus::PointF(pad, top + 39.0f * scale), &accent);

    const float metricsTop = top + 82.0f * scale;
    const float metricWidth = (available - gap * 4.0f) / 5.0f;
    std::wstring saved = L"Не сохранён";
    if (!document_.filePath().empty()) {
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(document_.filePath(), error);
        if (!error) saved = ModifiedLabel(modified);
    }
    const std::wstring metricTitles[] = {L"Сцен", L"Персонажей", L"Локаций", L"Хронометраж", L"Сохранение"};
    std::wostringstream runtime;
    runtime << std::fixed << std::setprecision(1) << snapshot.minutes << L" мин";
    const std::wstring metricValues[] = {
        std::to_wstring(project.scenes.size()), std::to_wstring(project.characters.size()),
        std::to_wstring(project.locations.size()), runtime.str(), saved
    };
    const std::wstring metricNotes[] = {
        snapshot.emptyScenes ? std::to_wstring(snapshot.emptyScenes) + L" требуют текста" : L"Все сцены заполнены",
        project.characters.empty() ? L"Добавьте героев" : L"В разработке проекта",
        project.locations.empty() ? L"Добавьте места" : L"В мире истории",
        L"Примерная оценка", settings_.get().autosave ? L"Автосохранение включено" : L"Автосохранение выключено"
    };

    for (int index = 0; index < 5; ++index) {
        const float x = pad + index * (metricWidth + gap);
        Gdiplus::RectF shadowRect(x + 2.0f, metricsTop + 4.0f, metricWidth, 112.0f * scale);
        Gdiplus::RectF cardRect(x, metricsTop, metricWidth, 112.0f * scale);
        Gdiplus::GraphicsPath shadowPath;
        Gdiplus::GraphicsPath cardPath;
        AddRoundedPath(shadowPath, shadowRect, 7.0f * scale);
        AddRoundedPath(cardPath, cardRect, 7.0f * scale);
        graphics.FillPath(&shadow, &shadowPath);
        graphics.FillPath(&panel, &cardPath);
        graphics.DrawPath(&border, &cardPath);
        graphics.FillEllipse(&subtleAccent, x + 14.0f * scale, metricsTop + 15.0f * scale, 34.0f * scale, 34.0f * scale);
        Gdiplus::RectF iconArea(x + 14.0f * scale, metricsTop + 15.0f * scale, 34.0f * scale, 34.0f * scale);
        Gdiplus::StringFormat centered;
        centered.SetAlignment(Gdiplus::StringAlignmentCenter);
        centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const wchar_t* icon = index == 0 ? L"▤" : index == 1 ? L"●●" : index == 2 ? L"⌖" : index == 3 ? L"◷" : L"✓";
        graphics.DrawString(icon, -1, &bodyFont, iconArea, &centered, &accent);
        graphics.DrawString(metricTitles[index].c_str(), -1, &smallFont,
                            Gdiplus::PointF(x + 58.0f * scale, metricsTop + 15.0f * scale), &muted);
        if (index == 4) {
            Gdiplus::RectF valueArea(x + 58.0f * scale, metricsTop + 37.0f * scale,
                                    metricWidth - 68.0f * scale, 30.0f * scale);
            graphics.DrawString(metricValues[index].c_str(), -1, &bodyFont, valueArea, &wrap, &text);
        } else {
            graphics.DrawString(metricValues[index].c_str(), -1, &metricFont,
                                Gdiplus::PointF(x + 58.0f * scale, metricsTop + 34.0f * scale), &text);
        }
        Gdiplus::RectF noteArea(x + 14.0f * scale, metricsTop + 82.0f * scale,
                               metricWidth - 28.0f * scale, 20.0f * scale);
        graphics.DrawString(metricNotes[index].c_str(), -1, &tinyFont, noteArea, &wrap, &muted);
    }

    const float middleTop = top + 252.0f * scale;
    const float quickWidth = available * 0.56f;
    const float activityWidth = available * 0.24f;
    const float progressWidth = available - quickWidth - activityWidth - gap * 2.0f;
    const Gdiplus::RectF quickRect(pad, middleTop, quickWidth, 178.0f * scale);
    const Gdiplus::RectF progressRect(quickRect.GetRight() + gap, middleTop, progressWidth, 178.0f * scale);
    const Gdiplus::RectF activityRect(progressRect.GetRight() + gap, middleTop, activityWidth, 178.0f * scale);
    for (const Gdiplus::RectF& rect : {quickRect, progressRect, activityRect}) {
        Gdiplus::GraphicsPath path;
        AddRoundedPath(path, rect, 7.0f * scale);
        graphics.FillPath(&panel, &path);
        graphics.DrawPath(&border, &path);
    }
    graphics.DrawString(L"Быстрые действия", -1, &titleFont,
                        Gdiplus::PointF(quickRect.X + 16.0f * scale, quickRect.Y + 14.0f * scale), &text);
    graphics.DrawString(L"Прогресс проекта", -1, &titleFont,
                        Gdiplus::PointF(progressRect.X + 16.0f * scale, progressRect.Y + 14.0f * scale), &text);
    graphics.DrawString(L"Недавняя активность", -1, &titleFont,
                        Gdiplus::PointF(activityRect.X + 16.0f * scale, activityRect.Y + 14.0f * scale), &text);

    int readyScenes = 0;
    for (const Scene& scene : project.scenes) if (ScriptService::countWords(scene.text) > 8) ++readyScenes;
    const int progress = project.scenes.empty() ? 0 : readyScenes * 100 / static_cast<int>(project.scenes.size());
    const float ringSize = std::min(progressRect.Width - 28.0f * scale, 86.0f * scale);
    const Gdiplus::RectF ring(progressRect.X + 16.0f * scale, progressRect.Y + 62.0f * scale, ringSize, ringSize);
    Gdiplus::Pen ringBack(Color(theme_.border), 8.0f * scale);
    Gdiplus::Pen ringAccent(Color(theme_.accent), 8.0f * scale);
    ringBack.SetStartCap(Gdiplus::LineCapRound); ringBack.SetEndCap(Gdiplus::LineCapRound);
    ringAccent.SetStartCap(Gdiplus::LineCapRound); ringAccent.SetEndCap(Gdiplus::LineCapRound);
    graphics.DrawArc(&ringBack, ring, -90.0f, 360.0f);
    graphics.DrawArc(&ringAccent, ring, -90.0f, progress * 3.6f);
    Gdiplus::StringFormat centered;
    centered.SetAlignment(Gdiplus::StringAlignmentCenter);
    centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    graphics.DrawString((std::to_wstring(progress) + L"%").c_str(), -1, &titleFont, ring, &centered, &text);
    const float progressTextX = ring.GetRight() + 12.0f * scale;
    Gdiplus::RectF progressText(progressTextX, progressRect.Y + 72.0f * scale,
                                progressRect.GetRight() - progressTextX - 12.0f * scale, 58.0f * scale);
    graphics.DrawString((std::to_wstring(readyScenes) + L" из " + std::to_wstring(project.scenes.size()) + L" сцен\nготовы к черновику").c_str(),
                        -1, &smallFont, progressText, &wrap, &muted);

    const std::size_t activityCount = std::min<std::size_t>(3, project.scenes.size());
    for (std::size_t row = 0; row < activityCount; ++row) {
        const std::size_t sceneIndex = project.scenes.size() - 1 - row;
        const Scene& scene = project.scenes[sceneIndex];
        const float rowY = activityRect.Y + (48.0f + static_cast<float>(row) * 39.0f) * scale;
        graphics.FillEllipse(&subtleAccent, activityRect.X + 16.0f * scale, rowY + 4.0f * scale, 22.0f * scale, 22.0f * scale);
        graphics.DrawString(L"▤", -1, &tinyFont,
                            Gdiplus::PointF(activityRect.X + 21.0f * scale, rowY + 6.0f * scale), &accent);
        Gdiplus::RectF rowText(activityRect.X + 48.0f * scale, rowY,
                              activityRect.Width - 60.0f * scale, 34.0f * scale);
        graphics.DrawString(scene.title.c_str(), -1, &smallFont, rowText, &wrap, &text);
    }
    if (activityCount == 0) {
        graphics.DrawString(L"Пока нет сцен", -1, &smallFont,
                            Gdiplus::PointF(activityRect.X + 16.0f * scale, activityRect.Y + 62.0f * scale), &muted);
    }

    const float scenesTop = top + 476.0f * scale;
    graphics.DrawString(L"Последние сцены", -1, &titleFont, Gdiplus::PointF(pad, scenesTop), &text);
    graphics.DrawString(L"Откройте сцену одним нажатием", -1, &smallFont,
                        Gdiplus::PointF(pad + 148.0f * scale, scenesTop + 3.0f * scale), &muted);
    for (std::size_t cardIndex = 0; cardIndex < dashboardSceneRects_.size() && cardIndex < dashboardSceneIndices_.size(); ++cardIndex) {
        const RECT& raw = dashboardSceneRects_[cardIndex];
        const std::size_t sceneIndex = dashboardSceneIndices_[cardIndex];
        if (sceneIndex >= project.scenes.size()) continue;
        const Scene& scene = project.scenes[sceneIndex];
        Gdiplus::RectF card(static_cast<float>(raw.left), static_cast<float>(raw.top),
                            static_cast<float>(raw.right - raw.left), static_cast<float>(raw.bottom - raw.top));
        Gdiplus::GraphicsPath path;
        AddRoundedPath(path, card, 7.0f * scale);
        graphics.FillPath(&panel, &path);
        graphics.DrawPath(static_cast<int>(cardIndex) == hoveredDashboardScene_ ? &accentPen : &border, &path);
        const auto state = graphics.Save();
        graphics.SetClip(&path);
        const Gdiplus::RectF imageArea(card.X, card.Y, card.Width, 88.0f * scale);
        if (currentPoster_ && currentPoster_->GetLastStatus() == Gdiplus::Ok) {
            const float sourceWidth = static_cast<float>(currentPoster_->GetWidth());
            const float sourceHeight = static_cast<float>(currentPoster_->GetHeight());
            const float imageScale = std::max(imageArea.Width / sourceWidth, imageArea.Height / sourceHeight);
            const float cropWidth = imageArea.Width / imageScale;
            const float cropHeight = imageArea.Height / imageScale;
            graphics.DrawImage(currentPoster_.get(), imageArea, (sourceWidth - cropWidth) * 0.5f,
                               (sourceHeight - cropHeight) * 0.5f, cropWidth, cropHeight, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush veil(Color(theme_.background, 70));
            graphics.FillRectangle(&veil, imageArea);
        } else {
            Gdiplus::LinearGradientBrush placeholder(imageArea, Color(theme_.selection), Color(theme_.panelAlt),
                                                       Gdiplus::LinearGradientModeHorizontal);
            graphics.FillRectangle(&placeholder, imageArea);
        }
        graphics.Restore(state);
        Gdiplus::RectF numberBadge(card.X + 10.0f * scale, card.Y + 10.0f * scale, 34.0f * scale, 24.0f * scale);
        Gdiplus::GraphicsPath badgePath;
        AddRoundedPath(badgePath, numberBadge, 5.0f * scale);
        graphics.FillPath(&subtleAccent, &badgePath);
        graphics.DrawString(std::to_wstring(sceneIndex + 1).c_str(), -1, &smallFont, numberBadge, &centered, &text);
        Gdiplus::RectF sceneTitle(card.X + 12.0f * scale, card.Y + 98.0f * scale,
                                  card.Width - 24.0f * scale, 34.0f * scale);
        graphics.DrawString(scene.title.c_str(), -1, &bodyFont, sceneTitle, &wrap, &text);
        Gdiplus::RectF sceneSummary(card.X + 12.0f * scale, card.Y + 133.0f * scale,
                                    card.Width - 24.0f * scale, 25.0f * scale);
        const std::wstring summary = scene.summary.empty() ? L"Без описания" : scene.summary;
        graphics.DrawString(summary.c_str(), -1, &tinyFont, sceneSummary, &wrap, &muted);
    }
    if (dashboardSceneRects_.empty()) {
        graphics.DrawString(L"Создайте первую сцену, и она появится здесь.", -1, &bodyFont,
                            Gdiplus::PointF(pad, scenesTop + 58.0f * scale), &muted);
    }
}

int HomePage::hitProject(POINT point) const {
    for (std::size_t index = 0; index < recentProjects_.size(); ++index)
        if (Contains(recentProjects_[index].card, point)) return static_cast<int>(index);
    return -1;
}

bool HomePage::hitPosterAction(int index, POINT point) const {
    return index >= 0 && index < static_cast<int>(recentProjects_.size()) && Contains(recentProjects_[static_cast<std::size_t>(index)].posterAction, point);
}

void HomePage::openProjectAt(int index) {
    if (index < 0 || index >= static_cast<int>(recentProjects_.size())) return;
    const RecentProject& item = recentProjects_[static_cast<std::size_t>(index)];
    if (item.currentDocument && item.path.empty()) return;
    if (!item.path.empty() && openPath_) openPath_(item.path);
}

void HomePage::posterMenu(int index, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ChangePoster, L"Выбрать афишу...");
    if (recentProjects_[static_cast<std::size_t>(index)].poster) AppendMenuW(menu, MF_STRING, RemovePoster, L"Удалить афишу");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, OpenProject, L"Открыть проект");
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == ChangePoster) choosePoster(index);
    else if (command == RemovePoster) removePoster(index);
    else if (command == OpenProject) openProjectAt(index);
}

void HomePage::choosePoster(int index) {
    const auto file = OpenFileDialog(hwnd_, false,
        L"Изображения (*.png;*.jpg;*.jpeg;*.bmp;*.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif\0PNG (*.png)\0*.png\0JPEG (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0Все файлы (*.*)\0*.*\0\0",
        L"png");
    if (!file.empty()) updatePoster(index, &file);
}

void HomePage::removePoster(int index) {
    if (MessageBoxW(hwnd_, L"Удалить афишу из проекта?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) == IDYES)
        updatePoster(index, nullptr);
}

bool HomePage::updatePoster(int index, const std::filesystem::path* imagePath) {
    if (index < 0 || index >= static_cast<int>(recentProjects_.size())) return false;
    RecentProject& recent = recentProjects_[static_cast<std::size_t>(index)];
    std::wstring error;
    if (recent.currentDocument) {
        document_.checkpoint(imagePath ? L"Изменение афиши" : L"Удаление афиши");
        Project& project = document_.editProject();
        if (imagePath) {
            if (!PosterService::importFile(*imagePath, project, &error)) {
                MessageBoxW(hwnd_, error.c_str(), L"Афиша проекта", MB_OK | MB_ICONERROR);
                return false;
            }
        } else {
            PosterService::clear(project);
        }
        document_.markChanged();
        if (save_ && !save_()) return false;
    } else {
        ProjectSerializer serializer;
        Project project;
        if (!serializer.load(recent.path, project, &error)) {
            MessageBoxW(hwnd_, error.c_str(), L"Афиша проекта", MB_OK | MB_ICONERROR);
            return false;
        }
        if (imagePath) {
            if (!PosterService::importFile(*imagePath, project, &error)) {
                MessageBoxW(hwnd_, error.c_str(), L"Афиша проекта", MB_OK | MB_ICONERROR);
                return false;
            }
        } else {
            PosterService::clear(project);
        }
        if (!serializer.save(recent.path, project, &error)) {
            MessageBoxW(hwnd_, error.c_str(), L"Афиша проекта", MB_OK | MB_ICONERROR);
            return false;
        }
    }
    refreshRecent();
    return true;
}

std::unique_ptr<Gdiplus::Bitmap> HomePage::loadPoster(const Project& project) const {
    const std::vector<unsigned char> bytes = PosterService::decode(project);
    if (bytes.empty()) return {};
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return {};
    void* target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return {};
    }
    std::copy(bytes.begin(), bytes.end(), static_cast<unsigned char*>(target));
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return {};
    }
    std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromStream(stream, FALSE));
    std::unique_ptr<Gdiplus::Bitmap> result;
    if (source && source->GetLastStatus() == Gdiplus::Ok && source->GetWidth() > 0 && source->GetHeight() > 0) {
        result = std::make_unique<Gdiplus::Bitmap>(source->GetWidth(), source->GetHeight(), PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(result.get());
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(source.get(), 0, 0, source->GetWidth(), source->GetHeight());
    }
    source.reset();
    stream->Release();
    return result;
}

}  // namespace mezozoy::ui
