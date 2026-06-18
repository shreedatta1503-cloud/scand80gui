#include "Hash.h"
#include "Logger.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <vector>
#include <cwctype>

#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif

namespace qgc {

namespace {
std::wstring toHex(const std::vector<unsigned char> &bytes)
{
    static const wchar_t *digits = L"0123456789abcdef";
    std::wstring out;
    out.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        out.push_back(digits[b >> 4]);
        out.push_back(digits[b & 0x0F]);
    }
    return out;
}
} // namespace

std::optional<std::wstring> sha256File(const std::wstring &path)
{
    HANDLE file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        Logger::error(L"sha256: cannot open " + path);
        return std::nullopt;
    }

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::wstring> result;

    auto cleanup = [&]() {
        if (hash) ::BCryptDestroyHash(hash);
        if (alg)  ::BCryptCloseAlgorithmProvider(alg, 0);
        ::CloseHandle(file);
    };

    if (!NT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        cleanup();
        return std::nullopt;
    }

    DWORD hashLen = 0, cbData = 0;
    if (!NT_SUCCESS(::BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
                                        sizeof(hashLen), &cbData, 0))) {
        cleanup();
        return std::nullopt;
    }

    if (!NT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0))) {
        cleanup();
        return std::nullopt;
    }

    std::vector<unsigned char> buffer(1 << 20); // 1 MiB chunks
    for (;;) {
        DWORD read = 0;
        if (!::ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
            cleanup();
            return std::nullopt;
        }
        if (read == 0) {
            break;
        }
        if (!NT_SUCCESS(::BCryptHashData(hash, buffer.data(), read, 0))) {
            cleanup();
            return std::nullopt;
        }
    }

    std::vector<unsigned char> digest(hashLen);
    if (!NT_SUCCESS(::BCryptFinishHash(hash, digest.data(), hashLen, 0))) {
        cleanup();
        return std::nullopt;
    }

    result = toHex(digest);
    cleanup();
    return result;
}

bool hashEquals(const std::wstring &a, const std::wstring &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned int diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned>(::towlower(a[i])) ^ static_cast<unsigned>(::towlower(b[i]));
    }
    return diff == 0;
}

} // namespace qgc
