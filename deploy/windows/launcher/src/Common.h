// ----------------------------------------------------------------------------
// QGroundControl Windows Bootstrap Launcher
// Common types and small Win32/string helpers shared across the launcher.
//
// This code is intentionally self-contained: it depends only on the Win32 API
// and the C++ standard library so the resulting QGroundControl.exe runs on a
// clean Windows machine with no Qt, no MSVC redistributable, and no third-party
// runtime present.
// ----------------------------------------------------------------------------
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace qgc {

// UTF-8 <-> UTF-16 conversion (Win32 APIs are wide; manifest/log text is UTF-8).
std::wstring utf8ToWide(const std::string &utf8);
std::string  wideToUtf8(const std::wstring &wide);

// Path helpers (operate on wide strings; "\\" separators).
std::wstring joinPath(const std::wstring &base, const std::wstring &leaf);
std::wstring parentDir(const std::wstring &path);
std::wstring fileName(const std::wstring &path);

// Returns the directory containing the running launcher executable (the bin/
// directory of an install), with no trailing separator.
std::wstring exeDir();

// Returns %LOCALAPPDATA%\QGroundControl (created if missing), or an empty
// string on failure.
std::wstring localAppDataDir();

// Filesystem helpers.
bool fileExists(const std::wstring &path);
bool dirExists(const std::wstring &path);
bool ensureDir(const std::wstring &path);            // mkdir -p semantics
bool removeTree(const std::wstring &path);           // recursive delete, best effort
std::optional<uint64_t> fileSize(const std::wstring &path);

// Normalize a possibly-relative, possibly-/-separated manifest path fragment to
// a clean Windows relative path (no leading separators, "/"->"\\"). Used to
// resolve manifest "dest"/"path" fields against the install directory.
std::wstring normalizeRelative(const std::wstring &fragment);

// True if `child`, once fully resolved, is contained within `root`. Used as the
// zip/path-traversal containment guard.
bool isContainedWithin(const std::wstring &root, const std::wstring &child);

} // namespace qgc
