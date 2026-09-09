#include "BoardCanvas.h"

#include "Dialogs.h"
#include "Win32Util.h"
#include "../core/Utf.h"
#include "../services/ScriptService.h"

#include <windowsx.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace mezozoy::ui {
namespace {

constexpr wchar_t CanvasClass[] = L"Mezozoy.BoardCanvas";
constexpr int MenuNewSection = 7201;
constexpr int MenuRename = 7202;
constexpr int MenuLock = 7203;
constexpr int MenuDelete = 7204;
constexpr int MenuRenameScene = 7205;
constexpr int MenuOpenScene = 7206;
constexpr int MenuEditSummary = 7207;
constexpr int MenuDuplicateScene = 7208;
constexpr int MenuMoveUp = 7209;
constexpr int MenuMoveDown = 7210;

D2D1_RECT_F NormalizeRect(POINT a, POINT b) {
    return D2D1::RectF(static_cast<float>(std::min(a.x, b.x)), static_cast<float>(std::min(a.y, b.y)),
                       static_cast<float>(std::max(a.x, b.x)), static_cast<float>(std::max(a.y, b.y)));
}

float Distance(POINT a, POINT b) {
    const float dx = static_cast<float>(a.x - b.x);
    const float dy = static_cast<float>(a.y - b.y);
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

BoardCanvas::BoardCanvas(HINSTANCE instance, HWND parent, ProjectDocument& document, SettingsService& settings, const Theme& theme)
    : instance_(instance), document_(document), settings_(settings), theme_(theme) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc; wc.hInstance = instance_; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr; wc.lpszClassName = CanvasClass; wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        RegisterClassExW(&wc); registered = true;
    }
    hwnd_ = CreateWindowExW(0, CanvasClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, parent, nullptr, instance_, this);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory_);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory_));
    if (writeFactory_) {
        writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"ru-RU", &titleFormat_);
        writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, 12.5f, L"ru-RU", &bodyFormat_);
        writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                                        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"ru-RU", &sectionFormat_);
        if (titleFormat_) { titleFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); }
        if (bodyFormat_) { bodyFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP); }
    }
    document_.subscribe([this](DocumentChange) { if (IsWindow(hwnd_)) refresh(); });
}

