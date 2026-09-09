#include "RecoveryService.h"

#include "ProjectSerializer.h"

#include <algorithm>
#include <cwctype>
#include <unordered_set>

namespace mezozoy {
namespace {

std::wstring PathKey(const std::filesystem::path& path) {
    std::wstring key = path.lexically_normal().wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return key;
}

RecoveryCandidate Inspect(const std::filesystem::path& path, RecoverySource source, const ProjectSerializer& serializer) {
    RecoveryCandidate candidate;
    candidate.path = path;
    candidate.source = source;

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        candidate.error = L"Файл не найден.";
        return candidate;
    }

    candidate.size = std::filesystem::file_size(path, ec);
    if (ec) candidate.size = 0;
    ec.clear();
    candidate.modified = std::filesystem::last_write_time(path, ec);
    if (ec) candidate.modified = {};

    Project project;
    candidate.valid = serializer.load(path, project, &candidate.error);
    if (candidate.valid) {
        candidate.projectTitle = project.title.empty() ? L"Без названия" : project.title;
        candidate.error.clear();
    }
    return candidate;
}

bool Newer(const RecoveryCandidate& left, const RecoveryCandidate& right) {
    return left.modified > right.modified;
}

}  // namespace

std::filesystem::path RecoveryService::AutosavePath(const std::filesystem::path& originalPath) {
    return originalPath.parent_path() / (originalPath.stem().wstring() + L".autosave.mzoy");
}

std::filesystem::path RecoveryService::AtomicBackupPath(const std::filesystem::path& originalPath) {
    return std::filesystem::path(originalPath.wstring() + L".bak");
}

std::wstring RecoveryService::SourceLabel(RecoverySource source) {
    switch (source) {
        case RecoverySource::Original: return L"Исходный проект";
        case RecoverySource::Autosave: return L"Автосохранение";
        case RecoverySource::AtomicBackup: return L"Атомарная копия";
        case RecoverySource::Backup: return L"Резервная копия";
    }
    return L"Копия проекта";
}

RecoveryScan RecoveryService::scan(const std::filesystem::path& originalPath) const {
    RecoveryScan result;
    result.originalPath = originalPath;

    ProjectSerializer serializer;
    std::unordered_set<std::wstring> seen;
    auto add = [&](const std::filesystem::path& path, RecoverySource source, bool includeMissing = false) {
        const std::wstring key = PathKey(path);
        if (!seen.insert(key).second) return;
        std::error_code ec;
        if (!includeMissing && (!std::filesystem::exists(path, ec) || ec)) return;
        result.candidates.push_back(Inspect(path, source, serializer));
    };

    add(originalPath, RecoverySource::Original, true);
    add(AutosavePath(originalPath), RecoverySource::Autosave);

    // Archived copies can be numerous and may contain embedded posters. They
    // are only parsed when the original itself cannot be opened; a healthy
    // project with a newer autosave only needs those two candidates.
    const bool originalLooksValid = !result.candidates.empty() && result.candidates.front().valid;
    if (!originalLooksValid) {
        add(AtomicBackupPath(originalPath), RecoverySource::AtomicBackup);

        const std::filesystem::path backupDirectory = originalPath.parent_path() / L"Mezozoy Backups";
        const std::wstring prefix = originalPath.stem().wstring() + L"_";
        std::error_code ec;
        if (std::filesystem::exists(backupDirectory, ec) && !ec) {
            for (const auto& entry : std::filesystem::directory_iterator(backupDirectory, ec)) {
                if (ec) break;
                if (!entry.is_regular_file(ec) || ec) { ec.clear(); continue; }
                const std::filesystem::path candidate = entry.path();
                const std::wstring name = candidate.filename().wstring();
                if (_wcsicmp(candidate.extension().c_str(), L".mzoy") == 0 && name.starts_with(prefix))
                    add(candidate, RecoverySource::Backup);
            }
        }
    }

    result.originalValid = !result.candidates.empty() && result.candidates.front().valid;
    result.recommendedIndex = 0;

    if (!result.originalValid) {
        bool found = false;
        for (std::size_t index = 1; index < result.candidates.size(); ++index) {
            const auto& candidate = result.candidates[index];
            if (!candidate.valid) continue;
            if (!found || Newer(candidate, result.candidates[result.recommendedIndex])) {
                result.recommendedIndex = index;
                found = true;
            }
        }
        result.needsRecovery = found;
    } else {
        for (std::size_t index = 1; index < result.candidates.size(); ++index) {
            const auto& candidate = result.candidates[index];
            if (candidate.source != RecoverySource::Autosave || !candidate.valid) continue;
            if (Newer(candidate, result.candidates.front())) {
                result.recommendedIndex = index;
                result.needsRecovery = true;
            }
        }
    }

    std::stable_sort(result.candidates.begin() + std::min<std::size_t>(1, result.candidates.size()), result.candidates.end(),
                     [](const RecoveryCandidate& left, const RecoveryCandidate& right) {
                         if (left.valid != right.valid) return left.valid > right.valid;
                         return left.modified > right.modified;
                     });

    if (result.needsRecovery) {
        const std::filesystem::path recommendedPath = result.candidates[result.recommendedIndex].path;
        for (std::size_t index = 0; index < result.candidates.size(); ++index) {
            if (PathKey(result.candidates[index].path) == PathKey(recommendedPath)) {
                result.recommendedIndex = index;
                break;
            }
        }
    }
    return result;
}

}  // namespace mezozoy
