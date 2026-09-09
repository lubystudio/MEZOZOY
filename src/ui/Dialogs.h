#pragma once

#include <windows.h>
#include <string>

namespace mezozoy::ui {
struct Theme;

std::wstring PromptText(HWND owner, const std::wstring& title, const std::wstring& label, const std::wstring& initial, const Theme& theme);

}  // namespace mezozoy::ui
