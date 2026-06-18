#include "Zip.h"
#include "Common.h"
#include "Logger.h"

#include <windows.h>

namespace qgc {

namespace {

// Runs a command line and waits for it. Returns the process exit code, or -1 on
// launch failure.
int runAndWait(const std::wstring &commandLine, const std::wstring &workingDir)
{
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring mutableCmd = commandLine; // CreateProcessW may modify the buffer.
    if (!::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW, nullptr,
                          workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi)) {
        return -1;
    }
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}

std::wstring systemTarPath()
{
    wchar_t sysDir[MAX_PATH] = {0};
    if (::GetSystemDirectoryW(sysDir, MAX_PATH) == 0) {
        return std::wstring();
    }
    return joinPath(sysDir, L"tar.exe");
}

} // namespace

bool extractArchive(const std::wstring &archivePath, const std::wstring &destDir)
{
    if (!ensureDir(destDir)) {
        Logger::error(L"extract: cannot create dest " + destDir);
        return false;
    }

    // Preferred: bsdtar (System32\tar.exe). -x extract, -f file. It refuses
    // absolute/".." members by default. Run with destDir as the working dir.
    const std::wstring tar = systemTarPath();
    if (fileExists(tar)) {
        const std::wstring cmd = L"\"" + tar + L"\" -x -f \"" + archivePath + L"\"";
        const int code = runAndWait(cmd, destDir);
        if (code == 0) {
            Logger::info(L"extract: tar.exe extracted " + fileName(archivePath));
            return true;
        }
        Logger::warning(L"extract: tar.exe exit " + std::to_wstring(code) + L", trying fallback");
    }

    // Fallback: PowerShell Expand-Archive (older Windows without bsdtar).
    // Escape any single quotes so a path cannot break out of the single-quoted
    // PowerShell string literals below ('->'').
    auto psQuote = [](const std::wstring &s) {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s) {
            if (c == L'\'') {
                out.push_back(L'\'');
            }
            out.push_back(c);
        }
        return out;
    };
    std::wstring ps = L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                      L"\"Expand-Archive -LiteralPath '" + psQuote(archivePath) +
                      L"' -DestinationPath '" + psQuote(destDir) + L"' -Force\"";
    const int code = runAndWait(ps, std::wstring());
    if (code == 0) {
        Logger::info(L"extract: Expand-Archive extracted " + fileName(archivePath));
        return true;
    }
    Logger::error(L"extract: all extractors failed for " + archivePath);
    return false;
}

} // namespace qgc
