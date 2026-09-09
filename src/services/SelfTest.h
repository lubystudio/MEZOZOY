#pragma once

#include <filesystem>

namespace mezozoy {

int RunSelfTests(const std::filesystem::path& compatibilityProject = {});

}  // namespace mezozoy
