#include "Detector.h"
#include "Common.h"
#include "Logger.h"

#include <windows.h>

#include <cstdlib>
#include <cstdio>
#include <vector>

#pragma comment(lib, "version.lib")

namespace qgc {

namespace {

// Compares dotted version strings ("14.40.33810" etc). Returns <0, 0, >0.
int compareVersions(const std::wstring &a, const std::wstring &b)
{
    auto next = [](const std::wstring &s, size_t &pos) -> long {
        if (pos >= s.size()) {
            return 0;
        }
        size_t end = s.find(L'.', pos);
        const std::wstring part = s.substr(pos, end == std::wstring::npos ? std::wstring::npos : end - pos);
        pos = (end == std::wstring::npos) ? s.size() : end + 1;
        return ::wcstol(part.c_str(), nullptr, 10);
    };

    size_t pa = 0, pb = 0;
    while (pa < a.size() || pb < b.size()) {
        const long va = next(a, pa);
        const long vb = next(b, pb);
        if (va != vb) {
            return va < vb ? -1 : 1;
        }
    }
    return 0;
}

std::optional<std::wstring> fileVersion(const std::wstring &path)
{
    DWORD dummy = 0;
    const DWORD size = ::GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<unsigned char> data(size);
    if (!::GetFileVersionInfoW(path.c_str(), 0, size, data.data())) {
        return std::nullopt;
    }
    VS_FIXEDFILEINFO *info = nullptr;
    UINT len = 0;
    if (!::VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &len) || !info) {
        return std::nullopt;
    }
    wchar_t buf[64];
    ::swprintf(buf, 64, L"%u.%u.%u.%u",
               HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
               HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return std::wstring(buf);
}

bool detectFileExists(const Component &c, const std::wstring &installDir)
{
    const std::wstring full = joinPath(installDir, normalizeRelative(c.detect.path));
    return fileExists(full);
}

bool detectDllVersion(const Component &c, const std::wstring &installDir)
{
    const std::wstring full = joinPath(installDir, normalizeRelative(c.detect.path));
    if (!fileExists(full)) {
        return false;
    }
    if (c.detect.minVersion.empty()) {
        return true;
    }
    const auto ver = fileVersion(full);
    return ver && compareVersions(*ver, c.detect.minVersion) >= 0;
}

bool detectRegistryKey(const Component &c)
{
    HKEY root = HKEY_LOCAL_MACHINE;
    if (c.detect.hive == L"HKCU") {
        root = HKEY_CURRENT_USER;
    }

    HKEY key = nullptr;
    // 64-bit view; the launcher and app are 64-bit.
    if (::RegOpenKeyExW(root, c.detect.key.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }

    bool satisfied = false;
    DWORD type = 0, dataSize = 0;
    if (::RegQueryValueExW(key, c.detect.value.c_str(), nullptr, &type, nullptr, &dataSize) == ERROR_SUCCESS) {
        std::vector<unsigned char> data(dataSize ? dataSize : 1);
        ::RegQueryValueExW(key, c.detect.value.c_str(), nullptr, &type, data.data(), &dataSize);

        std::wstring actual;
        if (type == REG_DWORD && dataSize >= sizeof(DWORD)) {
            actual = std::to_wstring(*reinterpret_cast<DWORD *>(data.data()));
        } else if (type == REG_SZ || type == REG_EXPAND_SZ) {
            actual = reinterpret_cast<wchar_t *>(data.data());
        }

        satisfied = c.detect.expect.empty() ? true : (actual == c.detect.expect);
    }

    // Optional version gate against a sibling "Version" value.
    if (satisfied && !c.detect.minVersion.empty()) {
        wchar_t verBuf[128] = {0};
        DWORD verSize = sizeof(verBuf), verType = 0;
        if (::RegQueryValueExW(key, L"Version", nullptr, &verType,
                               reinterpret_cast<LPBYTE>(verBuf), &verSize) == ERROR_SUCCESS) {
            std::wstring v = verBuf;
            if (!v.empty() && (v[0] == L'v' || v[0] == L'V')) {
                v.erase(0, 1);
            }
            satisfied = compareVersions(v, c.detect.minVersion) >= 0;
        }
    }

    ::RegCloseKey(key);
    return satisfied;
}

} // namespace

bool isComponentSatisfied(const Component &component, const std::wstring &installDir)
{
    bool satisfied;
    switch (component.detect.rule) {
    case DetectRule::FileExists:  satisfied = detectFileExists(component, installDir); break;
    case DetectRule::DllVersion:  satisfied = detectDllVersion(component, installDir); break;
    case DetectRule::RegistryKey: satisfied = detectRegistryKey(component); break;
    case DetectRule::Unknown:
    default:
        // Unknown rule: treat as satisfied so an unrecognized future component
        // never blocks startup on an older launcher.
        Logger::warning(L"detect: unknown rule for component " + component.name + L", skipping");
        satisfied = true;
        break;
    }
    Logger::info(L"detect: " + component.name + L" -> " + (satisfied ? L"present" : L"MISSING"));
    return satisfied;
}

} // namespace qgc
