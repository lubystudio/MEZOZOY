#pragma once

#include "Theme.h"
#include "../services/RecoveryService.h"

#include <filesystem>
#include <windows.h>

namespace mezozoy::ui {

struct RecoveryDialogResult {
    bool accepted = false;
    bool recovered = false;
    std::filesystem::path selectedPath;
};

RecoveryDialogResult ShowRecoveryDialog(HWND owner, const RecoveryScan& scan, const Theme& theme);

}  // namespace mezozoy::ui