BoardCanvas::~BoardCanvas() {
    if (hwnd_) KillTimer(hwnd_, ZoomTimer);
    discardResources();
    clearTextCache();
    for(auto*& path:lockPaths_) SafeRelease(path);
    SafeRelease(sectionFormat_); SafeRelease(bodyFormat_); SafeRelease(titleFormat_); SafeRelease(writeFactory_); SafeRelease(factory_);
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

void BoardCanvas::layout(int x, int y, int width, int height) {
    SetWindowPos(hwnd_, nullptr, x, y, std::max(0, width), std::max(0, height), SWP_NOZORDER);
}

void BoardCanvas::refresh() {
    if (!renderingSuspended_ && IsWindowVisible(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
}

void BoardCanvas::applyTheme(const Theme& theme) {
    theme_ = theme;
    discardResources();
    refresh();
}

void BoardCanvas::setRenderingSuspended(bool suspended) {
    if (renderingSuspended_ == suspended) return;
    renderingSuspended_ = suspended;
    if (suspended) {
        KillTimer(hwnd_, ZoomTimer);
        zoomTick_ = 0;
        targetZoom_ = camera_.zoom;
        if (mode_ == Mode::Zooming) mode_ = Mode::None;
    } else {
        refresh();
    }
}

LRESULT CALLBACK BoardCanvas::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    BoardCanvas* self = reinterpret_cast<BoardCanvas*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<BoardCanvas*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT BoardCanvas::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_APP+93: {
            // Isolated GUI tests request a native snapshot without desktop capture.
            wchar_t profile[32768]{};
            if(!GetEnvironmentVariableW(L"MEZOZOY_PROFILE_DIR",profile,32768)) return 0;
            const std::filesystem::path directory(profile);
            if(!directory.is_absolute()) return 0;
            RECT r{}; GetClientRect(hwnd_,&r); if(r.right<=0 || r.bottom<=0) return 0;
            HDC screen=GetDC(hwnd_), dc=CreateCompatibleDC(screen);
            HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom); ReleaseDC(hwnd_,screen);
            if(!dc || !bitmap) { if(dc) DeleteDC(dc); if(bitmap) DeleteObject(bitmap); return 0; }
            auto old=SelectObject(dc,bitmap);
            SendMessageW(hwnd_,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
            ULONG_PTR token{}; Gdiplus::GdiplusStartupInput startup;
            bool saved=false;
            if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)==Gdiplus::Ok) {
                {
                    Gdiplus::Bitmap image(bitmap,nullptr);
                    const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
                    saved=image.Save((directory/L"board-snapshot.png").c_str(),&png,nullptr)==Gdiplus::Ok;
                }
                Gdiplus::GdiplusShutdown(token);
            }
            SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
            return saved;
        }
        case WM_APP+92: return reinterpret_cast<LRESULT>(GetPropW(hwnd_,L"Mezozoy.PrintCheck"));
        case WM_APP + 90:
        case WM_APP + 91: {
            if(wParam>=document_.project().scenes.size()) return -1;
            const auto& scene=document_.project().scenes[wParam];
            if(message==WM_APP+91) return scene.locked;
            const auto point=worldToScreen(scene.cardX+CardWidth-20,scene.cardY+20);
            return MAKELPARAM(static_cast<int>(point.x),static_cast<int>(point.y));
        }
        case WM_PRINT:
        case WM_PRINTCLIENT: {
            SetPropW(hwnd_,L"Mezozoy.PrintCheck",reinterpret_cast<HANDLE>(1));
            // PrintWindow cannot capture a HWND Direct2D surface. Reuse the actual
            // board drawing with temporary DC-compatible brushes, leaving live resources intact.
            createResources(); if(!factory_) { SetPropW(hwnd_,L"Mezozoy.PrintCheck",reinterpret_cast<HANDLE>(2)); return 0; }
            RECT rect{}; GetClientRect(hwnd_,&rect);
            ID2D1DCRenderTarget* dcTarget=nullptr;
            const auto properties=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
            HRESULT printResult=factory_->CreateDCRenderTarget(&properties,&dcTarget);
            if(FAILED(printResult)) { SetPropW(hwnd_,L"Mezozoy.PrintCheck",reinterpret_cast<HANDLE>(static_cast<INT_PTR>(printResult))); return 0; }
            if(FAILED(dcTarget->BindDC(reinterpret_cast<HDC>(wParam),&rect))) { SafeRelease(dcTarget); return 0; }
            ID2D1SolidColorBrush** slots[]={&gridMinorBrush_,&gridMajorBrush_,&sectionFillBrush_,&sectionHeaderBrush_,
                &shadowBrush_,&panelBrush_,&borderBrush_,&accentBrush_,&textBrush_,&mutedBrush_,
                &selectionForwardFillBrush_,&selectionForwardBorderBrush_,&selectionBackwardFillBrush_,
                &selectionBackwardBorderBrush_,&diagnosticsFillBrush_};
            std::vector<ID2D1SolidColorBrush*> originals;
            const D2D1_COLOR_F colors[]={Theme::D2D(theme_.border,.34f),Theme::D2D(theme_.accent,.46f),
                Theme::D2D(theme_.panelAlt,.86f),Theme::D2D(theme_.panel,.98f),D2D1::ColorF(0,0,0,theme_.light ? .10f : .58f),
                Theme::D2D(theme_.panel),Theme::D2D(theme_.border),Theme::D2D(theme_.accent),Theme::D2D(theme_.text),Theme::D2D(theme_.textMuted),
                Theme::D2D(theme_.accent,.16f),Theme::D2D(theme_.accent,.95f),Theme::D2D(theme_.accent,.24f),
                Theme::D2D(theme_.accent,.95f),Theme::D2D(theme_.panel,.88f)};
            for(std::size_t i=0;i<std::size(slots);++i) {
                auto slot=slots[i];
                auto* original=*slot; originals.push_back(original); *slot=nullptr;
                dcTarget->CreateSolidColorBrush(original?original->GetColor():colors[i],slot);
            }
            dcTarget->BeginDraw(); dcTarget->Clear(Theme::D2D(theme_.background));
            const auto size=dcTarget->GetSize(); drawGrid(dcTarget,size);
            dcTarget->SetTransform(D2D1::Matrix3x2F(camera_.zoom,0,0,camera_.zoom,-camera_.x*camera_.zoom,-camera_.y*camera_.zoom));
            drawSections(dcTarget,size); drawCards(dcTarget,size);
            dcTarget->SetTransform(D2D1::Matrix3x2F::Identity()); drawSelectionRectangle(dcTarget);
            if(diagnostics_) drawDiagnostics(dcTarget);
            printResult=dcTarget->EndDraw();
            SetPropW(hwnd_,L"Mezozoy.PrintCheck",reinterpret_cast<HANDLE>(FAILED(printResult)?static_cast<INT_PTR>(printResult):3));
            for(std::size_t i=0;i<std::size(slots);++i) { SafeRelease(*slots[i]); *slots[i]=originals[i]; }
            SafeRelease(dcTarget); return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_SIZE: {
            const UINT width = LOWORD(lParam);
            const UINT height = HIWORD(lParam);
            if (target_ && width > 0 && height > 0 && FAILED(target_->Resize(D2D1::SizeU(width, height)))) discardResources();
            refresh();
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd_, &paint);
            if (!renderingSuspended_) render();
            EndPaint(hwnd_, &paint);
            return 0;
        }
        case WM_MOUSELEAVE:
            hoverSceneId_=-1; hoverSectionId_=-1; hoverLock_=false; refresh(); return 0;
        case WM_LBUTTONDBLCLK: {
            const Hit hit = hitTest({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hit.scene >= 0) { openScene(hit.scene); return 0; }
            if (hit.section >= 0) { renameSection(hit.section); return 0; }
            break;
        }
        case WM_TIMER:
            if (renderingSuspended_) return 0;
            if (wParam == ZoomTimer) {
                if (!IsWindowVisible(hwnd_)) {
                    KillTimer(hwnd_, ZoomTimer);
                    zoomTick_ = 0;
                    targetZoom_ = camera_.zoom;
                    mode_ = Mode::None;
                    return 0;
                }
                const DWORD now = GetTickCount();
                const float elapsed = zoomTick_ ? std::clamp(static_cast<float>(now - zoomTick_) / 1000.0f, 0.001f, 0.05f) : 0.016f;
                zoomTick_ = now;
                const float delta = targetZoom_ - camera_.zoom;
                if (std::fabs(delta) < 0.0002f) {
                    camera_.zoom = targetZoom_; KillTimer(hwnd_, ZoomTimer); zoomTick_ = 0; mode_ = Mode::None;
                } else {
                    const float alpha = 1.0f - std::exp(-32.0f * elapsed);
                    camera_.zoom += delta * alpha;
                }
                camera_.x = zoomAnchorWorld_.x - zoomAnchorScreen_.x / camera_.zoom;
                camera_.y = zoomAnchorWorld_.y - zoomAnchorScreen_.y / camera_.zoom;
                refresh(); return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            if (mode_ == Mode::Panning || mode_ == Mode::PendingCard || mode_ == Mode::PendingSection || mode_ == Mode::DraggingCard ||
                mode_ == Mode::DraggingSection || mode_ == Mode::ResizingSection || mode_ == Mode::Selecting) return 0;
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; ScreenToClient(hwnd_, &cursor);
            zoomAnchorScreen_ = D2D1::Point2F(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
            zoomAnchorWorld_ = screenToWorld(cursor);
            const float wheelSteps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
            const float factor = std::exp(wheelSteps * 0.145f);
            targetZoom_ = std::clamp(targetZoom_ * factor, 0.2f, 3.0f);
            camera_.zoom += (targetZoom_ - camera_.zoom) * 0.34f;
            camera_.x = zoomAnchorWorld_.x - zoomAnchorScreen_.x / camera_.zoom;
            camera_.y = zoomAnchorWorld_.y - zoomAnchorScreen_.y / camera_.zoom;
            zoomTick_ = GetTickCount(); mode_ = Mode::Zooming; SetTimer(hwnd_, ZoomTimer, 8, nullptr); refresh(); return 0;
        }
        case WM_MBUTTONDOWN: {
            KillTimer(hwnd_, ZoomTimer); zoomTick_ = 0; targetZoom_ = camera_.zoom;
            mode_ = Mode::Panning; mouseDown_ = lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd_); SetCursor(LoadCursorW(nullptr, IDC_SIZEALL)); return 0;
        }
        case WM_MBUTTONUP:
            if (mode_ == Mode::Panning) { mode_ = Mode::None; ReleaseCapture(); SetCursor(LoadCursorW(nullptr, IDC_ARROW)); }
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            KillTimer(hwnd_, ZoomTimer); zoomTick_ = 0; targetZoom_ = camera_.zoom;
            mouseDown_ = lastMouse_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            gestureCheckpointed_ = false;
            const D2D1_POINT_2F world = screenToWorld(mouseDown_);
            const Hit hit = hitTest(mouseDown_);
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (hit.scene >= 0) {
                Scene& scene = const_cast<Project&>(document_.project()).scenes[static_cast<std::size_t>(hit.scene)];
                if (hit.lock) { document_.checkpoint(L"Блокировка карточки"); scene.locked = !scene.locked; document_.markChanged(); mode_ = Mode::None; refresh(); return 0; }
                if (ctrl) scene.selected = !scene.selected; else if (!scene.selected) selectOnlyScene(hit.scene);
                activeScene_ = hit.scene; activeSection_ = -1; dragWorldStart_ = world; mode_ = Mode::PendingCard;
                sceneStartPositions_.clear();
                for (const auto& item : document_.project().scenes) sceneStartPositions_.push_back(D2D1::Point2F(item.cardX, item.cardY));
                SetCapture(hwnd_); refresh(); return 0;
            }
            if (hit.section >= 0) {
                BoardSection& section = const_cast<Project&>(document_.project()).boardSections[static_cast<std::size_t>(hit.section)];
                if (hit.lock) { document_.checkpoint(L"Блокировка раздела"); section.locked = !section.locked; document_.markChanged(); mode_ = Mode::None; refresh(); return 0; }
                if (ctrl) section.selected = !section.selected; else if (!section.selected) selectOnlySection(hit.section);
                activeSection_ = hit.section; activeScene_ = -1; dragWorldStart_ = world; resizeMask_ = hit.resizeMask;
                originalSection_ = sectionRect(section);
                if (resizeMask_ && !section.locked) mode_ = Mode::ResizingSection;
                else if (!section.locked) { beginSectionMove(hit.section, world); mode_ = Mode::PendingSection; }
                else mode_ = Mode::None;
                SetCapture(hwnd_); refresh(); return 0;
            }
            additiveSelection_ = ctrl;
            if (!additiveSelection_) clearSelection();
            selectionStart_ = selectionEnd_ = mouseDown_; mode_ = Mode::Selecting; SetCapture(hwnd_); refresh(); return 0;
        }
        case WM_MOUSEMOVE: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if(mode_==Mode::None) {
                const Hit hovered=hitTest(point);
                const int scene=hovered.scene>=0?document_.project().scenes[hovered.scene].id:-1;
                const int section=hovered.section>=0?document_.project().boardSections[hovered.section].id:-1;
                if(scene!=hoverSceneId_ || section!=hoverSectionId_ || hovered.lock!=hoverLock_) {
                    hoverSceneId_=scene; hoverSectionId_=section; hoverLock_=hovered.lock; refresh();
                }
                TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd_,0}; TrackMouseEvent(&track);
                if(hovered.lock) { SetCursor(LoadCursorW(nullptr,IDC_HAND)); return 0; }
            }
            if (mode_ == Mode::Panning) {
                camera_.x -= static_cast<float>(point.x - lastMouse_.x) / camera_.zoom;
                camera_.y -= static_cast<float>(point.y - lastMouse_.y) / camera_.zoom;
                lastMouse_ = point; refresh(); return 0;
            }
            if (mode_ == Mode::PendingCard && Distance(point, mouseDown_) >= 5.0f) {
                if (activeScene_ >= 0 && !document_.project().scenes[static_cast<std::size_t>(activeScene_)].locked) {
                    document_.checkpoint(L"Перемещение карточки"); gestureCheckpointed_ = true; mode_ = Mode::DraggingCard;
                }
            }
            if (mode_ == Mode::PendingSection && Distance(point, mouseDown_) >= 5.0f) {
                document_.checkpoint(L"Перемещение раздела"); gestureCheckpointed_ = true; mode_ = Mode::DraggingSection;
            }
            if (mode_ == Mode::DraggingCard || mode_ == Mode::DraggingSection || mode_ == Mode::ResizingSection) {
                if (!gestureCheckpointed_) {
                    document_.checkpoint(mode_ == Mode::ResizingSection ? L"Изменение размера раздела" : L"Перемещение объекта");
                    gestureCheckpointed_ = true;
                }
                moveActive(screenToWorld(point)); refresh(); return 0;
            }
            if (mode_ == Mode::Selecting) { selectionEnd_ = point; refresh(); return 0; }
            const Hit hit = hitTest(point);
            if (hit.resizeMask) SetCursor(LoadCursorW(nullptr, (hit.resizeMask == 3 || hit.resizeMask == 12) ? IDC_SIZENWSE : IDC_SIZEWE));
            else SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            break;
        }
        case WM_LBUTTONUP:
            if (mode_ == Mode::Selecting) {
                selectionEnd_ = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (Distance(selectionStart_, selectionEnd_) >= 5.0f) finishSelection();
            }
            if (mode_ == Mode::DraggingCard) assignDraggedCardsToColumns();
            if (gestureCheckpointed_) document_.markChanged();
            mode_ = Mode::None; activeScene_ = activeSection_ = -1; carriedScenes_.clear(); additiveSelection_ = false; gestureCheckpointed_ = false; ReleaseCapture(); refresh(); return 0;
        case WM_RBUTTONUP: contextMenu({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}); return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) { mode_ = Mode::None; clearSelection(); ReleaseCapture(); refresh(); return 0; }
            if (wParam == VK_DELETE) {
                deleteSelectedItems();
                return 0;
            }
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void BoardCanvas::createResources() {
    if (target_ || !factory_) return;
    RECT rect{}; GetClientRect(hwnd_, &rect);
    factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                      D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN), 0, 0,
                                      D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT),
                                     D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(std::max(1L, rect.right), std::max(1L, rect.bottom)),
                                                                     D2D1_PRESENT_OPTIONS_IMMEDIATELY), &target_);
    if (!target_) return;
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.border, 0.34f), &gridMinorBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.accent, 0.46f), &gridMajorBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.panelAlt, 0.86f), &sectionFillBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.panel, 0.98f), &sectionHeaderBrush_);
    target_->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, theme_.light ? .10f : .58f), &shadowBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.panel), &panelBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.border), &borderBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.accent), &accentBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.text), &textBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.textMuted), &mutedBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.accent, 0.16f), &selectionForwardFillBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.accent, 0.95f), &selectionForwardBorderBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.accent, 0.24f), &selectionBackwardFillBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.accent, 0.95f), &selectionBackwardBorderBrush_);
    target_->CreateSolidColorBrush(Theme::D2D(theme_.panel, .88f), &diagnosticsFillBrush_);
}

