#pragma once

#include <windows.h>
#include <d2d1.h>

#include <string>
#include <cmath>

namespace mezozoy::ui {

struct Theme {
    bool light = false;
    COLORREF background = RGB(15, 16, 18);
    COLORREF navigation = RGB(20, 21, 24);
    COLORREF panel = RGB(25, 26, 30);
    COLORREF panelAlt = RGB(32, 33, 38);
    COLORREF page = RGB(18, 19, 22);
    COLORREF border = RGB(55, 57, 64);
    COLORREF text = RGB(240, 241, 244);
    COLORREF textMuted = RGB(166, 169, 178);
    COLORREF accent = RGB(139, 92, 246);
    COLORREF danger = RGB(239, 68, 68);
    COLORREF selection = RGB(64, 45, 91);

    static double Contrast(COLORREF first, COLORREF second) {
        const auto luminance = [](COLORREF color) {
            const auto linear = [](BYTE channel) {
                const double c = channel / 255.0;
                return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
            };
            return .2126 * linear(GetRValue(color)) + .7152 * linear(GetGValue(color)) + .0722 * linear(GetBValue(color));
        };
        const double a = luminance(first), b = luminance(second);
        return a > b ? (a + .05) / (b + .05) : (b + .05) / (a + .05);
    }

    static Theme FromMode(const std::wstring& mode, COLORREF accentColor = RGB(139, 92, 246)) {
        Theme result;
        if (mode == L"Darker") {
            result.background = RGB(8, 9, 11); result.navigation = RGB(13, 14, 16);
            result.panel = RGB(18, 19, 22); result.panelAlt = RGB(25, 26, 30);
            result.page = RGB(12, 13, 15); result.border = RGB(46, 48, 54);
            result.text = RGB(238, 239, 242); result.textMuted = RGB(151, 154, 163);
        } else if (mode == L"Light") {
            result.light = true;
            result.background = RGB(244, 244, 242); result.navigation = RGB(235, 236, 234);
            result.panel = RGB(250, 250, 248); result.panelAlt = RGB(229, 231, 228);
            result.page = RGB(255, 255, 253); result.border = RGB(177, 182, 177);
            result.text = RGB(32, 36, 34); result.textMuted = RGB(88, 96, 91);
            result.danger = RGB(181, 38, 48);
        }
        result.selection = Blend(result.panelAlt, accentColor, result.light ? 16 : 34);
        result.accent = accentColor;
        // Retain the chosen hue; bright accents need darker ink on light surfaces.
        for (unsigned int amount = 5; result.light && amount <= 100 &&
             Contrast(result.accent, result.panelAlt) < 4.5; amount += 5)
            result.accent = Blend(accentColor, result.text, amount);
        return result;
    }

    static COLORREF Blend(COLORREF base, COLORREF overlay, unsigned int overlayPercent) {
        overlayPercent = overlayPercent > 100 ? 100 : overlayPercent;
        const unsigned int basePercent = 100 - overlayPercent;
        const auto channel = [basePercent, overlayPercent](BYTE baseValue, BYTE overlayValue) {
            return static_cast<BYTE>((baseValue * basePercent + overlayValue * overlayPercent + 50) / 100);
        };
        return RGB(channel(GetRValue(base), GetRValue(overlay)),
                   channel(GetGValue(base), GetGValue(overlay)),
                   channel(GetBValue(base), GetBValue(overlay)));
    }

    static COLORREF ParseHex(const std::wstring& value, COLORREF fallback = RGB(139, 92, 246)) {
        std::wstring text = value;
        if (!text.empty() && text.front() == L'#') text.erase(text.begin());
        if (text.size() != 6) return fallback;
        try {
            const unsigned long rgb = std::stoul(text, nullptr, 16);
            return RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
        } catch (...) { return fallback; }
    }

    static D2D1_COLOR_F D2D(COLORREF color, float alpha = 1.0f) {
        return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f, alpha);
    }
};

}  // namespace mezozoy::ui
