// ----------------------------------------------------------------------------
// Logger — timestamped UTF-8 log to %LOCALAPPDATA%\QGroundControl\launcher.log
// (also mirrored to OutputDebugString). Size-capped with a single rotation.
// ----------------------------------------------------------------------------
#pragma once

#include <string>

namespace qgc {

enum class LogLevel { Debug, Info, Warning, Error };

class Logger {
public:
    // Opens (and rotates if oversized) the log file under localAppData.
    static void init(LogLevel minLevel = LogLevel::Info);
    static void shutdown();

    static void log(LogLevel level, const std::wstring &message);

    static void debug(const std::wstring &m)   { log(LogLevel::Debug, m); }
    static void info(const std::wstring &m)     { log(LogLevel::Info, m); }
    static void warning(const std::wstring &m)  { log(LogLevel::Warning, m); }
    static void error(const std::wstring &m)    { log(LogLevel::Error, m); }

    static std::wstring logPath();

private:
    static void writeLine(const std::wstring &line);
};

} // namespace qgc
