#include "Config.h"
#include "Json.h"
#include "Common.h"
#include "Logger.h"

#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace qgc {

namespace {

constexpr int kSupportedSchemaVersion = 1;

DetectRule parseDetectRule(const std::wstring &s)
{
    if (s == L"file-exists")  return DetectRule::FileExists;
    if (s == L"registry-key") return DetectRule::RegistryKey;
    if (s == L"dll-version")  return DetectRule::DllVersion;
    return DetectRule::Unknown;
}

InstallAction parseInstallAction(const std::wstring &s)
{
    if (s == L"copy")       return InstallAction::Copy;
    if (s == L"extract")    return InstallAction::Extract;
    if (s == L"run-silent") return InstallAction::RunSilent;
    return InstallAction::Unknown;
}

std::wstring toLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(::towlower(c)); });
    return s;
}

bool startsWithCI(const std::wstring &s, const std::wstring &prefix)
{
    return s.size() >= prefix.size() && toLower(s.substr(0, prefix.size())) == toLower(prefix);
}

// A component name is used to build staging file/dir paths (e.g.
// "<staging>\<name>.download", "<staging>\x_<name>"). Reject names that contain
// path separators, a drive separator, or ".." so a hostile (e.g. remote)
// manifest cannot escape the staging directory.
bool isSafeComponentName(const std::wstring &name)
{
    if (name.empty() || name == L"." || name == L"..") {
        return false;
    }
    return name.find(L'\\') == std::wstring::npos &&
           name.find(L'/')  == std::wstring::npos &&
           name.find(L':')  == std::wstring::npos &&
           name.find(L"..") == std::wstring::npos;
}

} // namespace

bool parseManifest(const std::string &utf8, Manifest &out, std::wstring &error)
{
    JsonValue root;
    if (!parseJson(utf8, root, error)) {
        return false;
    }
    if (!root.isObject()) {
        error = L"Manifest root must be a JSON object";
        return false;
    }

    out.schemaVersion = static_cast<int>(root[L"schemaVersion"].asNumber(0));
    if (out.schemaVersion != kSupportedSchemaVersion) {
        error = L"Unsupported manifest schemaVersion (expected 1)";
        return false;
    }

    out.manifestVersion = root[L"manifestVersion"].asString();
    out.appExecutable = root[L"appExecutable"].asString(L"QGroundControlApp.exe");
    out.manifestUrl = root[L"manifestUrl"].asString();

    const JsonValue &mirrors = root[L"mirrors"];
    if (mirrors.isArray()) {
        for (const auto &m : mirrors.arrayValue) {
            if (m.isString() && !m.stringValue.empty()) {
                out.mirrors.push_back(m.stringValue);
            }
        }
    }

    const JsonValue &components = root[L"components"];
    if (!components.isArray()) {
        error = L"Manifest 'components' must be an array";
        return false;
    }

    for (const auto &c : components.arrayValue) {
        if (!c.isObject()) {
            continue;
        }
        Component comp;
        comp.name = c[L"name"].asString();
        comp.type = c[L"type"].asString();
        comp.required = c[L"required"].asBool(true);
        comp.sha256 = toLower(c[L"sha256"].asString());
        comp.sizeBytes = static_cast<uint64_t>(c[L"sizeBytes"].asNumber(0));
        comp.verifyAuthenticode = c[L"verifyAuthenticode"].asBool(false);
        comp.publisher = c[L"publisher"].asString();

        const JsonValue &detect = c[L"detect"];
        comp.detect.rule = parseDetectRule(detect[L"rule"].asString());
        comp.detect.path = detect[L"path"].asString();
        comp.detect.hive = detect[L"hive"].asString();
        comp.detect.key = detect[L"key"].asString();
        comp.detect.value = detect[L"value"].asString();
        comp.detect.expect = detect[L"expect"].isString()
                                 ? detect[L"expect"].stringValue
                                 : (detect.contains(L"expect")
                                        ? std::to_wstring(static_cast<long long>(detect[L"expect"].asNumber()))
                                        : L"");
        comp.detect.minVersion = detect[L"minVersion"].asString();

        const JsonValue &sources = c[L"sources"];
        if (sources.isArray()) {
            for (const auto &s : sources.arrayValue) {
                if (s.isString() && !s.stringValue.empty()) {
                    comp.sources.push_back(s.stringValue);
                }
            }
        }

        const JsonValue &install = c[L"install"];
        comp.install.action = parseInstallAction(install[L"action"].asString());
        comp.install.dest = install[L"dest"].asString();
        comp.install.args = install[L"args"].asString();
        comp.install.elevate = install[L"elevate"].asBool(false);

        if (!isSafeComponentName(comp.name)) {
            Logger::warning(L"Manifest component skipped: missing or unsafe 'name' ('" + comp.name + L"')");
            continue;
        }
        out.components.push_back(std::move(comp));
    }

    return true;
}

bool loadManifestFile(const std::wstring &path, Manifest &out, std::wstring &error)
{
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = L"Could not open manifest: " + path;
        return false;
    }
    LARGE_INTEGER size{};
    ::GetFileSizeEx(h, &size);
    std::string buffer(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ::ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    ::CloseHandle(h);
    if (!ok) {
        error = L"Could not read manifest: " + path;
        return false;
    }
    buffer.resize(read);
    if (!parseManifest(buffer, out, error)) {
        return false;
    }
    out.sourcePath = path;
    return true;
}

std::vector<std::wstring> resolveSourceUrls(const Manifest &manifest, const Component &component)
{
    std::vector<std::wstring> urls;
    for (const std::wstring &src : component.sources) {
        if (startsWithCI(src, L"https://")) {
            urls.push_back(src);
        } else if (startsWithCI(src, L"http://")) {
            // Security: dependency payloads are never fetched over plain HTTP.
            Logger::warning(L"manifest: ignoring insecure non-HTTPS source " + src);
        } else {
            // Mirror-relative: join onto every configured mirror, in order.
            for (const std::wstring &mirror : manifest.mirrors) {
                std::wstring base = mirror;
                if (!base.empty() && base.back() != L'/') {
                    base.push_back(L'/');
                }
                std::wstring leaf = src;
                size_t start = 0;
                while (start < leaf.size() && leaf[start] == L'/') {
                    ++start;
                }
                urls.push_back(base + leaf.substr(start));
            }
        }
    }
    return urls;
}

} // namespace qgc
