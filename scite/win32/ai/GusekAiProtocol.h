#ifndef GUSEK_AI_PROTOCOL_H
#define GUSEK_AI_PROTOCOL_H

#include "GusekAiDef.h"
#include "GusekAiConfig.h"

struct ChatImageAttachment {
    std::string name;
    std::string base64Png; // data:image/png;base64,...
    HBITMAP hThumb;
};

struct ChatMessage {
    std::string role;      // "user" or "assistant"
    std::string content;   // raw markdown or question
    std::vector<std::string> imageDataUrls; // base64 images if user turn
};

class GusekAiProtocol {
public:
    static std::string EscapeJsonString(const std::string &raw);
    static std::string UnescapeJsonString(const char *str, size_t len);

    static std::string BuildChatRequestJson(
        const std::string &systemPromptWithContext,
        const std::vector<ChatMessage> &history,
        const std::string &userQuestion,
        const std::vector<std::string> &currentImages,
        const GusekAiConfig &config);

    static bool ExtractFencedCode(const std::string &markdown, std::string &outCode);

    // Parses SSE delta text from a line or block of SSE data.
    // Returns true if text was extracted. Sets isDone=true if [DONE] received.
    static bool ParseSseDelta(const char *sseLine, std::string &outText, bool &isDone);
};

// Streaming filter that strips <think>...</think> tags, including tags split across chunks
class ThinkTagFilter {
    std::string buffer;
    bool insideThink;

public:
    ThinkTagFilter();
    void Reset();
    std::string FilterChunk(const char *chunk, size_t len);
    std::string Flush();
};

#endif // GUSEK_AI_PROTOCOL_H
