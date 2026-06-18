// ----------------------------------------------------------------------------
// Orchestrator — the bootstrap flow:
//   load manifest -> (optional) refresh from remote -> detect missing
//   -> download+verify+install missing -> launch QGroundControlApp.exe.
// Implements the offline/graceful-failure decision tree and rollback on error.
// ----------------------------------------------------------------------------
#pragma once

#include "Config.h"
#include <string>

namespace qgc {

class SplashWindow;

class Orchestrator {
public:
    explicit Orchestrator(SplashWindow &splash);

    // Runs the full bootstrap and launches the app. Returns a process exit code
    // (0 = app launched; non-zero = fatal failure, error already shown).
    int run();

private:
    enum class Outcome { Launch, FatalError };

    bool loadManifests(Manifest &manifest);
    bool tryRefreshRemoteManifest(Manifest &manifest);

    // Repairs the given missing components. Returns false on a fatal failure of
    // a *required* component (rollback already performed).
    bool repairComponents(const Manifest &manifest, const std::vector<const Component *> &missing);

    SplashWindow &m_splash;
    std::wstring  m_installDir;
    std::wstring  m_stagingDir;
};

} // namespace qgc
