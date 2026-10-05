#ifndef GUSEK_AI_PANE_H
#define GUSEK_AI_PANE_H

#include "GusekAiDef.h"
#include "GusekAiHost.h"
#include "GusekAiConfig.h"
#include "GusekAiProtocol.h"
#include "GusekAiModel.h"
#include "GusekAiImage.h"
#include "GusekAiRender.h"
#include "GusekAiDownload.h"

class GusekAiPane {
    IGusekAiHost   *m_pHost;
    GusekAiConfig   m_config;
    GusekAiModel    m_model;

    HWND            m_hWnd;
    HWND            m_hStatus;
    HWND            m_hHist;
    HWND            m_hInput;
    HWND            m_btnSend;
    HWND            m_btnStop;
    HWND            m_btnCopy;
    HWND            m_btnToEd;
    HWND            m_btnNew;
    HWND            m_btnAttach;

    HFONT           m_hFontUi;
    HMODULE         m_hMsftEdit;

    // State
    bool            m_busy;
    volatile LONG   m_cancel;
    volatile LONG   m_downloading;
    bool            m_mouseDragging;
    LONG            m_currentTurnStartPos;
    LONG            m_generation;
    HANDLE          m_cancelEvent;

    std::string     m_lastAnswer;
    std::string     m_currentStreamingReply;
    std::vector<ChatMessage> m_history;
    ChatMessage     m_pendingQuestion;
    std::vector<ChatImageAttachment> m_attachedImages;

    // Thread synchronization
    CRITICAL_SECTION m_cs;
    std::string     m_pendingBuffer;
    volatile LONG   m_postPending;
    HANDLE          m_hWorkerThread;
    HANDLE          m_hDownloadThread;

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK InputSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK HistSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    static DWORD WINAPI WorkerThreadProc(LPVOID lpParam);
    static DWORD WINAPI DownloadThreadProc(LPVOID lpParam);

    void LayoutChildren(int width, int height);
    void SetStatus(const std::string &status);
    void AppendTextToInput(const std::string &text);
    void ShowAttachMenu();
    void JoinWorkers();
    void DiscardWorkerMessages();
    void PostWorkerMessage(UINT message, LONG generation, bool success,
                           const std::string &text = "");

public:
    GusekAiPane(IGusekAiHost *pHost);
    ~GusekAiPane();

    bool Create(HWND hParent, int x, int y, int width, int height);
    void Destroy();
    HWND GetHWND() const { return m_hWnd; }

    void SetPosition(int x, int y, int width, int height);
    void Show(bool bShow);
    bool IsVisible() const;

    void SendQuestion();
    void StopWork();
    void CopyLastCode();
    void InsertCodeToEditor();
    void NewChat();
    void AttachImageFile();
    void AttachClipboardImage();
    void ClearAttachedImages();
    void TriggerFirstRunDownload(bool isVisionOnly = false);

    void DrainPendingBuffer();
    void FinishTurn(bool success);
    void HandleDownloadDone(bool success);

    void OnHostQuit();
};

#endif // GUSEK_AI_PANE_H
