// ----------------------------------------------------------------------------
// Connectivity — the wininet-based "is the machine online?" probe.
//
// This lives in its own translation unit because <wininet.h> and <winhttp.h>
// have conflicting definitions and cannot be included together; Downloader.cpp
// owns <winhttp.h>, so the wininet call is isolated here.
// ----------------------------------------------------------------------------
#include "Downloader.h"

#include <windows.h>
#include <wininet.h>

namespace qgc {

bool hasInternetConnection()
{
    DWORD flags = 0;
    return ::InternetGetConnectedState(&flags, 0) != FALSE;
}

} // namespace qgc
