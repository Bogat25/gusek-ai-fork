#ifndef GUSEK_AI_DOWNLOAD_H
#define GUSEK_AI_DOWNLOAD_H

#include "GusekAiDef.h"

typedef void (*DownloadProgressCallback)(unsigned __int64 downloaded, unsigned __int64 total, const char *statusMsg, void *userData);

class GusekAiDownload {
public:
    static bool DownloadWithResume(
        const std::string &url,
        const std::string &destPath,
        const std::string &expectedSha256,
        unsigned __int64 expectedSize,
        volatile LONG *pCancelFlag,
        DownloadProgressCallback callback,
        void *userData,
        std::string &outError,
        HANDLE cancelEvent = NULL);

    static bool VerifyFileSha256(const std::string &filePath, const std::string &expectedSha256,
                                volatile LONG *cancelFlag = NULL);
};

#endif // GUSEK_AI_DOWNLOAD_H
