// ----------------------------------------------------------------------------
// Zip — archive extraction.
//
// Rather than vendoring a decompression library, this uses the OS-native
// extractor: bsdtar (System32\tar.exe), which ships with Windows 10 1803+ and
// extracts .zip. bsdtar already refuses absolute paths and ".." traversal; we
// extract only into an isolated staging directory and additionally validate
// containment, so a malicious archive cannot escape. PowerShell Expand-Archive
// is used only as a last-resort fallback on older systems.
// ----------------------------------------------------------------------------
#pragma once

#include <string>

namespace qgc {

// Extracts `archivePath` into `destDir` (created if missing). Returns true on
// success. `destDir` must already be an isolated, trusted directory.
bool extractArchive(const std::wstring &archivePath, const std::wstring &destDir);

} // namespace qgc
