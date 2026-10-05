#include "GusekAiModel.h"
#include "GusekAiHttp.h"
#include "GusekAiDownload.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

GusekAiModel::GusekAiModel()
    : m_hJob(NULL), m_hProcess(NULL), m_dwPid(0), m_isOwnedProcess(false)
{
}

GusekAiModel::~GusekAiModel() {
    Stop();
}

bool GusekAiModel::IsOwnedProcessRunning() const {
    if (!m_hProcess) return false;
    DWORD exitCode = 0;
    if (GetExitCodeProcess(m_hProcess, &exitCode)) {
        return (exitCode == STILL_ACTIVE);
    }
    return false;
}

void GusekAiModel::Stop() {
    if (m_hProcess) {
        if (IsOwnedProcessRunning()) {
            TerminateProcess(m_hProcess, 0);
            WaitForSingleObject(m_hProcess, 1000);
        }
        CloseHandle(m_hProcess);
        m_hProcess = NULL;
    }
    if (m_hJob) {
        CloseHandle(m_hJob);
        m_hJob = NULL;
    }
    m_dwPid = 0;
    m_isOwnedProcess = false;
}

bool GusekAiModel::CheckHealth(const std::string &host, int port, HANDLE cancelEvent) {
    GusekAiHttpRequest http(cancelEvent, NULL, 1000);
    DWORD status = 0;
    return http.Open(Utf8ToWide(host), static_cast<INTERNET_PORT>(port), L"GET", L"/health") &&
        http.Send() && http.Receive() && http.Status(status) && status == 200;
}

static std::string ExtractLastLogError(const std::string &logPath) {
    std::ifstream in(Utf8ToWide(logPath).c_str());
    if (!in.is_open()) return "";
    std::string line;
    std::string lastErr;
    while (std::getline(in, line)) {
        std::string lower = line;
        for (size_t i = 0; i < lower.length(); i++) {
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] += ('a' - 'A');
        }
        if (lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos ||
            lower.find("exception") != std::string::npos || lower.find("fatal") != std::string::npos) {
            lastErr = line;
        }
    }
    return lastErr;
}

