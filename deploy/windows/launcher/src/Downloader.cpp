#include "Downloader.h"
#include "Common.h"
#include "Logger.h"

#include <windows.h>
#include <winhttp.h>
// NOTE: do not include <wininet.h> here — it conflicts with <winhttp.h> in a
// single translation unit. The wininet-based connectivity probe lives in
// Connectivity.cpp.

#include <vector>
#include <cstdlib>

namespace qgc {

namespace {

struct ParsedUrl {
    bool         secure = true;
    std::wstring host;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    std::wstring pathWithQuery;
};

bool parseUrl(const std::wstring &url, ParsedUrl &out)
{
    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    wchar_t hostBuf[256] = {0};
    wchar_t pathBuf[2048] = {0};
    wchar_t extraBuf[2048] = {0};
    comp.lpszHostName = hostBuf;     comp.dwHostNameLength = ARRAYSIZE(hostBuf);
    comp.lpszUrlPath = pathBuf;      comp.dwUrlPathLength = ARRAYSIZE(pathBuf);
    comp.lpszExtraInfo = extraBuf;   comp.dwExtraInfoLength = ARRAYSIZE(extraBuf);

    if (!::WinHttpCrackUrl(url.c_str(), 0, 0, &comp)) {
        return false;
    }

    out.secure = (comp.nScheme == INTERNET_SCHEME_HTTPS);
    out.host = hostBuf;
    out.port = comp.nPort;
    out.pathWithQuery = std::wstring(pathBuf) + extraBuf;
    if (out.pathWithQuery.empty()) {
        out.pathWithQuery = L"/";
    }
    return true;
}

} // namespace

DownloadResult downloadToFile(const std::wstring &url, const std::wstring &destPath,
                              const ProgressFn &progress, const CancelFn &cancel)
{
    ParsedUrl parsed;
    if (!parseUrl(url, parsed)) {
        Logger::error(L"download: unparseable url " + url);
        return DownloadResult::InvalidUrl;
    }
    if (!parsed.secure) {
        // Security: refuse plain-HTTP sources outright.
        Logger::error(L"download: refusing non-HTTPS url " + url);
        return DownloadResult::InvalidUrl;
    }

    HINTERNET session = ::WinHttpOpen(L"QGroundControlLauncher/1.0",
                                      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        Logger::error(L"download: WinHttpOpen failed");
        return DownloadResult::Offline;
    }

    ::WinHttpSetTimeouts(session, 15000, 15000, 30000, 30000);

    HINTERNET connect = ::WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0);
    if (!connect) {
        ::WinHttpCloseHandle(session);
        Logger::warning(L"download: cannot connect to " + parsed.host);
        return DownloadResult::Offline;
    }

    const DWORD requestFlags = WINHTTP_FLAG_SECURE | WINHTTP_FLAG_REFRESH;
    HINTERNET request = ::WinHttpOpenRequest(connect, L"GET", parsed.pathWithQuery.c_str(),
                                             nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, requestFlags);
    if (!request) {
        ::WinHttpCloseHandle(connect);
        ::WinHttpCloseHandle(session);
        return DownloadResult::Offline;
    }

    // Follow redirects automatically (default), including https->https short links.
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    ::WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    auto fail = [&](DownloadResult r) {
        ::WinHttpCloseHandle(request);
        ::WinHttpCloseHandle(connect);
        ::WinHttpCloseHandle(session);
        return r;
    };

    if (!::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                              WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        Logger::warning(L"download: send failed for " + url);
        return fail(DownloadResult::Offline);
    }
    if (!::WinHttpReceiveResponse(request, nullptr)) {
        Logger::warning(L"download: no response for " + url);
        return fail(DownloadResult::Offline);
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300) {
        Logger::error(L"download: HTTP " + std::to_wstring(statusCode) + L" for " + url);
        return fail(DownloadResult::HttpError);
    }

    uint64_t totalBytes = 0;
    {
        wchar_t lenBuf[32] = {0};
        DWORD lenSize = sizeof(lenBuf);
        if (::WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                                  WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &lenSize, WINHTTP_NO_HEADER_INDEX)) {
            totalBytes = ::_wcstoui64(lenBuf, nullptr, 10);
        }
    }

    HANDLE file = ::CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Logger::error(L"download: cannot create " + destPath);
        return fail(DownloadResult::IoError);
    }

    std::vector<unsigned char> buffer(256 * 1024);
    uint64_t received = 0;
    DownloadResult result = DownloadResult::Success;

    for (;;) {
        if (cancel && cancel()) {
            result = DownloadResult::Cancelled;
            break;
        }
        DWORD avail = 0;
        if (!::WinHttpQueryDataAvailable(request, &avail)) {
            result = DownloadResult::Offline;
            break;
        }
        if (avail == 0) {
            break; // done
        }
        DWORD toRead = avail < buffer.size() ? avail : static_cast<DWORD>(buffer.size());
        DWORD read = 0;
        if (!::WinHttpReadData(request, buffer.data(), toRead, &read) || read == 0) {
            result = DownloadResult::Offline;
            break;
        }
        DWORD written = 0;
        if (!::WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
            result = DownloadResult::IoError;
            break;
        }
        received += read;
        if (progress) {
            progress(received, totalBytes);
        }
    }

    ::CloseHandle(file);
    ::WinHttpCloseHandle(request);
    ::WinHttpCloseHandle(connect);
    ::WinHttpCloseHandle(session);

    if (result != DownloadResult::Success) {
        ::DeleteFileW(destPath.c_str()); // never leave a partial file behind
    }
    return result;
}

} // namespace qgc
