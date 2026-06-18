#include "Logger.h"
#include "Common.h"

#include <windows.h>
#include <mutex>
#include <cstdint>
#include <cstdio>

namespace qgc {

namespace {
constexpr uint64_t kMaxLogBytes = 2 * 1024 * 1024; // 2 MiB, rotate once.

std::mutex      g_mutex;
HANDLE          g_file = INVALID_HANDLE_VALUE;
LogLevel        g_minLevel = LogLevel::Info;
std::wstring    g_path;

const wchar_t *levelTag(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:   return L"DEBUG";
    case LogLevel::Info:    return L"INFO ";
    case LogLevel::Warning: return L"WARN ";
    case LogLevel::Error:   return L"ERROR";
    }
    return L"?????";
}

std::wstring timestamp()
{
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    wchar_t buf[32];
    ::swprintf(buf, 32, L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return buf;
}
} // namespace

void Logger::init(LogLevel minLevel)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_minLevel = minLevel;

    const std::wstring dir = localAppDataDir();
    if (dir.empty()) {
        return;
    }
    g_path = joinPath(dir, L"launcher.log");

    // Rotate if the existing log is oversized.
    if (const auto sz = fileSize(g_path); sz && *sz > kMaxLogBytes) {
        const std::wstring old = joinPath(dir, L"launcher.log.1");
        ::DeleteFileW(old.c_str());
        ::MoveFileW(g_path.c_str(), old.c_str());
    }

    g_file = ::CreateFileW(g_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_file != INVALID_HANDLE_VALUE) {
        // Write a UTF-8 BOM on a brand-new file.
        LARGE_INTEGER pos{};
        if (::GetFileSizeEx(g_file, &pos) && pos.QuadPart == 0) {
            const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
            DWORD written = 0;
            ::WriteFile(g_file, bom, sizeof(bom), &written, nullptr);
        }
    }
}

void Logger::shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) {
        ::CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

void Logger::writeLine(const std::wstring &line)
{
    ::OutputDebugStringW((line + L"\n").c_str());
    if (g_file == INVALID_HANDLE_VALUE) {
        return;
    }
    const std::string utf8 = wideToUtf8(line) + "\r\n";
    DWORD written = 0;
    ::WriteFile(g_file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void Logger::log(LogLevel level, const std::wstring &message)
{
    if (level < g_minLevel) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    writeLine(timestamp() + L" [" + levelTag(level) + L"] " + message);
}

std::wstring Logger::logPath()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_path;
}

} // namespace qgc
