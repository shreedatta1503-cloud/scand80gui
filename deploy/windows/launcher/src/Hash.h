// ----------------------------------------------------------------------------
// Hash — SHA-256 over a file using the Windows CNG (BCrypt) API. No OpenSSL,
// no third-party crypto. Used to verify the integrity of every download.
// ----------------------------------------------------------------------------
#pragma once

#include <string>
#include <optional>

namespace qgc {

// Computes the lowercase hex SHA-256 of a file. Returns nullopt on I/O error.
std::optional<std::wstring> sha256File(const std::wstring &path);

// Constant-time-ish comparison of two lowercase hex digests (case-insensitive).
bool hashEquals(const std::wstring &a, const std::wstring &b);

} // namespace qgc
