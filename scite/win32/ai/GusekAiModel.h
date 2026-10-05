#ifndef GUSEK_AI_MODEL_H
#define GUSEK_AI_MODEL_H

#include "GusekAiDef.h"
#include "GusekAiConfig.h"

class GusekAiModel {
    HANDLE m_hJob;
    HANDLE m_hProcess;
    DWORD  m_dwPid;
    bool   m_isOwnedProcess;

public:
    GusekAiModel();
    ~GusekAiModel();

    // Starts llama-server if not already answering /health.
    // Returns true when server is ready, false on error.
    bool EnsureRunning(const GusekAiConfig &config, std::string &outError);

    // Checks if loopback server responds 200 to /health
    static bool CheckHealth(const std::string &host, int port);

    // Stops owned server process
    void Stop();

    bool IsOwnedProcessRunning() const;
};

#endif // GUSEK_AI_MODEL_H
