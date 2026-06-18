#include "Elevation.h"
#include "Common.h"
#include "Logger.h"

#include <windows.h>
#include <shellapi.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>

#include <vector>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace qgc {

bool isProcessElevated()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    bool elevated = false;
    if (::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size)) {
        elevated = elevation.TokenIsElevated != 0;
    }
    ::CloseHandle(token);
    return elevated;
}

bool canWriteToDir(const std::wstring &dir)
{
    const std::wstring probe = joinPath(dir, L".qgc-write-probe.tmp");
    HANDLE h = ::CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(h);
    return true;
}

int runProcess(const std::wstring &exePath, const std::wstring &args, bool elevate)
{
    const bool needElevate = elevate && !isProcessElevated();

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = needElevate ? L"runas" : L"open";
    sei.lpFile = exePath.c_str();
    sei.lpParameters = args.empty() ? nullptr : args.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!::ShellExecuteExW(&sei)) {
        const DWORD err = ::GetLastError();
        if (err == ERROR_CANCELLED) {
            Logger::warning(L"runProcess: user declined elevation for " + fileName(exePath));
        } else {
            Logger::error(L"runProcess: ShellExecuteEx failed (" + std::to_wstring(err) + L") for " + exePath);
        }
        return -1;
    }

    int exitCode = -1;
    if (sei.hProcess) {
        ::WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 0;
        ::GetExitCodeProcess(sei.hProcess, &code);
        exitCode = static_cast<int>(code);
        ::CloseHandle(sei.hProcess);
    }
    return exitCode;
}

bool verifyAuthenticode(const std::wstring &path, const std::wstring &expectedPublisher)
{
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.pFile = &fileInfo;

    const LONG status = ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trustData);

    // Always release the state regardless of result.
    trustData.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &trustData);

    if (status != ERROR_SUCCESS) {
        Logger::error(L"authenticode: untrusted signature for " + fileName(path) +
                      L" (status " + std::to_wstring(status) + L")");
        return false;
    }

    if (expectedPublisher.empty()) {
        return true;
    }

    // Confirm the signer subject name contains the expected publisher.
    bool publisherOk = false;
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    DWORD encoding = 0, contentType = 0, formatType = 0;
    if (::CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
                           CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                           CERT_QUERY_FORMAT_FLAG_BINARY, 0, &encoding, &contentType, &formatType,
                           &store, &msg, nullptr)) {
        DWORD signerSize = 0;
        if (::CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerSize)) {
            std::vector<unsigned char> signerBuf(signerSize);
            if (::CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, signerBuf.data(), &signerSize)) {
                auto *signer = reinterpret_cast<CMSG_SIGNER_INFO *>(signerBuf.data());
                CERT_INFO certInfo{};
                certInfo.Issuer = signer->Issuer;
                certInfo.SerialNumber = signer->SerialNumber;
                PCCERT_CONTEXT cert = ::CertFindCertificateInStore(
                    store, encoding, 0, CERT_FIND_SUBJECT_CERT, &certInfo, nullptr);
                if (cert) {
                    const DWORD nameLen = ::CertGetNameStringW(
                        cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nullptr, 0);
                    std::wstring subject(nameLen, L'\0');
                    ::CertGetNameStringW(cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                                         subject.data(), nameLen);
                    if (!subject.empty() && subject.back() == L'\0') {
                        subject.pop_back();
                    }
                    publisherOk = subject.find(expectedPublisher) != std::wstring::npos;
                    if (!publisherOk) {
                        Logger::error(L"authenticode: publisher mismatch (got '" + subject +
                                      L"', expected '" + expectedPublisher + L"')");
                    }
                    ::CertFreeCertificateContext(cert);
                }
            }
        }
    }
    if (store) ::CertCloseStore(store, 0);
    if (msg)   ::CryptMsgClose(msg);
    return publisherOk;
}

} // namespace qgc
