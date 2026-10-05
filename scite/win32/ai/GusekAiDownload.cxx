#include "GusekAiDownload.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <io.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

bool GusekAiDownload::VerifyFileSha256(const std::string &filePath, const std::string &expectedSha256) {
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            BYTE buffer[65536];
            DWORD bytesRead = 0;
            while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
                BCryptHashData(hHash, buffer, bytesRead, 0);
            }
            BYTE hash[32];
            if (BCryptFinishHash(hHash, hash, sizeof(hash), 0) == 0) {
                char hex[65];
                for (int i = 0; i < 32; i++) {
                    sprintf(hex + i * 2, "%02x", hash[i]);
                }
                hex[64] = '\0';
                ok = (_stricmp(hex, expectedSha256.c_str()) == 0);
            }
            BCryptDestroyHash(hHash);
        }
        BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    CloseHandle(hFile);
    return ok;
}

bool GusekAiDownload::DownloadWithResume(
    const std::string &url,
    const std::string &destPath,
    const std::string &expectedSha256,
    unsigned __int64 expectedSize,
    volatile LONG *pCancelFlag,
    DownloadProgressCallback callback,
    void *userData,
    std::string &outError)
{
    outError.clear();

    // Check if final destination already matches
    if (VerifyFileSha256(destPath, expectedSha256)) {
        if (callback) callback(expectedSize, expectedSize, "Model verified in cache", userData);
        return true;
    }

    std::string partPath = destPath + ".part";
    unsigned __int64 existingSize = 0;

    HANDLE hPart = CreateFileA(partPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPart != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER li;
        if (GetFileSizeEx(hPart, &li)) {
            existingSize = (unsigned __int64)li.QuadPart;
        }
        CloseHandle(hPart);

        if (existingSize == expectedSize && VerifyFileSha256(partPath, expectedSha256)) {
            MoveFileExA(partPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (callback) callback(expectedSize, expectedSize, "Download complete", userData);
            return true;
        }
        if (existingSize > expectedSize) {
            // Corrupt or outdated part file
            DeleteFileA(partPath.c_str());
            existingSize = 0;
        }
    }

    // Disk space check
    std::string dir = destPath;
    size_t lastSlash = dir.find_last_of("/\\");
    if (lastSlash != std::string::npos) dir = dir.substr(0, lastSlash);
    ULARGE_INTEGER freeBytes;
    if (GetDiskFreeSpaceExA(dir.c_str(), &freeBytes, NULL, NULL)) {
        unsigned __int64 needed = expectedSize > existingSize ? (expectedSize - existingSize) : 0;
        if (freeBytes.QuadPart < needed + (50ULL * 1024 * 1024)) {
            outError = "Insufficient free disk space for model download.";
            return false;
        }
    }

    std::wstring wUrl = Utf8ToWide(url);
    URL_COMPONENTSW urlComp;
    memset(&urlComp, 0, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        outError = "Invalid download URL: " + url;
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.lpszExtraInfo && urlComp.dwExtraInfoLength > 0) {
        path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    DWORD accessType = (host == L"127.0.0.1" || host == L"localhost") ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    HINTERNET hSession = WinHttpOpen(
        L"GUSEK-AI-Assistant/1.0",
        accessType,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!hSession) {
        outError = "Failed to initialize WinHTTP session";
        return false;
    }
    WinHttpSetTimeouts(hSession, 5000, 5000, 10000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        outError = "Failed to connect to host: " + WideToUtf8(host);
        return false;
    }

    DWORD reqFlags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Failed to open WinHTTP request";
        return false;
    }

    // Set Range header if resuming
    bool usingRange = false;
    if (existingSize > 0) {
        std::wstring rangeHdr = L"Range: bytes=" + std::to_wstring(existingSize) + L"-";
        WinHttpAddRequestHeaders(hRequest, rangeHdr.c_str(), (DWORD)rangeHdr.length(), WINHTTP_ADDREQ_FLAG_ADD);
        usingRange = true;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Network error during download request (WinHTTP error " + std::to_string(err) + ")";
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    DWORD fileDisposition = OPEN_ALWAYS;
    if (usingRange && statusCode == 206) {
        // Range accepted, append
        fileDisposition = OPEN_ALWAYS;
    } else if (statusCode == 200) {
        // Range ignored or fresh download, truncate
        existingSize = 0;
        fileDisposition = CREATE_ALWAYS;
    } else {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Server returned HTTP status " + std::to_string(statusCode);
        return false;
    }

    HANDLE hOut = CreateFileA(partPath.c_str(), GENERIC_WRITE, 0, NULL, fileDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        outError = "Could not open target part file for writing: " + partPath;
        return false;
    }

    if (existingSize > 0) {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)existingSize;
        SetFilePointerEx(hOut, li, NULL, FILE_BEGIN);
    }

    unsigned __int64 currentDownloaded = existingSize;
    BYTE buffer[65536];
    DWORD bytesRead = 0;
    bool success = true;

    while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead)) {
        if (bytesRead == 0) break; // Finished stream

        if (pCancelFlag && *pCancelFlag) {
            outError = "Download paused by user";
            success = false;
            break;
        }

        DWORD bytesWritten = 0;
        if (!WriteFile(hOut, buffer, bytesRead, &bytesWritten, NULL) || bytesWritten != bytesRead) {
            outError = "Disk write failure during download";
            success = false;
            break;
        }

        currentDownloaded += bytesWritten;
        if (callback) {
            char status[128];
            double dGb = (double)currentDownloaded / (1024.0 * 1024.0 * 1024.0);
            double tGb = (double)expectedSize / (1024.0 * 1024.0 * 1024.0);
            int pct = expectedSize > 0 ? (int)((currentDownloaded * 100) / expectedSize) : 0;
            sprintf(status, "Downloading %.1f of %.1f GB (%d%%)", dGb, tGb, pct);
            callback(currentDownloaded, expectedSize, status, userData);
        }
    }

    CloseHandle(hOut);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    if (!success) return false;

    // Verify SHA-256 of completed part file
    if (callback) callback(currentDownloaded, expectedSize, "Verifying SHA-256...", userData);
    if (!VerifyFileSha256(partPath, expectedSha256)) {
        DeleteFileA(partPath.c_str());
        outError = "Downloaded file checksum verification failed (SHA-256 mismatch). File removed.";
        return false;
    }

    // Atomic move to final destination
    if (!MoveFileExA(partPath.c_str(), destPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        outError = "Failed to rename verified model into place.";
        return false;
    }

    if (callback) callback(expectedSize, expectedSize, "Ready", userData);
    return true;
}
