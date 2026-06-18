// ----------------------------------------------------------------------------
// ProcessLauncher — starts the real Qt application (QGroundControlApp.exe),
// forwarding every command-line argument the launcher itself received (so the
// Start-menu shortcuts' -desktop / -swrast flags reach the app unchanged).
// ----------------------------------------------------------------------------
#pragma once

#include <string>

namespace qgc {

// Launches `appPath` with the launcher's own arguments (argv[1..]) appended.
// Sets the working directory to the app's directory. Returns true if the
// process started. Does not wait for the app to exit.
bool launchApplication(const std::wstring &appPath);

// Returns the original command-line arguments (everything after argv[0]),
// preserving quoting, ready to append to a child command line.
std::wstring forwardedArguments();

} // namespace qgc
