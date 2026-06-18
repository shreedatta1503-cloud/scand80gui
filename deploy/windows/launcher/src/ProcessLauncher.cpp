#include "ProcessLauncher.h"
#include "Common.h"
#include "Logger.h"

#include <windows.h>

namespace qgc {

std::wstring forwardedArguments()
{
    // GetCommandLineW() is argv[0] (possibly quoted) followed by the arguments.
    // Strip the program token, preserving the remainder verbatim so quoting in
    // user arguments is forwarded exactly as received.
    const wchar_t *cmd = ::GetCommandLineW();
    if (!cmd) {
        return std::wstring();
    }

    const wchar_t *p = cmd;
    if (*p == L'"') {
        // Quoted program path: skip to the closing quote.
        ++p;
        while (*p && *p != L'"') {
            ++p;
        }
        if (*p == L'"') {
            ++p;
        }
    } else {
        // Unquoted: skip to first whitespace.
        while (*p && *p != L' ' && *p != L'\t') {
            ++p;
        }
    }
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    return std::wstring(p);
}

bool launchApplication(const std::wstring &appPath)
{
    if (!fileExists(appPath)) {
        Logger::error(L"launch: application not found: " + appPath);
        return false;
    }

    const std::wstring args = forwardedArguments();
    std::wstring commandLine = L"\"" + appPath + L"\"";
    if (!args.empty()) {
        commandLine += L" " + args;
    }

    const std::wstring workingDir = parentDir(appPath);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    std::wstring mutableCmd = commandLine; // CreateProcessW may write to the buffer.
    Logger::info(L"launch: starting " + commandLine);
    if (!::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0,
                          nullptr, workingDir.c_str(), &si, &pi)) {
        Logger::error(L"launch: CreateProcess failed (" + std::to_wstring(::GetLastError()) + L")");
        return false;
    }

    // Give the app a moment to get past loader/initialization so the splash can
    // close without flashing the desktop, then detach.
    ::WaitForInputIdle(pi.hProcess, 10000);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

} // namespace qgc