void BoardCanvas::discardResources() {
    SafeRelease(diagnosticsFillBrush_);
    SafeRelease(selectionBackwardBorderBrush_); SafeRelease(selectionBackwardFillBrush_);
    SafeRelease(selectionForwardBorderBrush_); SafeRelease(selectionForwardFillBrush_);
    SafeRelease(mutedBrush_); SafeRelease(textBrush_); SafeRelease(accentBrush_); SafeRelease(borderBrush_);
    SafeRelease(panelBrush_); SafeRelease(shadowBrush_); SafeRelease(sectionHeaderBrush_); SafeRelease(sectionFillBrush_);
    SafeRelease(gridMajorBrush_); SafeRelease(gridMinorBrush_);
    SafeRelease(target_);
}

void BoardCanvas::clearTextCache() {
    for (auto& entry : sceneTextCache_) {
        SafeRelease(entry.second.bodyLayout);
        SafeRelease(entry.second.titleLayout);
    }
    sceneTextCache_.clear();
}

BoardCanvas::SceneTextCache& BoardCanvas::textCacheFor(const Scene& scene) {
    SceneTextCache& cache = sceneTextCache_[scene.id];
    const std::wstring title = std::to_wstring(scene.orderIndex + 1) + L". " + scene.title;
    std::wostringstream details;
    details << (scene.summary.empty() ? L"Описание действия." : scene.summary) << L"\n\n"
            << std::fixed << std::setprecision(1) << ScriptService::estimatedMinutes(scene.text, scene.screenplayFormats) << L" мин"
            << L"  ·  " << scene.status;
    if (!scene.reviewComments.empty()) details << L"  ·  Ревью " << scene.reviewComments.size();
    if (!scene.breakdownItems.empty()) details << L"  ·  Разбор " << scene.breakdownItems.size();
    const std::wstring body = details.str();
    if (cache.title != title || !cache.titleLayout) {
        SafeRelease(cache.titleLayout);
        cache.title = title;
        if (writeFactory_ && titleFormat_) {
            writeFactory_->CreateTextLayout(cache.title.c_str(), static_cast<UINT32>(cache.title.size()), titleFormat_,
                                            CardWidth - 52.0f, 45.0f, &cache.titleLayout);
        }
    }
    if (cache.body != body || !cache.bodyLayout) {
        SafeRelease(cache.bodyLayout);
        cache.body = body;
        if (writeFactory_ && bodyFormat_) {
            writeFactory_->CreateTextLayout(cache.body.c_str(), static_cast<UINT32>(cache.body.size()), bodyFormat_,
                                            CardWidth - 26.0f, CardHeight - 80.0f, &cache.bodyLayout);
        }
    }
    return cache;
}

