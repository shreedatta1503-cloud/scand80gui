// ----------------------------------------------------------------------------
// Config — the bootstrap manifest model and its loader.
//
// The manifest (qgc-bootstrap.json) is external data so dependencies can be
// updated by publishing a new manifest + assets, without rebuilding the app.
// See deploy/windows/launcher/README.md for the schema documentation.
// ----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace qgc {

enum class DetectRule { FileExists, RegistryKey, DllVersion, Unknown };
enum class InstallAction { Copy, Extract, RunSilent, Unknown };

struct Detect {
    DetectRule   rule = DetectRule::Unknown;
    std::wstring path;        // file-exists / dll-version: path relative to install dir
    std::wstring hive;        // registry-key: "HKLM" | "HKCU"
    std::wstring key;         // registry-key: subkey path
    std::wstring value;       // registry-key: value name
    std::wstring expect;      // registry-key: expected value (string compare)
    std::wstring minVersion;  // registry-key / dll-version: minimum acceptable version
};

struct Install {
    InstallAction action = InstallAction::Unknown;
    std::wstring  dest;       // copy/extract destination (relative to install dir)
    std::wstring  args;       // run-silent: command-line arguments
    bool          elevate = false; // run-silent: require admin (UAC)
};

struct Component {
    std::wstring              name;
    std::wstring              type;      // file | vcredist | driver | qt-bundle (informational)
    bool                      required = true; // hard-required for the app to launch
    Detect                    detect;
    std::vector<std::wstring> sources;   // ordered URLs (absolute) or mirror-relative
    std::wstring              sha256;     // lowercase hex, mandatory for downloads
    uint64_t                  sizeBytes = 0;
    Install                   install;
    bool                      verifyAuthenticode = false;
    std::wstring              publisher; // expected Authenticode publisher (substring match)
};

struct Manifest {
    int                       schemaVersion = 0;
    std::wstring              manifestVersion;
    std::wstring              appExecutable = L"QGroundControlApp.exe";
    std::wstring              manifestUrl;  // optional remote manifest override
    std::vector<std::wstring> mirrors;      // ordered base URLs for relative sources
    std::vector<Component>    components;
    std::wstring              sourcePath;   // where this manifest was loaded from (diagnostics)
};

// Parses a manifest from raw UTF-8 JSON. Returns false with `error` set on
// malformed input or unsupported schema version.
bool parseManifest(const std::string &utf8, Manifest &out, std::wstring &error);

// Loads a manifest from a local file path.
bool loadManifestFile(const std::wstring &path, Manifest &out, std::wstring &error);

// Resolves a component source against the manifest mirrors. Absolute https URLs
// are returned as-is; relative fragments are joined onto each mirror in order.
std::vector<std::wstring> resolveSourceUrls(const Manifest &manifest, const Component &component);

} // namespace qgc
