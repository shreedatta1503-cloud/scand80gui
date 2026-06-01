// ----------------------------------------------------------------------------
// QGroundControl Windows Bootstrap Launcher — entry point.
//
// This is the executable the user double-clicks (QGroundControl.exe). It is a
// tiny, statically-linked (/MT) Win32 program with no dependency on Qt or the
// MSVC redistributable, so it always starts on a clean machine. It verifies and
// repairs the runtime, then launches the real Qt application
// (QGroundControlApp.exe), forwarding all command-line arguments.
// ----------------------------------------------------------------------------
#include "Logger.h"
#include "Orchestrator.h"
#include "SplashWindow.h"
#include "Common.h"

#include <windows.h>
#include <objbase.h>

using namespace qgc;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    Logger::init(LogLevel::Info);
    Logger::info(L"==== QGroundControl launcher starting ====");

    // Single-instance guard: prevents two concurrent repair sessions. If another
    // launcher is already running, defer to it (it will start the app).
    HANDLE mutex = ::CreateMutexW(nullptr, FALSE, L"Global\\QGroundControlLauncher");
    if (mutex && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        Logger::info(L"launcher: another instance is running; exiting");
        if (mutex) {
            ::CloseHandle(mutex);
        }
        Logger::shutdown();
        ::CoUninitialize();
        return 0;
    }

    int exitCode = 1;
    {
        SplashWindow splash;
        splash.show(L"QGroundControl");

        Orchestrator orchestrator(splash);
        exitCode = orchestrator.run();

        splash.close();
    }

    Logger::info(L"==== QGroundControl launcher exiting (" + std::to_wstring(exitCode) + L") ====");

    if (mutex) {
        ::ReleaseMutex(mutex);
        ::CloseHandle(mutex);
    }
    Logger::shutdown();
    ::CoUninitialize();
    return exitCode;
}