bool GusekAiModel::EnsureRunning(const GusekAiConfig &config, std::string &outError, HANDLE cancelEvent) {
    outError.clear();

    if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
        outError = "Stopped";
        return false;
    }
    // 1. If server is already answering /health, use it
    if (CheckHealth(config.host, config.port, cancelEvent)) {
        return true;
    }

    if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
        outError = "Stopped";
        return false;
    }
    if (!config.autostart) {
        outError = "Model autostart is disabled and no local server is ready";
        return false;
    }
    // Release handles from an earlier crashed process before starting another.
    Stop();

    // 2. Check if files exist
    DWORD srvAttr = GetFileAttributesW(Utf8ToWide(config.resolved_server_exe).c_str());
    if (srvAttr == INVALID_FILE_ATTRIBUTES || (srvAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        outError = "llama-server executable not found at: " + config.resolved_server_exe;
        return false;
    }

    DWORD modAttr = GetFileAttributesW(Utf8ToWide(config.resolved_model).c_str());
    if (modAttr == INVALID_FILE_ATTRIBUTES || (modAttr & FILE_ATTRIBUTE_DIRECTORY)) {
        outError = "Model file not found. First-run download required.";
        return false;
    }

    if (!GusekAiDownload::VerifyFileSha256(config.resolved_model, config.model_sha256, NULL, cancelEvent)) {
        outError = "Model verification failed or was stopped; check its SHA-256 or download it again";
        return false;
    }
    if (config.vision && GetFileAttributesW(Utf8ToWide(config.resolved_vision_model).c_str()) != INVALID_FILE_ATTRIBUTES &&
        !GusekAiDownload::VerifyFileSha256(config.resolved_vision_model, config.vision_sha256, NULL, cancelEvent)) {
        outError = "Picture reader verification failed or was stopped";
        return false;
    }
    // 3. Setup Job Object
    if (!m_hJob) {
        m_hJob = CreateJobObject(NULL, NULL);
        if (m_hJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
            memset(&li, 0, sizeof(li));
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(m_hJob, JobObjectExtendedLimitInformation, &li, sizeof(li))) {
                Stop();
                outError = "Could not configure model process cleanup";
                return false;
            }
        }
    }

    if (!m_hJob) {
        outError = "Could not create the model process job";
        return false;
    }

    // 4. Build command line
    std::string cmd = "\"" + config.resolved_server_exe + "\"";
    cmd += " -m \"" + config.resolved_model + "\"";
    cmd += " --host " + config.host;
    cmd += " --port " + std::to_string(config.port);
    cmd += " -c " + std::to_string(config.ctx_size);
    cmd += " -ngl " + std::to_string(config.gpu_layers);
    if (config.threads > 0) {
        cmd += " -t " + std::to_string(config.threads);
    }

    if (config.vision) {
        DWORD vAttr = GetFileAttributesW(Utf8ToWide(config.resolved_vision_model).c_str());
        if (vAttr != INVALID_FILE_ATTRIBUTES && !(vAttr & FILE_ATTRIBUTE_DIRECTORY)) {
            cmd += " --mmproj \"" + config.resolved_vision_model + "\"";
            cmd += " --image-max-tokens " + std::to_string(config.image_max_tokens);
        }
    }

    if (!config.extra_args.empty()) {
        cmd += " " + config.extra_args;
    }

    // 5. Open log file for redirection
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hLog = CreateFileW(Utf8ToWide(config.resolved_log_file).c_str(),
                              GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL,
                              NULL);

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (hLog != INVALID_HANDLE_VALUE) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hLog;
        si.hStdError  = hLog;
        si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    // Working directory is the folder containing llama-server.exe so DLLs resolve
    std::string workDir = config.resolved_server_exe;
    size_t lastSlash = workDir.find_last_of("/\\");
    if (lastSlash != std::string::npos) workDir = workDir.substr(0, lastSlash);

    std::wstring cmdBuf = Utf8ToWide(cmd);

    BOOL created = CreateProcessW(
        NULL,
        &cmdBuf[0],
        NULL,
        NULL,
        TRUE, // inherit handles for log
        CREATE_SUSPENDED | CREATE_NO_WINDOW,
        NULL,
        Utf8ToWide(workDir).c_str(),
        &si,
        &pi);

    if (hLog != INVALID_HANDLE_VALUE) {
        CloseHandle(hLog);
    }

    if (!created) {
        outError = "Failed to launch llama-server (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    m_hProcess = pi.hProcess;
    m_dwPid = pi.dwProcessId;
    m_isOwnedProcess = true;
    if (!AssignProcessToJobObject(m_hJob, pi.hProcess) ||
        ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
        CloseHandle(pi.hThread);
        Stop();
        outError = "Could not start the model in its cleanup job";
        return false;
    }
    CloseHandle(pi.hThread);

    // 6. Poll /health until ready or timeout
    DWORD startupMs = static_cast<DWORD>((std::max)(1, (std::min)(config.startup_timeout, 86400))) * 1000;
    DWORD start = GetTickCount();
    while (GetTickCount() - start < startupMs) {
        if (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) {
            Stop();
            outError = "Stopped";
            return false;
        }
        if (!IsOwnedProcessRunning()) {
            std::string lastErr = ExtractLastLogError(config.resolved_log_file);
            if (!lastErr.empty()) {
                outError = "Server exited: " + lastErr;
            } else {
                outError = "Server process exited unexpectedly. See log: " + config.resolved_log_file;
            }
            Stop();
            return false;
        }

        if (CheckHealth(config.host, config.port, cancelEvent)) {
            return true;
        }
        if (cancelEvent) WaitForSingleObject(cancelEvent, 500);
        else Sleep(500);
    }

    Stop();
    outError = "Model server startup timed out after " + std::to_string(config.startup_timeout) + "s";
    return false;
}
