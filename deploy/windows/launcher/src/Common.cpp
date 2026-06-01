#include "Common.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <cwctype>

namespace qgc {

std::wstring utf8ToWide(const std::string &utf8)
{
    if (utf8.empty()) {
        return std::wstring();
    }
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::string wideToUtf8(const std::wstring &wide)
{
    if (wide.empty()) {
        return std::string();
    }
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring joinPath(const std::wstring &base, const std::wstring &leaf)
{
    if (base.empty()) {
        return leaf;
    }
    if (leaf.empty()) {
        return base;
    }
    std::wstring out = base;
    if (out.back() != L'\\' && out.back() != L'/') {
        out.push_back(L'\\');
    }
    size_t start = 0;
    while (start < leaf.size() && (leaf[start] == L'\\' || leaf[start] == L'/')) {
        ++start;
    }
    out.append(leaf, start, std::wstring::npos);
    return out;
}

std::wstring parentDir(const std::wstring &path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return std::wstring();
    }
    return path.substr(0, pos);
}

std::wstring fileName(const std::wstring &path)
{
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

std::wstring exeDir()
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return std::wstring();
        }
        if (n < buf.size()) {
            buf.resize(n);
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return parentDir(buf);
}

std::wstring localAppDataDir()
{
    PWSTR raw = nullptr;
    if (::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw) != S_OK) {
        return std::wstring();
    }
    std::wstring base(raw);
    ::CoTaskMemFree(raw);
    const std::wstring dir = joinPath(base, L"QGroundControl");
    ensureDir(dir);
    return dir;
}

bool fileExists(const std::wstring &path)
{
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring &path)
{
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensureDir(const std::wstring &path)
{
    if (path.empty() || dirExists(path)) {
        return true;
    }
    const std::wstring parent = parentDir(path);
    if (!parent.empty() && !dirExists(parent)) {
        ensureDir(parent);
    }
    if (::CreateDirectoryW(path.c_str(), nullptr)) {
        return true;
    }
    return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

bool removeTree(const std::wstring &path)
{
    if (fileExists(path)) {
        return ::DeleteFileW(path.c_str()) != FALSE;
    }
    if (!dirExists(path)) {
        return true; // nothing to remove
    }

    const std::wstring pattern = joinPath(path, L"*");
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") {
                continue;
            }
            removeTree(joinPath(path, name));
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
    }
    return ::RemoveDirectoryW(path.c_str()) != FALSE;
}

std::optional<uint64_t> fileSize(const std::wstring &path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return std::nullopt;
    }
    ULARGE_INTEGER li;
    li.HighPart = data.nFileSizeHigh;
    li.LowPart = data.nFileSizeLow;
    return li.QuadPart;
}

std::wstring normalizeRelative(const std::wstring &fragment)
{
    std::wstring out;
    out.reserve(fragment.size());
    for (wchar_t c : fragment) {
        out.push_back(c == L'/' ? L'\\' : c);
    }
    size_t start = 0;
    while (start < out.size() && out[start] == L'\\') {
        ++start;
    }
    return out.substr(start);
}

bool isContainedWithin(const std::wstring &root, const std::wstring &child)
{
    auto canonical = [](const std::wstring &p) -> std::wstring {
        std::wstring buf(MAX_PATH, L'\0');
        DWORD n = ::GetFullPathNameW(p.c_str(), static_cast<DWORD>(buf.size()), buf.data(), nullptr);
        if (n == 0) {
            return std::wstring();
        }
        if (n >= buf.size()) {
            buf.resize(n);
            n = ::GetFullPathNameW(p.c_str(), static_cast<DWORD>(buf.size()), buf.data(), nullptr);
        }
        buf.resize(n);
        for (auto &c : buf) {
            c = static_cast<wchar_t>(::towlower(c));
        }
        return buf;
    };

    std::wstring r = canonical(root);
    const std::wstring c = canonical(child);
    if (r.empty() || c.empty()) {
        return false;
    }
    if (!r.empty() && r.back() != L'\\') {
        r.push_back(L'\\');
    }
    return c.compare(0, r.size(), r) == 0;
}

} // namespace qgc