void BoardCanvas::render() {
    if (renderingSuspended_ || !IsWindowVisible(hwnd_)) return;
    createResources(); if (!target_) return;
    ensurePositions();
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(Theme::D2D(theme_.background));
    const D2D1_SIZE_F size = target_->GetSize();
    drawGrid(target_, size);
    target_->SetTransform(D2D1::Matrix3x2F(camera_.zoom, 0, 0, camera_.zoom, -camera_.x * camera_.zoom, -camera_.y * camera_.zoom));
    drawSections(target_, size);
    drawCards(target_, size);
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    drawSelectionRectangle(target_);
    if (diagnostics_) drawDiagnostics(target_);
    const HRESULT result = target_->EndDraw();
    if (result == D2DERR_RECREATE_TARGET) discardResources();
    ++frames_;
    const DWORD now = GetTickCount();
    if (!fpsTick_) fpsTick_ = now;
    if (now - fpsTick_ >= 1000) { fps_ = static_cast<int>(frames_ * 1000 / std::max<DWORD>(1, now - fpsTick_)); frames_ = 0; fpsTick_ = now; }
}

void BoardCanvas::drawGrid(ID2D1RenderTarget* target, const D2D1_SIZE_F& size) {
    if (!gridMinorBrush_ || !gridMajorBrush_) return;
    float worldSpacing = 42.0f;
    float screenSpacing = worldSpacing * camera_.zoom;
    while (screenSpacing < 14.0f) { worldSpacing *= 2.0f; screenSpacing *= 2.0f; }
    while (screenSpacing > 78.0f) { worldSpacing *= 0.5f; screenSpacing *= 0.5f; }
    const float firstX = std::fmod(-camera_.x * camera_.zoom, screenSpacing);
    const float firstY = std::fmod(-camera_.y * camera_.zoom, screenSpacing);
    const long long worldX0 = static_cast<long long>(std::floor(camera_.x / worldSpacing));
    const long long worldY0 = static_cast<long long>(std::floor(camera_.y / worldSpacing));
    dotCount_ = 0;
    int xi = 0;
    for (float x = firstX - screenSpacing; x < size.width + screenSpacing; x += screenSpacing, ++xi) {
        int yi = 0;
        for (float y = firstY - screenSpacing; y < size.height + screenSpacing; y += screenSpacing, ++yi) {
            const bool isMajor = ((worldX0 + xi) % 5 == 0) && ((worldY0 + yi) % 5 == 0);
            target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), isMajor ? 1.45f : 0.85f, isMajor ? 1.45f : 0.85f),
                                isMajor ? gridMajorBrush_ : gridMinorBrush_);
            ++dotCount_;
        }
    }
}

void BoardCanvas::drawSections(ID2D1RenderTarget* target, const D2D1_SIZE_F& size) {
    if (!sectionFillBrush_ || !sectionHeaderBrush_ || !accentBrush_ || !textBrush_) return;
    const D2D1_RECT_F viewport = D2D1::RectF(camera_.x - 100 / camera_.zoom, camera_.y - 100 / camera_.zoom,
                                             camera_.x + (size.width + 100) / camera_.zoom,
                                             camera_.y + (size.height + 100) / camera_.zoom);
    for (const auto& section : document_.project().boardSections) {
        const D2D1_RECT_F r = sectionRect(section);
        if (!intersects(viewport, r)) continue;
        target->FillRoundedRectangle(D2D1::RoundedRect(r, 9, 9), sectionFillBrush_);
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left, r.top, r.right, r.top + 34), 9, 9), sectionHeaderBrush_);
        target->DrawRoundedRectangle(D2D1::RoundedRect(r, 9, 9), accentBrush_, section.selected ? 2.5f : 1.25f);
        if (sectionFormat_) target->DrawTextW(section.name.c_str(), static_cast<UINT32>(section.name.size()), sectionFormat_,
                                              D2D1::RectF(r.left + 13, r.top + 7, r.right - 38, r.top + 29), textBrush_,
                                              D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL);
        drawLock(target, D2D1::RectF(r.right - 29, r.top + 7, r.right - 9, r.top + 27), section.locked, textBrush_);
    }
}

