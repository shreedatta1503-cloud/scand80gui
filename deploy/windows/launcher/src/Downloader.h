// ----------------------------------------------------------------------------
// Downloader — HTTPS file download via WinHTTP.
//
// Uses the system/IE proxy configuration (WPAD-aware), follows redirects (so
// vendor short links such as https://aka.ms/... work), enforces HTTPS, and
// reports progress. Distinguishes "offline / unreachable" from "HTTP error" so
// the orchestrator can fall back gracefully.
// ----------------------------------------------------------------------------
#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace qgc {

enum class DownloadResult {
    Success,
    InvalidUrl,       // not an https URL, or unparseable
    Offline,          // could not reach the host (DNS/connect/proxy failure)
    HttpError,        // reached server but got a non-2xx status
    IoError,          // could not write the destination file
    Cancelled,
};

// progress(bytesReceived, totalBytes) — totalBytes is 0 when unknown.
using ProgressFn = std::function<void(uint64_t bytesReceived, uint64_t totalBytes)>;
// cancel() — return true to abort the transfer.
using CancelFn = std::function<bool()>;

// Downloads `url` to `destPath` (overwriting). Streams to disk in chunks.
DownloadResult downloadToFile(const std::wstring &url, const std::wstring &destPath,
                              const ProgressFn &progress, const CancelFn &cancel);

// Lightweight connectivity probe used to choose the offline fast-path. Returns
// false if the system reports no usable internet connection.
bool hasInternetConnection();

} // namespace qgc
