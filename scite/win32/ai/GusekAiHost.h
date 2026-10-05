#ifndef GUSEK_AI_HOST_H
#define GUSEK_AI_HOST_H

#include "GusekAiDef.h"

class IGusekAiHost {
public:
    virtual ~IGusekAiHost() {}

    // Document context and editing
    virtual std::string GetActiveDocumentText() = 0;
    virtual std::string GetActiveDocumentPath() = 0;
    virtual std::string GetActiveDocumentFormat() = 0;
    virtual bool InsertTextAtCaret(const char *utf8Text) = 0;
    virtual bool CreateNewMathProgDocument(const char *utf8Text) = 0;

    // Solver output and error inspection
    virtual std::string GetLastSolverError() = 0;
    virtual std::string GetLastSolverOutput() = 0;
    virtual std::string GetRecentOutput(int maxChars) = 0;

    // Window management and properties
    virtual HWND GetMainHWND() = 0;
    virtual std::string GetProperty(const char *key) = 0;
    virtual void SetAssistantVisible(bool visible) = 0;
    virtual bool IsAssistantVisible() = 0;
};

#endif // GUSEK_AI_HOST_H
