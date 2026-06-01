#include "Installer.h"
#include "Common.h"
#include "Logger.h"
#include "Zip.h"
#include "Elevation.h"

#include <windows.h>

namespace qgc {

Installer::Installer(const std::wstring &installDir, const std::wstring &stagingDir)
    : m_installDir(installDir), m_stagingDir(stagingDir)
{
    ensureDir(m_stagingDir);
}

std::wstring Installer::uniqueBackupPath()
{
    return joinPath(m_stagingDir, L"bak_" + std::to_wstring(++m_backupCounter) + L".tmp");
}

bool Installer::placeFile(const std::wstring &srcFile, const std::wstring &destFile)
{
    // Containment guard: never write outside the install directory.
    if (!isContainedWithin(m_installDir, destFile)) {
        Logger::error(L"install: refusing to write outside install dir: " + destFile);
        return false;
    }

    const std::wstring parent = parentDir(destFile);
    if (!ensureDir(parent)) {
        Logger::error(L"install: cannot create dir " + parent);
        return false;
    }

    if (fileExists(destFile)) {
        // Back up the original so we can restore on rollback.
        const std::wstring backup = uniqueBackupPath();
        if (!::MoveFileExW(destFile.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            Logger::error(L"install: cannot back up " + destFile);
            return false;
        }
        m_journal.push_back({JournalEntry::Kind::RestoreBackup, destFile, backup});
    } else {
        m_journal.push_back({JournalEntry::Kind::DeleteOnRollback, destFile, L""});
    }

    if (!::CopyFileW(srcFile.c_str(), destFile.c_str(), FALSE)) {
        Logger::error(L"install: copy failed -> " + destFile);
        return false;
    }
    return true;
}

bool Installer::installTree(const std::wstring &srcDir, const std::wstring &destDir)
{
    const std::wstring pattern = joinPath(srcDir, L"*");
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return true; // empty
    }
    bool ok = true;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") {
            continue;
        }
        const std::wstring src = joinPath(srcDir, name);
        const std::wstring dst = joinPath(destDir, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            ok = installTree(src, dst) && ok;
        } else {
            ok = placeFile(src, dst) && ok;
        }
        if (!ok) {
            break;
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    return ok;
}

bool Installer::installComponent(const Component &component, const std::wstring &artifactPath)
{
    Logger::info(L"install: applying component " + component.name);

    switch (component.install.action) {
    case InstallAction::Copy: {
        const std::wstring dest = component.install.dest.empty()
                                      ? joinPath(m_installDir, fileName(artifactPath))
                                      : joinPath(m_installDir, normalizeRelative(component.install.dest));
        return placeFile(artifactPath, dest);
    }
    case InstallAction::Extract: {
        // Extract to an isolated staging subdir, then move into place with a
        // journal so each placed file can be rolled back.
        const std::wstring extractDir = joinPath(m_stagingDir, L"x_" + component.name);
        removeTree(extractDir);
        if (!extractArchive(artifactPath, extractDir)) {
            return false;
        }
        const std::wstring destDir = component.install.dest.empty()
                                         ? m_installDir
                                         : joinPath(m_installDir, normalizeRelative(component.install.dest));
        const bool ok = installTree(extractDir, destDir);
        removeTree(extractDir);
        return ok;
    }
    case InstallAction::RunSilent: {
        // e.g. vc_redist.x64.exe /install /quiet /norestart (may require UAC).
        if (component.verifyAuthenticode &&
            !verifyAuthenticode(artifactPath, component.publisher)) {
            Logger::error(L"install: Authenticode verification failed for " + component.name);
            return false;
        }
        const int code = runProcess(artifactPath, component.install.args, component.install.elevate);
        // Common redist success codes: 0 (ok), 3010 (ok, reboot required), 1638 (newer already installed).
        if (code == 0 || code == 3010 || code == 1638) {
            Logger::info(L"install: " + component.name + L" run-silent exit " + std::to_wstring(code));
            return true;
        }
        Logger::error(L"install: " + component.name + L" run-silent failed exit " + std::to_wstring(code));
        return false;
    }
    case InstallAction::Unknown:
    default:
        Logger::error(L"install: unknown install action for " + component.name);
        return false;
    }
}

void Installer::rollbackAll()
{
    Logger::warning(L"install: rolling back " + std::to_wstring(m_journal.size()) + L" change(s)");
    for (auto it = m_journal.rbegin(); it != m_journal.rend(); ++it) {
        if (it->kind == JournalEntry::Kind::DeleteOnRollback) {
            ::DeleteFileW(it->target.c_str());
        } else { // RestoreBackup
            ::DeleteFileW(it->target.c_str());
            ::MoveFileExW(it->backup.c_str(), it->target.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    m_journal.clear();
}

void Installer::commit()
{
    for (const auto &entry : m_journal) {
        if (entry.kind == JournalEntry::Kind::RestoreBackup) {
            ::DeleteFileW(entry.backup.c_str());
        }
    }
    m_journal.clear();
}

} // namespace qgc
