#ifndef GUSEK_AI_HTTP_H
#define GUSEK_AI_HTTP_H

#include "GusekAiDef.h"
#include <winhttp.h>
#include <limits.h>

// The worker waits for asynchronous WinHTTP operations. Cancellation only
// signals an event; the worker closes its request and waits for the final
// callback before releasing any request data. WinHTTP decodes HTTP chunks.
class GusekAiHttpRequest {
    HINTERNET session, connection, request;
    HANDLE completed, closed, cancelEvent;
    volatile LONG *cancelFlag;
    DWORD error, bytesRead, timeout;
    bool callbackInstalled;

    GusekAiHttpRequest(const GusekAiHttpRequest &);
    GusekAiHttpRequest &operator=(const GusekAiHttpRequest &);

    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status,
                                  LPVOID info, DWORD length) {
        GusekAiHttpRequest *self = reinterpret_cast<GusekAiHttpRequest *>(context);
        if (!self) return;
        switch (status) {
        case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
            SetEvent(self->closed);
            break;
        case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
            self->error = static_cast<WINHTTP_ASYNC_RESULT *>(info)->dwError;
            SetEvent(self->completed);
            break;
        case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
            self->bytesRead = length;
            SetEvent(self->completed);
            break;
        case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
        case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
            SetEvent(self->completed);
            break;
        }
    }

    void Prepare() {
        ResetEvent(completed);
        error = ERROR_SUCCESS;
        bytesRead = 0;
    }

    void CloseRequest() {
        if (!request) return;
        HINTERNET owned = request;
        request = NULL;
        WinHttpCloseHandle(owned);
        if (callbackInstalled) WaitForSingleObject(closed, INFINITE);
    }

    bool Fail(DWORD reason) {
        // An outstanding Read still owns its caller's buffer. Finish teardown
        // before returning cancellation/timeout to that caller.
        CloseRequest();
        error = reason;
        return false;
    }

    bool Wait(BOOL started) {
        if (!started) {
            error = GetLastError();
            if (error != ERROR_IO_PENDING) return Fail(error);
            error = ERROR_SUCCESS;
        }
        DWORD start = GetTickCount();
        for (;;) {
            if (IsCancelled()) {
                return Fail(ERROR_CANCELLED);
            }
            DWORD elapsed = GetTickCount() - start;
            if (elapsed >= timeout) {
                return Fail(ERROR_WINHTTP_TIMEOUT);
            }
            HANDLE events[] = { completed, cancelEvent };
            DWORD slice = (std::min)(timeout - elapsed, static_cast<DWORD>(100));
            DWORD result = WaitForMultipleObjects(cancelEvent ? 2 : 1, events, FALSE, slice);
            if (result == WAIT_OBJECT_0)
                return error == ERROR_SUCCESS ? true : Fail(error);
            if (result == WAIT_OBJECT_0 + 1) {
                return Fail(ERROR_CANCELLED);
            }
            if (result == WAIT_FAILED) {
                return Fail(GetLastError());
            }
        }
    }

public:
    GusekAiHttpRequest(HANDLE cancel = NULL, volatile LONG *flag = NULL,
                       DWORD timeoutMs = 30000)
        : session(NULL), connection(NULL), request(NULL),
          completed(CreateEvent(NULL, TRUE, FALSE, NULL)),
          closed(CreateEvent(NULL, TRUE, FALSE, NULL)),
          cancelEvent(cancel), cancelFlag(flag), error(0), bytesRead(0),
          timeout((std::max)(static_cast<DWORD>(1), timeoutMs)),
          callbackInstalled(false) {}

    ~GusekAiHttpRequest() {
        CloseRequest();
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        if (completed) CloseHandle(completed);
        if (closed) CloseHandle(closed);
    }

    bool IsCancelled() const {
        return (cancelEvent && WaitForSingleObject(cancelEvent, 0) == WAIT_OBJECT_0) ||
            (cancelFlag && InterlockedCompareExchange(cancelFlag, 0, 0) != 0);
    }

    DWORD Error() const { return error; }

    bool Open(const std::wstring &host, INTERNET_PORT port,
              const std::wstring &verb, const std::wstring &path, DWORD flags = 0,
              DWORD access = WINHTTP_ACCESS_TYPE_NO_PROXY) {
        if (!completed || !closed || IsCancelled()) {
            error = IsCancelled() ? ERROR_CANCELLED : ERROR_NOT_ENOUGH_MEMORY;
            return false;
        }
        session = WinHttpOpen(L"GUSEK-AI-Assistant/1.0", access,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (session) {
            int operationTimeout = static_cast<int>((std::min)(timeout, static_cast<DWORD>(INT_MAX)));
            WinHttpSetTimeouts(session, operationTimeout, operationTimeout,
                               operationTimeout, operationTimeout);
            connection = WinHttpConnect(session, host.c_str(), port, 0);
        }
        if (connection) {
            request = WinHttpOpenRequest(connection, verb.c_str(), path.c_str(), NULL,
                WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        }
        if (!request) {
            error = GetLastError();
            return false;
        }
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        if (!WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
            error = GetLastError();
            return false;
        }
        callbackInstalled = WinHttpSetStatusCallback(request, Callback,
            WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0)
            != WINHTTP_INVALID_STATUS_CALLBACK;
        if (!callbackInstalled) error = GetLastError();
        return callbackInstalled;
    }

    bool Send(const std::wstring &headers = L"", const std::string &body = "") {
        if (IsCancelled()) { error = ERROR_CANCELLED; return false; }
        Prepare();
        return Wait(WinHttpSendRequest(request,
            headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
            static_cast<DWORD>(headers.length()),
            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char *>(body.data()),
            static_cast<DWORD>(body.length()), static_cast<DWORD>(body.length()),
            reinterpret_cast<DWORD_PTR>(this)));
    }

    bool Receive() {
        if (IsCancelled()) { error = ERROR_CANCELLED; return false; }
        Prepare();
        return Wait(WinHttpReceiveResponse(request, NULL));
    }

    bool Status(DWORD &status) {
        DWORD size = sizeof(status);
        return WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE;
    }

    bool Read(void *buffer, DWORD capacity, DWORD &size) {
        size = 0;
        if (IsCancelled()) { error = ERROR_CANCELLED; return false; }
        Prepare();
        if (!Wait(WinHttpReadData(request, buffer, capacity, NULL))) return false;
        size = bytesRead;
        return true;
    }
};

#endif