void BoardCanvas::drawCards(ID2D1RenderTarget* target, const D2D1_SIZE_F& size) {
    const D2D1_RECT_F viewport = D2D1::RectF(camera_.x - 80 / camera_.zoom, camera_.y - 80 / camera_.zoom,
                                             camera_.x + (size.width + 80) / camera_.zoom,
                                             camera_.y + (size.height + 80) / camera_.zoom);
    if (!shadowBrush_ || !panelBrush_ || !borderBrush_ || !accentBrush_ || !textBrush_ || !mutedBrush_) return;
    visibleCards_ = 0;
    for (const auto& scene : document_.project().scenes) {
        const D2D1_RECT_F r = sceneRect(scene);
        if (!intersects(viewport, r)) continue;
        ++visibleCards_;
        target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + 6, r.top + 8, r.right + 6, r.bottom + 8), 7, 7), shadowBrush_);
        target->FillRoundedRectangle(D2D1::RoundedRect(r, 7, 7), panelBrush_);
        target->DrawRoundedRectangle(D2D1::RoundedRect(r, 7, 7), scene.selected ? accentBrush_ : scene.id==hoverSceneId_?mutedBrush_:borderBrush_, scene.selected ? 2.0f : 1.0f);
        if(scene.locked || (hoverLock_ && scene.id==hoverSceneId_))
            target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.right-37,r.top+5,r.right-5,r.top+39),5,5),sectionFillBrush_);
        SceneTextCache& cache = textCacheFor(scene);
        if (cache.titleLayout) {
            target->DrawTextLayout(D2D1::Point2F(r.left + 13, r.top + 12), cache.titleLayout, textBrush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        if (camera_.zoom >= 0.34f && cache.bodyLayout) {
            target->DrawTextLayout(D2D1::Point2F(r.left + 13, r.top + 68), cache.bodyLayout, mutedBrush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        drawLock(target, D2D1::RectF(r.right - 31, r.top + 11, r.right - 10, r.top + 32), scene.locked,
                 scene.locked ? static_cast<ID2D1Brush*>(accentBrush_) : static_cast<ID2D1Brush*>(mutedBrush_));
    }
}

void BoardCanvas::drawLock(ID2D1RenderTarget* target, const D2D1_RECT_F& r, bool locked, ID2D1Brush* brush) {
    const float unit=(r.right-r.left)/24.0f;
    auto p=[](float x,float y) { return D2D1::Point2F(x,y); };
    auto*& path=lockPaths_[locked?1:0];
    if (!path && factory_) {
        ID2D1GeometrySink* sink=nullptr;
        if(SUCCEEDED(factory_->CreatePathGeometry(&path)) && SUCCEEDED(path->Open(&sink))) {
            sink->BeginFigure(p(8,11),D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(p(8,7));
            sink->AddBezier(D2D1::BezierSegment(p(8,0),p(18,0),p(18,7)));
            sink->AddLine(p(18,locked?11:8));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if(FAILED(sink->Close())) SafeRelease(path);
        } else SafeRelease(path);
        SafeRelease(sink);
    }
    D2D1_MATRIX_3X2_F old{}; target->GetTransform(&old);
    target->SetTransform(D2D1::Matrix3x2F::Scale(unit,unit)*D2D1::Matrix3x2F::Translation(r.left,r.top)*old);
    target->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(4,11,21,23),2,2),brush,1.7f);
    if(path) target->DrawGeometry(path,brush,1.7f);
    target->FillEllipse(D2D1::Ellipse(p(12.5f,16),1.2f,1.2f),brush);
    target->DrawLine(p(12.5f,16),p(12.5f,19),brush,1.6f);
    target->SetTransform(old);
}

void BoardCanvas::drawSelectionRectangle(ID2D1RenderTarget* target) {
    if (mode_ != Mode::Selecting || Distance(selectionStart_, selectionEnd_) < 3.0f) return;
    const D2D1_RECT_F r = NormalizeRect(selectionStart_, selectionEnd_);
    const bool leftToRight = selectionEnd_.x >= selectionStart_.x;
    ID2D1SolidColorBrush* fill = leftToRight ? selectionForwardFillBrush_ : selectionBackwardFillBrush_;
    ID2D1SolidColorBrush* border = leftToRight ? selectionForwardBorderBrush_ : selectionBackwardBorderBrush_;
    if (!fill || !border) return;
    target->FillRectangle(r, fill); target->DrawRectangle(r, border, 1.2f);
}

void BoardCanvas::drawDiagnostics(ID2D1RenderTarget* target) {
    if (!diagnosticsFillBrush_ || !textBrush_) return;
    target->FillRectangle(D2D1::RectF(12, 12, 210, 116), diagnosticsFillBrush_);
    std::wostringstream value;
    value << L"FPS: " << fps_ << L"\nКарточек: " << visibleCards_ << L"\nТочек: " << dotCount_ << L"\nМасштаб: " << static_cast<int>(camera_.zoom * 100) << L"%";
    const std::wstring text = value.str();
    if (bodyFormat_) target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), bodyFormat_, D2D1::RectF(22, 20, 200, 108), textBrush_);
}

void BoardCanvas::ensurePositions() {
    bool changed = false;
    for (std::size_t i = 0; i < document_.project().scenes.size(); ++i) {
        Scene& scene = const_cast<Scene&>(document_.project().scenes[i]);
        // (-1, -1) is the legacy "not positioned yet" sentinel. Negative
        // world coordinates themselves are valid on the infinite board.
        if (std::fabs(scene.cardX + 1.0f) < 0.0001f && std::fabs(scene.cardY + 1.0f) < 0.0001f) {
            scene.cardX = 90.0f + static_cast<float>(i % 4) * 260.0f;
            scene.cardY = 120.0f + static_cast<float>(i / 4) * 185.0f;
            changed = true;
        }
    }
    if (changed) document_.markChanged();
}

void BoardCanvas::useFreeLayout() {
    Project& project = document_.editProject();
    const auto oldSize = project.boardSections.size();
    const bool hasAutomaticSections = std::any_of(project.boardSections.begin(), project.boardSections.end(), [](const BoardSection& section) {
        return section.mode == L"auto-columns" || section.mode == L"auto-timeline";
    });
    if (hasAutomaticSections) document_.checkpoint(L"Свободная раскладка карточек");
    std::erase_if(project.boardSections, [](const BoardSection& section) { return section.mode == L"auto-columns" || section.mode == L"auto-timeline"; });
    if (project.boardSections.size() != oldSize) document_.markChanged();
    if (status_) status_(L"Свободная доска");
    refresh();
}

void BoardCanvas::arrangeColumns() {
    document_.checkpoint(L"Раскладка карточек по колонкам");
    Project& project = document_.editProject();
    std::erase_if(project.boardSections, [](const BoardSection& section) { return section.mode == L"auto-columns" || section.mode == L"auto-timeline"; });
    std::vector<std::wstring> columnNames;
    std::map<std::wstring, std::vector<std::size_t>> columns;
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        Scene& scene = project.scenes[index];
        std::wstring name = utf::Trim(scene.boardColumn);
        if (name.empty()) name = utf::Trim(scene.status);
        if (name.empty()) name = L"Без колонки";
        if (!columns.contains(name)) columnNames.push_back(name);
        columns[name].push_back(index);
        scene.boardColumn = name;
    }
    float x = 70.0f;
    for (const auto& name : columnNames) {
        const auto& items = columns[name];
        BoardSection section;
        section.id = project.nextId();
        section.name = name;
        section.mode = L"auto-columns";
        section.x = x;
        section.y = 48.0f;
        section.width = CardWidth + 40.0f;
        section.height = std::max(250.0f, 82.0f + static_cast<float>(items.size()) * (CardHeight + 22.0f));
        project.boardSections.push_back(section);
        float y = section.y + 54.0f;
        for (const std::size_t index : items) {
            project.scenes[index].cardX = x + 20.0f;
            project.scenes[index].cardY = y;
            y += CardHeight + 22.0f;
        }
        x += section.width + 34.0f;
    }
    document_.markChanged();
    clearTextCache();
    fitAll();
    if (status_) status_(L"Карточки разложены по колонкам");
}

