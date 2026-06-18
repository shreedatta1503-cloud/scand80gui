// ----------------------------------------------------------------------------
// Elevation — admin checks, elevated process launch (UAC), and Authenticode
// signature verification. Used only for the run-silent install path (e.g. the
// VC++ redistributable), which may require administrator rights.
// ----------------------------------------------------------------------------
#pragma once

#include <string>

namespace qgc {

// True if the current process is already running elevated.
bool isProcessElevated();

// True if the current process can write into `dir` (used to decide whether a
// runtime repair into Program Files needs elevation).
bool canWriteToDir(const std::wstring &dir);

// Runs `exePath args`. If `elevate` is true and we are not already elevated,
// launches via ShellExecuteEx "runas" (shows the normal UAC prompt). Waits for
// completion. Returns the process exit code, or -1 on launch failure / refusal.
int runProcess(const std::wstring &exePath, const std::wstring &args, bool elevate);

// Verifies the Authenticode signature of `path` via WinVerifyTrust. When
// `expectedPublisher` is non-empty, also checks the signer subject contains it.
// Returns true only if the file is trusted (and matches the publisher).
bool verifyAuthenticode(const std::wstring &path, const std::wstring &expectedPublisher);

} // namespace qgc
