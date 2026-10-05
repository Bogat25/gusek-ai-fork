#include "GusekAiProtocol.h"
#include <sstream>
#include <iomanip>

static bool HexVal(char c, unsigned int &v) {
    if (c >= '0' && c <= '9') { v = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; return true; }
    return false;
}

static void AppendUtf8(std::string &out, unsigned int cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
    if (cp <= 0x7F) {
        out += (char)cp;
    } else if (cp <= 0x7FF) {
        out += (char)(0xC0 | ((cp >> 6) & 0x1F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0xFFFF) {
        out += (char)(0xE0 | ((cp >> 12) & 0x0F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += (char)(0xF0 | ((cp >> 18) & 0x07));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

std::string GusekAiProtocol::EscapeJsonString(const std::string &raw) {
    std::string out;
    out.reserve(raw.length() + 32);
    for (size_t i = 0; i < raw.length(); i++) {
        unsigned char c = (unsigned char)raw[i];
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                sprintf(buf, "\\u%04x", (int)c);
                out += buf;
            } else {
                out += (char)c;
            }
            break;
        }
    }
    return out;
}

std::string GusekAiProtocol::UnescapeJsonString(const char *str, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\\' && i + 1 < len) {
            char next = str[++i];
            switch (next) {
            case '"':  out += '"'; break;
            case '\\': out += '\\'; break;
            case '/':  out += '/'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {
                if (i + 4 < len) {
                    unsigned int h1, h2, h3, h4;
                    if (HexVal(str[i+1], h1) && HexVal(str[i+2], h2) &&
                        HexVal(str[i+3], h3) && HexVal(str[i+4], h4)) {
                        unsigned int cp = (h1 << 12) | (h2 << 8) | (h3 << 4) | h4;
                        i += 4;
                        // Surrogate pair check
                        if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < len &&
                            str[i+1] == '\\' && str[i+2] == 'u') {
                            unsigned int l1, l2, l3, l4;
                            if (HexVal(str[i+3], l1) && HexVal(str[i+4], l2) &&
                                HexVal(str[i+5], l3) && HexVal(str[i+6], l4)) {
                                unsigned int low = (l1 << 12) | (l2 << 8) | (l3 << 4) | l4;
                                if (low >= 0xDC00 && low <= 0xDFFF) {
                                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                                    i += 6;
                                }
                            }
                        }
                        AppendUtf8(out, cp);
                        break;
                    }
                }
                out += "\\u";
                break;
            }
            default:
                out += next;
                break;
            }
        } else {
            out += str[i];
        }
    }
    return out;
}

std::string GusekAiProtocol::BuildChatRequestJson(
    const std::string &systemPromptWithContext,
    const std::vector<ChatMessage> &history,
    const std::string &userQuestion,
    const std::vector<std::string> &currentImages,
    const GusekAiConfig &config)
{
    const std::string oldPicture = "[A picture was attached here; it is no longer shown.]";
    size_t limit = static_cast<size_t>((std::max)(0, config.keep_history));
    size_t histStart = history.size() > limit ? history.size() - limit : 0;
    size_t currentStart = currentImages.size() > 4 ? currentImages.size() - 4 : 0;
    size_t remaining = 4 - (currentImages.size() - currentStart);
    std::vector<size_t> imageStart(history.size(), 0);
    // Allocate the picture budget backwards so newer turns win.
    for (size_t i = history.size(); i > histStart; ) {
        --i;
        size_t count = history[i].role == "user" ? history[i].imageDataUrls.size() : 0;
        size_t keep = (std::min)(count, remaining);
        imageStart[i] = count - keep;
        remaining -= keep;
    }

    std::string json = "{\"messages\":[{\"role\":\"system\",\"content\":\"" +
        EscapeJsonString(systemPromptWithContext) + "\"}";
    for (size_t i = histStart; i < history.size(); ++i) {
        const ChatMessage &message = history[i];
        json += ",{\"role\":\"" + EscapeJsonString(message.role) + "\",\"content\":";
        if (message.role != "user" || message.imageDataUrls.empty()) {
            json += "\"" + EscapeJsonString(message.content) + "\"}";
            continue;
        }
        if (imageStart[i] == message.imageDataUrls.size()) {
            std::string content = oldPicture + "\n" + message.content;
            json += "\"" + EscapeJsonString(content) + "\"}";
            continue;
        }
        json += "[";
        bool comma = false;
        for (size_t j = imageStart[i]; j < message.imageDataUrls.size(); ++j) {
            if (comma) json += ",";
            json += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" +
                EscapeJsonString(message.imageDataUrls[j]) + "\"}}";
            comma = true;
        }
        std::string content = message.content;
        if (imageStart[i] && content.find(oldPicture) == std::string::npos)
            content = oldPicture + "\n" + content;
        if (comma) json += ",";
        json += "{\"type\":\"text\",\"text\":\"" + EscapeJsonString(content) + "\"}]}";
    }

    json += ",{\"role\":\"user\",\"content\":";
    if (currentImages.empty()) {
        json += "\"" + EscapeJsonString(userQuestion) + "\"}";
    } else {
        json += "[";
        for (size_t i = currentStart; i < currentImages.size(); ++i) {
            if (i != currentStart) json += ",";
            json += "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" +
                EscapeJsonString(currentImages[i]) + "\"}}";
        }
        std::string question = userQuestion.empty() ?
            "What does this picture show? If it is a model, optimization output, or an error, explain it." :
            userQuestion;
        if (currentStart) question = oldPicture + "\n" + question;
        json += ",{\"type\":\"text\",\"text\":\"" + EscapeJsonString(question) + "\"}]}";
    }
    json += "],\"temperature\":" + std::to_string(config.temperature);
    json += ",\"top_p\":" + std::to_string(config.top_p);
    json += ",\"max_tokens\":" + std::to_string(config.n_predict);
    json += ",\"stream\":true,\"chat_template_kwargs\":{\"enable_thinking\":";
    json += config.thinking ? "true}}" : "false}}";
    return json;
}

bool GusekAiProtocol::ExtractFencedCode(const std::string &markdown, std::string &outCode) {
    outCode.clear();
    bool foundAny = false;
    size_t pos = 0;

    while (pos < markdown.length()) {
        size_t fenceStart = markdown.find("```", pos);
        if (fenceStart == std::string::npos) break;

        // Find end of the line with ```
        size_t lineEnd = markdown.find('\n', fenceStart);
        if (lineEnd == std::string::npos) break;

        size_t blockStart = lineEnd + 1;
        size_t fenceEnd = markdown.find("```", blockStart);
        if (fenceEnd == std::string::npos) {
            // Unterminated block to end of text
            std::string block = markdown.substr(blockStart);
            if (!outCode.empty()) outCode += "\n\n";
            outCode += block;
            foundAny = true;
            break;
        }

        std::string block = markdown.substr(blockStart, fenceEnd - blockStart);
        // Trim trailing newline if present
        if (!block.empty() && block[block.length() - 1] == '\n') {
            block = block.substr(0, block.length() - 1);
        }
        if (!block.empty() && block[block.length() - 1] == '\r') {
            block = block.substr(0, block.length() - 1);
        }

        if (!outCode.empty()) outCode += "\n\n";
        outCode += block;
        foundAny = true;

        pos = fenceEnd + 3;
    }

    return foundAny;
}

bool GusekAiProtocol::ParseSseDelta(const char *sseLine, std::string &outText, bool &isDone) {
    outText.clear();
    isDone = false;
    if (!sseLine) return false;

    // Skip leading whitespace
    while (*sseLine == ' ' || *sseLine == '\t' || *sseLine == '\r' || *sseLine == '\n') sseLine++;
    if (strncmp(sseLine, "data:", 5) != 0) return false;

    const char *payload = sseLine + 5;
    while (*payload == ' ' || *payload == '\t') payload++;

    if (strncmp(payload, "[DONE]", 6) == 0) {
        isDone = true;
        return true;
    }

    // Look for "content": in payload
    const char *contentKey = strstr(payload, "\"content\"");
    if (!contentKey) {
        // Valid SSE line without content (e.g. role-only delta or metadata)
        return true;
    }

    const char *valStart = contentKey + 9;
    while (*valStart == ' ' || *valStart == '\t') ++valStart;
    if (*valStart++ != ':') return false;
    while (*valStart == ' ' || *valStart == '\t') valStart++;

    if (*valStart == 'n' && strncmp(valStart, "null", 4) == 0) {
        // First chunk role-only delta
        return true;
    }

    if (*valStart != '"') return true;
    valStart++; // skip opening quote

    // Find closing quote taking backslashes into account
    const char *p = valStart;
    while (*p) {
        if (*p == '\\' && *(p+1)) {
            p += 2;
        } else if (*p == '"') {
            break;
        } else {
            p++;
        }
    }

    if (*p != '"') return true;

    size_t valLen = p - valStart;
    outText = UnescapeJsonString(valStart, valLen);
    return true;
}

ThinkTagFilter::ThinkTagFilter() : insideThink(false) {
}

void ThinkTagFilter::Reset() {
    buffer.clear();
    insideThink = false;
}

std::string ThinkTagFilter::FilterChunk(const char *chunk, size_t len) {
    if (!chunk || len == 0) return "";
    buffer.append(chunk, len);

    std::string output;

    while (!buffer.empty()) {
        if (!insideThink) {
            size_t tagPos = buffer.find("<think>");
            if (tagPos != std::string::npos) {
                output.append(buffer.substr(0, tagPos));
                buffer.erase(0, tagPos + 7);
                insideThink = true;
            } else {
                // Check if buffer ends with a prefix of "<think>"
                size_t keep = 0;
                for (size_t k = 1; k < 7 && k <= buffer.length(); k++) {
                    if (buffer.compare(buffer.length() - k, k, "<think>", k) == 0) {
                        keep = k;
                        break;
                    }
                }
                size_t emitLen = buffer.length() - keep;
                output.append(buffer.substr(0, emitLen));
                buffer.erase(0, emitLen);
                break;
            }
        } else {
            // Inside think block, look for </think>
            size_t endTagPos = buffer.find("</think>");
            if (endTagPos != std::string::npos) {
                buffer.erase(0, endTagPos + 8);
                insideThink = false;
            } else {
                // Check if buffer ends with a prefix of "</think>"
                size_t keep = 0;
                for (size_t k = 1; k < 8 && k <= buffer.length(); k++) {
                    if (buffer.compare(buffer.length() - k, k, "</think>", k) == 0) {
                        keep = k;
                        break;
                    }
                }
                buffer.erase(0, buffer.length() - keep);
                break;
            }
        }
    }

    return output;
}

std::string ThinkTagFilter::Flush() {
    std::string rem;
    if (!insideThink) {
        rem = buffer;
    }
    Reset();
    return rem;
}