void BoardCanvas::arrangeTimelineRows() {
    document_.checkpoint(L"Раскладка карточек по порядку");
    Project& project = document_.editProject();
    std::erase_if(project.boardSections, [](const BoardSection& section) { return section.mode == L"auto-columns" || section.mode == L"auto-timeline"; });
    const float x = 70.0f;
    const float y = 58.0f;
    for (std::size_t index = 0; index < project.scenes.size(); ++index) {
        project.scenes[index].cardX = x + 24.0f + static_cast<float>(index) * (CardWidth + 28.0f);
        project.scenes[index].cardY = y + 58.0f;
    }
    if (!project.scenes.empty()) {
        BoardSection section;
        section.id = project.nextId();
        section.name = L"Порядок сцен";
        section.mode = L"auto-timeline";
        section.x = x;
        section.y = y;
        section.width = 48.0f + static_cast<float>(project.scenes.size()) * (CardWidth + 28.0f);
        section.height = CardHeight + 96.0f;
        project.boardSections.push_back(section);
    }
    document_.markChanged();
    clearTextCache();
    fitAll();
    if (status_) status_(L"Карточки выстроены по порядку сцен");
}

void BoardCanvas::fitAll() {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const float width = static_cast<float>(client.right - client.left);
    const float height = static_cast<float>(client.bottom - client.top);
    if (width < 80.0f || height < 80.0f) return;
    bool hasBounds = false;
    D2D1_RECT_F bounds{};
    auto include = [&](const D2D1_RECT_F& value) {
        if (!hasBounds) { bounds = value; hasBounds = true; return; }
        bounds.left = std::min(bounds.left, value.left); bounds.top = std::min(bounds.top, value.top);
        bounds.right = std::max(bounds.right, value.right); bounds.bottom = std::max(bounds.bottom, value.bottom);
    };
    for (const auto& section : document_.project().boardSections) include(sectionRect(section));
    for (const auto& scene : document_.project().scenes) include(sceneRect(scene));
    if (!hasBounds) { camera_ = {}; targetZoom_ = camera_.zoom; refresh(); return; }
    const float worldWidth = std::max(1.0f, bounds.right - bounds.left);
    const float worldHeight = std::max(1.0f, bounds.bottom - bounds.top);
    camera_.zoom = std::clamp(std::min((width - 80.0f) / worldWidth, (height - 80.0f) / worldHeight), 0.2f, 2.0f);
    targetZoom_ = camera_.zoom;
    camera_.x = (bounds.left + bounds.right) * 0.5f - width / (2.0f * camera_.zoom);
    camera_.y = (bounds.top + bounds.bottom) * 0.5f - height / (2.0f * camera_.zoom);
    KillTimer(hwnd_, ZoomTimer);
    zoomTick_ = 0;
    mode_ = Mode::None;
    refresh();
}

bool BoardCanvas::copySelection(ProjectClipboard& clipboard) const {
    clipboard.clear();
    for (const auto& scene : document_.project().scenes) if (scene.selected) clipboard.scenes.push_back(scene);
    for (const auto& section : document_.project().boardSections) if (section.selected) clipboard.sections.push_back(section);
    if (clipboard.scenes.empty() && clipboard.sections.empty()) return false;
    clipboard.kind = clipboard.sections.empty() ? ProjectClipboardKind::Scenes : ProjectClipboardKind::BoardItems;
    return true;
}

bool BoardCanvas::pasteSelection(const ProjectClipboard& clipboard) {
    if (clipboard.scenes.empty() && clipboard.sections.empty()) return false;
    document_.checkpoint(L"Вставка объектов на доску");
    Project& project = document_.editProject(); clearSelection();
    for (const BoardSection& source : clipboard.sections) {
        BoardSection copy = source; copy.id = project.nextId(); copy.x += 36.0f; copy.y += 36.0f; copy.selected = true;
        project.boardSections.push_back(std::move(copy));
    }
    for (const Scene& source : clipboard.scenes) {
        Scene copy = CloneSceneForProject(project, source); copy.selected = true;
        project.scenes.push_back(std::move(copy));
    }
    project.normalizeOrder(); clearTextCache(); document_.markChanged(); refresh();
    if (status_) status_(L"Объекты вставлены на доску");
    return true;
}

D2D1_POINT_2F BoardCanvas::screenToWorld(POINT p) const { return D2D1::Point2F(camera_.x + p.x / camera_.zoom, camera_.y + p.y / camera_.zoom); }
D2D1_POINT_2F BoardCanvas::worldToScreen(float x, float y) const { return D2D1::Point2F((x - camera_.x) * camera_.zoom, (y - camera_.y) * camera_.zoom); }
D2D1_RECT_F BoardCanvas::sceneRect(const Scene& s) const { return D2D1::RectF(s.cardX, s.cardY, s.cardX + CardWidth, s.cardY + CardHeight); }
D2D1_RECT_F BoardCanvas::sectionRect(const BoardSection& s) const { return D2D1::RectF(s.x, s.y, s.x + s.width, s.y + s.height); }

BoardCanvas::Hit BoardCanvas::hitTest(POINT point) const {
    const D2D1_POINT_2F p = screenToWorld(point);
    for (int i = static_cast<int>(document_.project().scenes.size()) - 1; i >= 0; --i) {
        const D2D1_RECT_F r = sceneRect(document_.project().scenes[static_cast<std::size_t>(i)]);
        if (p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom) {
            const bool lock = p.x >= r.right - 38 && p.y <= r.top + 40;
            return {i, -1, lock, 0};
        }
    }
    for (int i = static_cast<int>(document_.project().boardSections.size()) - 1; i >= 0; --i) {
        const D2D1_RECT_F r = sectionRect(document_.project().boardSections[static_cast<std::size_t>(i)]);
        const float edge = 9.0f / camera_.zoom;
        if (p.x >= r.left - edge && p.x <= r.right + edge && p.y >= r.top - edge && p.y <= r.bottom + edge) {
            int mask = 0;
            if (std::fabs(p.x - r.left) <= edge) mask |= 1;
            if (std::fabs(p.x - r.right) <= edge) mask |= 2;
            if (std::fabs(p.y - r.top) <= edge) mask |= 4;
            if (std::fabs(p.y - r.bottom) <= edge) mask |= 8;
            const bool lock = p.x >= r.right - 38 && p.y >= r.top && p.y <= r.top + 36;
            if (mask || (p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.top + 36)) return {-1, i, lock, mask};
        }
    }
    return {};
}

