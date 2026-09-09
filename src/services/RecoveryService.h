#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mezozoy {

enum class RecoverySource {
    Original,
    Autosave,
    AtomicBackup,
    Backup
};

struct RecoveryCandidate {
    std::filesystem::path path;
    RecoverySource source = RecoverySource::Original;
    bool valid = false;
    std::wstring error;
    std::wstring projectTitle;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};
};

struct RecoveryScan {
    std::filesystem::path originalPath;
    std::vector<RecoveryCandidate> candidates;
    bool originalValid = false;
    bool needsRecovery = false;
    std::size_t recommendedIndex = 0;
};

class RecoveryService {
public:
    RecoveryScan scan(const std::filesystem::path& originalPath) const;

    static std::filesystem::path AutosavePath(const std::filesystem::path& originalPath);
    static std::filesystem::path AtomicBackupPath(const std::filesystem::path& originalPath);
    static std::wstring SourceLabel(RecoverySource source);
};

}  // namespace mezozoy
