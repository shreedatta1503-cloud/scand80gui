// ----------------------------------------------------------------------------
// Installer — applies a component's install action (copy / extract / run-silent)
// into the install directory, recording an in-memory rollback journal so a
// failed repair can be undone. Files are placed atomically (download to staging,
// back up any file being overwritten, then move into place); on failure the
// journal is replayed in reverse to restore the previous state.
// ----------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include <string>
#include <vector>

namespace qgc {

class Installer {
public:
    Installer(const std::wstring &installDir, const std::wstring &stagingDir);

    // Installs one component given the already-downloaded+verified artifact.
    // Returns true on success. On failure the partial work for THIS component is
    // left for rollbackAll() to undo (call it on the orchestrator's first failure).
    bool installComponent(const Component &component, const std::wstring &artifactPath);

    // Undo everything installed so far (reverse order). Best effort.
    void rollbackAll();

    // Discard backups / journal after a fully successful run.
    void commit();

private:
    struct JournalEntry {
        enum class Kind { DeleteOnRollback, RestoreBackup } kind;
        std::wstring target; // file that was created or overwritten
        std::wstring backup; // saved copy of the overwritten original (RestoreBackup)
    };

    bool placeFile(const std::wstring &srcFile, const std::wstring &destFile);
    bool installTree(const std::wstring &srcDir, const std::wstring &destDir);
    std::wstring uniqueBackupPath();

    std::wstring m_installDir;
    std::wstring m_stagingDir;
    std::vector<JournalEntry> m_journal;
    unsigned m_backupCounter = 0;
};

} // namespace qgc