void BoardCanvas::clearSelection() {
    for (auto& item : const_cast<Project&>(document_.project()).scenes) item.selected = false;
    for (auto& item : const_cast<Project&>(document_.project()).boardSections) item.selected = false;
}

void BoardCanvas::selectOnlyScene(int index) {
    clearSelection();
    if (index >= 0 && index < static_cast<int>(document_.project().scenes.size())) {
        auto& scene = const_cast<Project&>(document_.project()).scenes[static_cast<std::size_t>(index)]; scene.selected = true;
        if (settings_.get().keepScriptCardsSync && settings_.get().selectMatchingScene)
            document_.setSelectedScene(static_cast<std::size_t>(index));
    }
}

void BoardCanvas::selectOnlySection(int index) {
    clearSelection();
    if (index >= 0 && index < static_cast<int>(document_.project().boardSections.size()))
        const_cast<Project&>(document_.project()).boardSections[static_cast<std::size_t>(index)].selected = true;
}

void BoardCanvas::beginSectionMove(int index, D2D1_POINT_2F) {
    carriedScenes_.clear();
    const auto& section = document_.project().boardSections[static_cast<std::size_t>(index)];
    const D2D1_RECT_F sr = sectionRect(section);
    for (std::size_t i = 0; i < document_.project().scenes.size(); ++i) {
        const D2D1_RECT_F cr = sceneRect(document_.project().scenes[i]);
        const float cx = (cr.left + cr.right) * 0.5f, cy = (cr.top + cr.bottom) * 0.5f;
        if (cx >= sr.left && cx <= sr.right && cy >= sr.top && cy <= sr.bottom)
            carriedScenes_[static_cast<int>(i)] = D2D1::Point2F(document_.project().scenes[i].cardX, document_.project().scenes[i].cardY);
    }
}

void BoardCanvas::moveActive(D2D1_POINT_2F world) {
    const float dx = world.x - dragWorldStart_.x, dy = world.y - dragWorldStart_.y;
    Project& project = const_cast<Project&>(document_.project());
    if (mode_ == Mode::DraggingCard) {
        for (std::size_t i = 0; i < project.scenes.size() && i < sceneStartPositions_.size(); ++i) {
            if (project.scenes[i].selected && !project.scenes[i].locked) {
                project.scenes[i].cardX = sceneStartPositions_[i].x + dx; project.scenes[i].cardY = sceneStartPositions_[i].y + dy;
            }
        }
    } else if (activeSection_ >= 0 && activeSection_ < static_cast<int>(project.boardSections.size())) {
        BoardSection& section = project.boardSections[static_cast<std::size_t>(activeSection_)];
        if (mode_ == Mode::DraggingSection) {
            section.x = originalSection_.left + dx; section.y = originalSection_.top + dy;
            for (const auto& entry : carriedScenes_) {
                Scene& scene = project.scenes[static_cast<std::size_t>(entry.first)];
                scene.cardX = entry.second.x + dx; scene.cardY = entry.second.y + dy;
            }
        } else if (mode_ == Mode::ResizingSection) {
            D2D1_RECT_F r = originalSection_;
            if (resizeMask_ & 1) r.left += dx; if (resizeMask_ & 2) r.right += dx;
            if (resizeMask_ & 4) r.top += dy; if (resizeMask_ & 8) r.bottom += dy;
            if (r.right - r.left >= 260) { section.x = r.left; section.width = r.right - r.left; }
            if (r.bottom - r.top >= 150) { section.y = r.top; section.height = r.bottom - r.top; }
        }
    }
}

void BoardCanvas::finishSelection() {
    const D2D1_POINT_2F a = screenToWorld(selectionStart_), b = screenToWorld(selectionEnd_);
    const D2D1_RECT_F box = D2D1::RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
    const bool leftToRight = selectionEnd_.x >= selectionStart_.x;
    Project& project = const_cast<Project&>(document_.project());
    for (auto& scene : project.scenes) {
        const bool hit = leftToRight ? intersects(box, sceneRect(scene)) : contains(box, sceneRect(scene));
        scene.selected = additiveSelection_ ? (scene.selected || hit) : hit;
    }
    for (auto& section : project.boardSections) {
        const bool hit = leftToRight ? intersects(box, sectionRect(section)) : contains(box, sectionRect(section));
        section.selected = additiveSelection_ ? (section.selected || hit) : hit;
    }
}

void BoardCanvas::assignDraggedCardsToColumns() {
    Project& project = document_.editProject();
    for (auto& scene : project.scenes) {
        if (!scene.selected) continue;
        const D2D1_RECT_F card = sceneRect(scene);
        const float centerX = (card.left + card.right) * 0.5f;
        const float centerY = (card.top + card.bottom) * 0.5f;
        for (const auto& section : project.boardSections) {
            if (section.mode != L"auto-columns") continue;
            const D2D1_RECT_F area = sectionRect(section);
            if (centerX >= area.left && centerX <= area.right && centerY >= area.top && centerY <= area.bottom) {
                scene.boardColumn = section.name;
                break;
            }
        }
    }
}

