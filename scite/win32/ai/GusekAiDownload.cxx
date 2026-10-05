#include "GusekAiDownload.h"
#include "GusekAiHttp.h"
#include <winhttp.h>
#include <bcrypt.h>
#include <io.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

bool GusekAiDownload::VerifyFileSha256(const std::string &filePath, const std::string &expectedSha256, volatile LONG *cancelFlag, HANDLE cancelEvent) {
    if (expectedSha256.size() != 64 ||
        expectedSha256.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
    HANDLE hFile = CreateFileW(Utf8ToWide(filePath).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) == 0) {
        if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) == 0) {
            BYTE buffer[65536];
            DWORD bytesRead = 0;
            bool readOk = true;
            while (true) {
                if (!ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL)) { readOk = false; break; }
                if (!bytesRead) break;
                if ((cancelFlag && InterlockedCompareExchange(cancelFlag, 0, 0)) ||
                    (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0)) { readOk = false; break; }
                if (BCryptHashData(hHash, buffer, bytesRead, 0) != 0) { readOk = false; break; }
            }
            BYTE hash[32];
            if (BCryptFinishHash(hHash, hash, sizeof(hash), 0) == 0) {
                char hex[65];
                for (int i = 0; i < 32; i++) {
                    sprintf(hex + i * 2, "%02x", hash[i]);
                }
                hex[64] = '\0';
                ok = readOk && (!cancelEvent || WaitForSingleObject(cancelEvent, 0) != WAIT_OBJECT_0) && (!cancelFlag || !InterlockedCompareExchange(cancelFlag, 0, 0)) &&
                    (_stricmp(hex, expectedSha256.c_str()) == 0);
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
    std::string &outError,
    HANDLE cancelEvent)
{
    outError.clear();
    if (pCancelFlag && InterlockedCompareExchange(pCancelFlag, 0, 0)) {
        outError = "Download paused by user";
        return false;
    }

    // Check if final destination already matches
    if (VerifyFileSha256(destPath, expectedSha256, pCancelFlag)) {
        if (callback) callback(expectedSize, expectedSize, "Model verified in cache", userData);
        return true;
    }

    std::string partPath = destPath + ".part";
    unsigned __int64 existingSize = 0;

    HANDLE hPart = CreateFileW(Utf8ToWide(partPath).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPart != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER li;
        if (GetFileSizeEx(hPart, &li)) {
            existingSize = (unsigned __int64)li.QuadPart;
        }
        CloseHandle(hPart);

        if (existingSize == expectedSize && VerifyFileSha256(partPath, expectedSha256, pCancelFlag)) {
            if (!MoveFileExW(Utf8ToWide(partPath).c_str(), Utf8ToWide(destPath).c_str(), MOVEFILE_REPLACE_EXISTING)) {
                outError = "Failed to rename verified model into place.";
                return false;
            }
            if (callback) callback(expectedSize, expectedSize, "Download complete", userData);
            return true;
        }
        if (expectedSize && existingSize >= expectedSize) {
            // Corrupt or outdated part file
            DeleteFileW(Utf8ToWide(partPath).c_str());
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

    size_t parentEnd = destPath.find_last_of("/\\");
    std::wstring parent = Utf8ToWide(parentEnd == std::string::npos ? "." : destPath.substr(0, parentEnd));
    ULARGE_INTEGER available;
    if (!GetDiskFreeSpaceExW(parent.c_str(), &available, NULL, NULL)) {
        outError = "The model folder is not writable or does not exist";
        return false;
    }
    if (expectedSize > existingSize && available.QuadPart < expectedSize - existingSize) {
        outError = "Not enough free disk space for the model download";
        return false;
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

    DWORD accessType = (host == L"127.0.0.1" || host == L"localhost") ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
    GusekAiHttpRequest http(cancelEvent, pCancelFlag, 30000);
    DWORD reqFlags = urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    std::wstring headers;
    bool usingRange = existingSize > 0;
    if (usingRange) headers = L"Range: bytes=" + std::to_wstring(existingSize) + L"-\r\n";
    if (!http.Open(host, urlComp.nPort, L"GET", path, reqFlags, accessType) ||
        !http.Send(headers) || !http.Receive()) {
        outError = http.IsCancelled() ? "Download paused by user" :
            "Network error during download request (WinHTTP error " + std::to_string(http.Error()) + ")";
        return false;
    }
    DWORD statusCode = 0;
    if (!http.Status(statusCode)) {
        outError = "Could not read download response status";
        return false;
    }

    DWORD fileDisposition = OPEN_ALWAYS;
    if (usingRange && statusCode == 206) {
        std::wstring range;
        unsigned __int64 first = 0, last = 0, total = 0;
        if (!http.Header(WINHTTP_QUERY_CONTENT_RANGE, range) ||
            swscanf(range.c_str(), L"bytes %llu-%llu/%llu", &first, &last, &total) != 3 ||
            first != existingSize || last < first || last >= total || (expectedSize && total != expectedSize)) {
            outError = "Invalid download range response; partial file kept for resume";
            return false;
        }
        fileDisposition = OPEN_ALWAYS;
    } else if (statusCode == 200) {
        // Range ignored or fresh download, truncate
        existingSize = 0;
        fileDisposition = CREATE_ALWAYS;
    } else {
        outError = "Server returned HTTP status " + std::to_string(statusCode);
        return false;
    }

    HANDLE hOut = CreateFileW(Utf8ToWide(partPath).c_str(), GENERIC_WRITE, 0, NULL, fileDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
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

    while (true) {
        if (!http.Read(buffer, sizeof(buffer), bytesRead)) {
            outError = http.IsCancelled() ? "Download paused by user" :
                "Network error during download (WinHTTP error " + std::to_string(http.Error()) + ")";
            success = false;
            break;
        }
        if (bytesRead == 0) break; // Finished stream

        if (pCancelFlag && *pCancelFlag) {
            outError = "Download paused by user";
            success = false;
            break;
        }

        if (expectedSize && bytesRead > expectedSize - currentDownloaded) {
            outError = "Download exceeded the expected model size";
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

    if (!success) return false;
    if (http.IsCancelled()) {
        outError = "Download paused by user";
        return false;
    }
    if (expectedSize && currentDownloaded != expectedSize) {
        outError = "Download interrupted; partial file kept for resume";
        return false;
    }

    // Verify SHA-256 of completed part file
    if (callback) callback(currentDownloaded, expectedSize, "Verifying SHA-256...", userData);
    if (!VerifyFileSha256(partPath, expectedSha256, pCancelFlag)) {
        if (http.IsCancelled()) {
            outError = "Download paused by user";
            return false;
        }
        DeleteFileW(Utf8ToWide(partPath).c_str());
        outError = "Downloaded file checksum verification failed (SHA-256 mismatch). File removed.";
        return false;
    }

    // Atomic move to final destination
    if (!MoveFileExW(Utf8ToWide(partPath).c_str(), Utf8ToWide(destPath).c_str(), MOVEFILE_REPLACE_EXISTING)) {
        outError = "Failed to rename verified model into place.";
        return false;
    }

    if (callback) callback(expectedSize, expectedSize, "Ready", userData);
    return true;
}
