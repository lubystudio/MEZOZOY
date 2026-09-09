#pragma once

#include "Theme.h"

#include <windows.h>
#include <commctrl.h>

#include <vector>

namespace mezozoy::ui {

class Page {
public:
    virtual ~Page() = default;
    virtual HWND hwnd() const = 0;
    virtual void layout(int width, int height) = 0;
    virtual void layoutAt(int x, int y, int width, int height) = 0;
    virtual void invalidateLayout() {}
    virtual void commit() {}
    virtual bool handleCommand(int, int, HWND) { return false; }
    virtual bool handleNotify(NMHDR*) { return false; }
    virtual void applyAppearance(const Theme&, float) {}
    virtual void setHostInteractionPaused(bool) {}
};

class NativePage : public Page {
public:
    NativePage(HINSTANCE instance, HWND parent, const Theme& theme);
    ~NativePage() override;

    HWND hwnd() const override { return hwnd_; }
    void layout(int width, int height) override;
    void layoutAt(int x, int y, int width, int height) override;
    void invalidateLayout() override { layoutValid_ = false; }
    void applyAppearance(const Theme& theme, float uiScale) override;
    virtual void onLayout(int width, int height) = 0;
    virtual LRESULT onMessage(UINT, WPARAM, LPARAM) { return 0; }
    void show(bool visible);

protected:
    HINSTANCE instance_{};
    HWND parent_{};
    HWND hwnd_{};
    Theme theme_;
    HFONT uiFont_{};
    HFONT uiFontBold_{};
    HBRUSH backgroundBrush_{};
    HBRUSH panelBrush_{};
    HBRUSH editBrush_{};
    std::vector<HWND> controls_;
    std::vector<HWND> boldControls_;
    float uiScale_ = 1.0f;

    void remember(HWND control, bool bold = false);
    virtual HBRUSH controlColor(HDC dc, HWND control, UINT message);
    virtual bool drawCustomItem(DRAWITEMSTRUCT*) { return false; }

private:
    int layoutX_ = 0;
    int layoutY_ = 0;
    int layoutWidth_ = -1;
    int layoutHeight_ = -1;
    bool layoutValid_ = false;
    void drawButton(DRAWITEMSTRUCT* draw);
    static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
};

}  // namespace mezozoy::ui