void BoardCanvas::contextMenu(POINT point) {
    const Hit hit = hitTest(point);
    HMENU menu = CreatePopupMenu();
    if (hit.scene < 0 && hit.section < 0) AppendMenuW(menu, MF_STRING, MenuNewSection, L"Создать раздел");
    else {
        if (hit.section >= 0) AppendMenuW(menu, MF_STRING, MenuRename, L"Переименовать раздел");
        if (hit.scene >= 0) {
            AppendMenuW(menu, MF_STRING, MenuOpenScene, L"Открыть в сценарии");
            SetMenuDefaultItem(menu, MenuOpenScene, FALSE);
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, MenuRenameScene, L"Переименовать сцену");
            AppendMenuW(menu, MF_STRING, MenuEditSummary, L"Изменить синопсис карточки");
            AppendMenuW(menu, MF_STRING, MenuDuplicateScene, L"Дублировать сцену");
            AppendMenuW(menu, document_.project().scenes.size() > 1 && hit.scene > 0 ? MF_STRING : MF_STRING | MF_GRAYED, MenuMoveUp, L"Переместить сцену выше");
            AppendMenuW(menu, document_.project().scenes.size() > 1 && hit.scene + 1 < static_cast<int>(document_.project().scenes.size()) ? MF_STRING : MF_STRING | MF_GRAYED,
                        MenuMoveDown, L"Переместить сцену ниже");
        }
        const bool locked = hit.scene >= 0 ? document_.project().scenes[static_cast<std::size_t>(hit.scene)].locked
                                           : document_.project().boardSections[static_cast<std::size_t>(hit.section)].locked;
        AppendMenuW(menu, MF_STRING, MenuLock, locked ? L"Разблокировать" : L"Заблокировать");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); AppendMenuW(menu, MF_STRING, MenuDelete, L"Удалить");
    }
    POINT screen = point; ClientToScreen(hwnd_, &screen);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == MenuNewSection) {
        document_.checkpoint(L"Создание раздела карточек");
        const D2D1_POINT_2F world = screenToWorld(point); BoardSection section; section.id = document_.project().nextId();
        section.name = L"Новый раздел"; section.x = world.x; section.y = world.y; document_.editProject().boardSections.push_back(section); document_.markChanged();
        if (status_) status_(L"Раздел создан");
    } else if (command == MenuOpenScene && hit.scene >= 0) openScene(hit.scene);
    else if (command == MenuRename && hit.section >= 0) renameSection(hit.section);
    else if (command == MenuRenameScene && hit.scene >= 0) {
        const Scene& scene = document_.project().scenes[static_cast<std::size_t>(hit.scene)];
        const std::wstring name = PromptText(hwnd_, L"Переименовать сцену", L"Название", scene.title, theme_);
        if (!utf::Trim(name).empty()) {
            document_.setSelectedScene(static_cast<std::size_t>(hit.scene));
            document_.renameSelectedScene(name);
        }
    }
    else if (command == MenuEditSummary && hit.scene >= 0) editSceneSummary(hit.scene);
    else if (command == MenuDuplicateScene && hit.scene >= 0) {
        document_.duplicateScene(static_cast<std::size_t>(hit.scene));
        if (status_) status_(L"Сцена продублирована");
    }
    else if (command == MenuMoveUp && hit.scene >= 0) document_.moveScene(static_cast<std::size_t>(hit.scene), -1);
    else if (command == MenuMoveDown && hit.scene >= 0) document_.moveScene(static_cast<std::size_t>(hit.scene), 1);
    else if (command == MenuLock) {
        if (hit.scene >= 0) { document_.checkpoint(L"Блокировка карточки"); auto& item = document_.editProject().scenes[static_cast<std::size_t>(hit.scene)]; item.locked = !item.locked; document_.markChanged(); }
        else if (hit.section >= 0) { document_.checkpoint(L"Блокировка раздела"); auto& item = document_.editProject().boardSections[static_cast<std::size_t>(hit.section)]; item.locked = !item.locked; document_.markChanged(); }
    } else if (command == MenuDelete) {
        if (hit.scene >= 0) deleteScene(hit.scene); else if (hit.section >= 0) deleteSection(hit.section);
    }
    refresh();
}

void BoardCanvas::openScene(int index) {
    if (index < 0 || index >= static_cast<int>(document_.project().scenes.size())) return;
    document_.setSelectedScene(static_cast<std::size_t>(index));
    if (openSceneAction_) openSceneAction_();
}

void BoardCanvas::editSceneSummary(int index) {
    if (index < 0 || index >= static_cast<int>(document_.project().scenes.size())) return;
    const Scene& scene = document_.project().scenes[static_cast<std::size_t>(index)];
    const std::wstring value = PromptText(hwnd_, L"Синопсис карточки", L"Кратко опишите содержание сцены", scene.summary, theme_);
    if (value.empty() || value == scene.summary) return;
    document_.checkpoint(L"Изменение синопсиса карточки");
    Scene& editable = document_.editProject().scenes[static_cast<std::size_t>(index)];
    editable.summary = value;
    editable.summaryManual = true;
    document_.markChanged();
    if (status_) status_(L"Синопсис карточки обновлен");
}

void BoardCanvas::deleteScene(int index, bool askConfirmation) {
    if (index < 0 || index >= static_cast<int>(document_.project().scenes.size()) || document_.project().scenes.size() <= 1) return;
    if (askConfirmation && settings_.get().confirmDeleteCard &&
        MessageBoxW(hwnd_, L"Удалить выбранную карточку сцены?", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.deleteScene(static_cast<std::size_t>(index));
    refresh();
}

void BoardCanvas::deleteSection(int index, bool askConfirmation) {
    if (index < 0 || index >= static_cast<int>(document_.project().boardSections.size())) return;
    if (askConfirmation && settings_.get().confirmDeleteCard &&
        MessageBoxW(hwnd_, L"Удалить выбранный раздел? Карточки сцен останутся на доске.", L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    document_.checkpoint(L"Удаление раздела карточек");
    Project& project = document_.editProject();
    project.boardSections.erase(project.boardSections.begin() + index);
    document_.markChanged();
    refresh();
}

void BoardCanvas::deleteSelectedItems() {
    int selectedCount = 0;
    for (const auto& scene : document_.project().scenes) if (scene.selected) ++selectedCount;
    for (const auto& section : document_.project().boardSections) if (section.selected) ++selectedCount;
    if (!selectedCount) return;
    if (settings_.get().confirmDeleteCard &&
        MessageBoxW(hwnd_, selectedCount == 1 ? L"Удалить выбранный объект?" : L"Удалить выбранные объекты?",
                    L"Mezozoy", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    document_.checkpoint(L"Удаление объектов с доски");
    Project& project = document_.editProject();
    std::erase_if(project.boardSections, [](const BoardSection& section) { return section.selected; });
    for (int i = static_cast<int>(project.scenes.size()) - 1; i >= 0 && project.scenes.size() > 1; --i) {
        if (project.scenes[static_cast<std::size_t>(i)].selected) project.scenes.erase(project.scenes.begin() + i);
    }
    project.normalizeOrder();
    const std::size_t selected = project.scenes.empty() ? 0 : std::min(document_.selectedScene(), project.scenes.size() - 1);
    clearTextCache(); document_.markChanged();
    if (!project.scenes.empty()) document_.setSelectedScene(selected);
    refresh();
}

void BoardCanvas::renameSection(int index) {
    if (index < 0 || index >= static_cast<int>(document_.project().boardSections.size())) return;
    const std::wstring name = PromptText(hwnd_, L"Переименовать раздел", L"Название", document_.project().boardSections[static_cast<std::size_t>(index)].name, theme_);
    if (!name.empty() && name != document_.project().boardSections[static_cast<std::size_t>(index)].name) {
        document_.checkpoint(L"Переименование раздела"); document_.editProject().boardSections[static_cast<std::size_t>(index)].name = name; document_.markChanged(); refresh();
    }
}

bool BoardCanvas::contains(const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return b.left >= a.left && b.top >= a.top && b.right <= a.right && b.bottom <= a.bottom; }
bool BoardCanvas::intersects(const D2D1_RECT_F& a, const D2D1_RECT_F& b) { return a.left <= b.right && a.right >= b.left && a.top <= b.bottom && a.bottom >= b.top; }

}  // namespace mezozoy::ui
