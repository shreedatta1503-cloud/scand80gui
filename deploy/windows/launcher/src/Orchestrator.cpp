#include "Orchestrator.h"
#include "Common.h"
#include "Logger.h"
#include "Config.h"
#include "Detector.h"
#include "Downloader.h"
#include "Elevation.h"
#include "Hash.h"
#include "Installer.h"
#include "ProcessLauncher.h"
#include "SplashWindow.h"

#include <windows.h>

namespace qgc {

namespace {
constexpr wchar_t kManifestFileName[] = L"qgc-bootstrap.json";
constexpr wchar_t kStagingDirName[]   = L".qgc-bootstrap-staging";

std::wstring supportUrlHint(const Manifest &manifest)
{
    return manifest.mirrors.empty() ? L"(no download source configured)" : manifest.mirrors.front();
}
} // namespace

Orchestrator::Orchestrator(SplashWindow &splash) : m_splash(splash)
{
    m_installDir = exeDir();
    m_stagingDir = joinPath(m_installDir, kStagingDirName);
}

bool Orchestrator::loadManifests(Manifest &manifest)
{
    const std::wstring localPath = joinPath(m_installDir, kManifestFileName);
    std::wstring error;
    if (!loadManifestFile(localPath, manifest, error)) {
        Logger::warning(L"manifest: " + error);
        return false;
    }
    Logger::info(L"manifest: loaded local " + manifest.manifestVersion +
                 L" with " + std::to_wstring(manifest.components.size()) + L" component(s)");

    // Optionally refresh from the remote manifest authorized by the local file.
    if (!manifest.manifestUrl.empty()) {
        tryRefreshRemoteManifest(manifest);
    }
    return true;
}

bool Orchestrator::tryRefreshRemoteManifest(Manifest &manifest)
{
    if (!hasInternetConnection()) {
        Logger::info(L"manifest: offline, using local manifest");
        return false;
    }
    m_splash.setStatus(L"Checking for updates…");
    ensureDir(m_stagingDir);
    const std::wstring tmp = joinPath(m_stagingDir, L"remote-manifest.json");

    const DownloadResult dr = downloadToFile(manifest.manifestUrl, tmp, nullptr, nullptr);
    if (dr != DownloadResult::Success) {
        Logger::warning(L"manifest: remote fetch failed, using local");
        return false;
    }

    Manifest remote;
    std::wstring error;
    if (!loadManifestFile(tmp, remote, error)) {
        Logger::warning(L"manifest: remote parse failed (" + error + L"), using local");
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    ::DeleteFileW(tmp.c_str());

    // Only adopt the remote manifest if it is newer (string compare of the
    // version field is sufficient for our YYYY.MM.DD scheme).
    if (remote.manifestVersion > manifest.manifestVersion) {
        Logger::info(L"manifest: adopting remote " + remote.manifestVersion);
        remote.manifestUrl = manifest.manifestUrl; // preserve the authorized URL
        manifest = std::move(remote);
        return true;
    }
    Logger::info(L"manifest: local manifest is current");
    return false;
}

bool Orchestrator::repairComponents(const Manifest &manifest,
                                    const std::vector<const Component *> &missing)
{
    Installer installer(m_installDir, m_stagingDir);
    ensureDir(m_stagingDir);

    // Total bytes for the progress bar (0 sizes => indeterminate).
    uint64_t totalBytes = 0;
    for (const Component *c : missing) {
        totalBytes += c->sizeBytes;
    }
    uint64_t completedBytes = 0;

    for (const Component *c : missing) {
        m_splash.setStatus(L"Preparing " + c->name + L"…");
        Logger::info(L"repair: component " + c->name);

        const std::vector<std::wstring> urls = resolveSourceUrls(manifest, *c);
        if (urls.empty()) {
            Logger::error(L"repair: no sources for " + c->name);
            if (c->required) {
                installer.rollbackAll();
                return false;
            }
            continue;
        }
        if (c->sha256.empty()) {
            Logger::error(L"repair: refusing component without sha256: " + c->name);
            if (c->required) {
                installer.rollbackAll();
                return false;
            }
            continue;
        }

        const std::wstring dlPath = joinPath(m_stagingDir, c->name + L".download");

        bool installed = false;
        for (const std::wstring &url : urls) {
            m_splash.setStatus(L"Downloading " + c->name + L"…");
            if (c->sizeBytes == 0) {
                m_splash.setMarquee(true);
            }

            const uint64_t baseCompleted = completedBytes;
            const auto progress = [&](uint64_t got, uint64_t /*tot*/) {
                if (totalBytes > 0) {
                    const uint64_t overall = baseCompleted + got;
                    m_splash.setProgress(static_cast<int>((overall * 100) / (totalBytes ? totalBytes : 1)));
                }
            };

            const DownloadResult dr = downloadToFile(url, dlPath, progress, nullptr);
            if (dr != DownloadResult::Success) {
                Logger::warning(L"repair: source failed (" + url + L"), trying next");
                continue;
            }

            m_splash.setStatus(L"Verifying " + c->name + L"…");
            m_splash.setMarquee(true);
            const auto digest = sha256File(dlPath);
            if (!digest || !hashEquals(*digest, c->sha256)) {
                Logger::error(L"repair: SHA-256 mismatch for " + c->name + L" from " + url);
                ::DeleteFileW(dlPath.c_str());
                continue; // try next source
            }

            m_splash.setStatus(L"Installing " + c->name + L"…");
            if (installer.installComponent(*c, dlPath)) {
                installed = true;
                ::DeleteFileW(dlPath.c_str());
                break;
            }
            ::DeleteFileW(dlPath.c_str());
            Logger::warning(L"repair: install failed for " + c->name + L" from " + url);
        }

        completedBytes += c->sizeBytes;
        if (totalBytes > 0) {
            m_splash.setProgress(static_cast<int>((completedBytes * 100) / totalBytes));
        }

        if (!installed) {
            if (c->required) {
                Logger::error(L"repair: required component failed: " + c->name);
                installer.rollbackAll();
                return false;
            }
            Logger::warning(L"repair: optional component skipped: " + c->name);
        } else {
            // Re-verify with the detect rule that the install actually satisfied it.
            if (c->required && !isComponentSatisfied(*c, m_installDir) &&
                c->install.action != InstallAction::RunSilent) {
                Logger::error(L"repair: component still missing after install: " + c->name);
                installer.rollbackAll();
                return false;
            }
        }
    }

    installer.commit();
    return true;
}

int Orchestrator::run()
{
    Manifest manifest;
    const bool haveManifest = loadManifests(manifest);

    const std::wstring appPath = joinPath(m_installDir,
                                          haveManifest ? manifest.appExecutable : L"QGroundControlApp.exe");

    // Without a manifest we cannot self-repair; just try to launch the app.
    if (!haveManifest) {
        Logger::warning(L"manifest: none found; launching app directly");
        if (launchApplication(appPath)) {
            return 0;
        }
        SplashWindow::showError(L"QGroundControl",
            L"QGroundControl could not start and no recovery manifest was found.\n\n"
            L"Please reinstall QGroundControl.\n\nLog: " + Logger::logPath());
        return 1;
    }

    // Detect which components are missing/outdated.
    m_splash.setStatus(L"Checking required components…");
    std::vector<const Component *> missing;
    bool missingRequired = false;
    for (const Component &c : manifest.components) {
        if (!isComponentSatisfied(c, m_installDir)) {
            missing.push_back(&c);
            missingRequired = missingRequired || c.required;
        }
    }

    if (missing.empty()) {
        Logger::info(L"bootstrap: all components present; launching");
        m_splash.setStatus(L"Starting QGroundControl…");
        if (launchApplication(appPath)) {
            return 0;
        }
        SplashWindow::showError(L"QGroundControl",
            L"All components appear present but QGroundControl failed to start.\n\nLog: " + Logger::logPath());
        return 1;
    }

    // Offline decision tree.
    if (!hasInternetConnection()) {
        if (missingRequired) {
            Logger::error(L"bootstrap: required components missing and offline");
            SplashWindow::showError(L"QGroundControl - Setup required",
                L"QGroundControl needs to download required components but no internet "
                L"connection is available.\n\nConnect to the internet and try again.\n\n"
                L"Download source: " + supportUrlHint(manifest) + L"\nLog: " + Logger::logPath());
            return 2;
        }
        Logger::warning(L"bootstrap: only optional components missing and offline; launching anyway");
        if (launchApplication(appPath)) {
            return 0;
        }
        return 1;
    }

    // File-based repair (copy/extract) writes into the install directory, which
    // is normally Program Files (admin-only). If a required component needs such
    // a write but we cannot write there, fail with an actionable message rather
    // than attempting writes that would fail and roll back. (run-silent
    // components such as the VC++ redist self-elevate via UAC instead.)
    bool needsInstallDirWrite = false;
    for (const Component *c : missing) {
        if (c->required &&
            (c->install.action == InstallAction::Copy || c->install.action == InstallAction::Extract)) {
            needsInstallDirWrite = true;
            break;
        }
    }
    if (needsInstallDirWrite && !canWriteToDir(m_installDir)) {
        Logger::error(L"bootstrap: repair needs install dir but it is not writable: " + m_installDir);
        SplashWindow::showError(L"QGroundControl - Setup required",
            L"QGroundControl needs to repair missing components in:\n" + m_installDir +
            L"\n\nThis requires administrator rights. Right-click QGroundControl and choose "
            L"\"Run as administrator\", then try again.\n\nLog: " + Logger::logPath());
        return 4;
    }

    // Repair, then launch.
    if (!repairComponents(manifest, missing)) {
        SplashWindow::showError(L"QGroundControl - Setup failed",
            L"QGroundControl could not download or install a required component.\n\n"
            L"Please check your internet connection and try again.\n\n"
            L"Download source: " + supportUrlHint(manifest) + L"\nLog: " + Logger::logPath());
        return 3;
    }

    m_splash.setStatus(L"Starting QGroundControl…");
    m_splash.setProgress(100);
    if (launchApplication(appPath)) {
        return 0;
    }
    SplashWindow::showError(L"QGroundControl",
        L"Setup completed but QGroundControl failed to start.\n\nLog: " + Logger::logPath());
    return 1;
}

} // namespace qgc
