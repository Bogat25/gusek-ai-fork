#include "GusekAiConfig.h"
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <shlobj.h>

static std::string Trim(const std::string &s) {
    size_t start = 0;
    while (start < s.length() && (unsigned char)s[start] <= ' ') start++;
    size_t end = s.length();
    while (end > start && (unsigned char)s[end - 1] <= ' ') end--;
    return s.substr(start, end - start);
}

static std::string ToLower(const std::string &s) {
    std::string res = s;
    for (size_t i = 0; i < res.length(); i++) {
        if (res[i] >= 'A' && res[i] <= 'Z') res[i] = (char)(res[i] + ('a' - 'A'));
    }
    return res;
}

static bool FileExists(const std::string &path) {
    DWORD dwAttrib = GetFileAttributesA(path.c_str());
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

static bool DirExists(const std::string &path) {
    DWORD dwAttrib = GetFileAttributesA(path.c_str());
    return (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

static void ParseIniFile(const std::string &path, std::map<std::string, std::string> &kv) {
    std::ifstream in(path.c_str());
    if (!in.is_open()) return;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
            std::string k = ToLower(Trim(line.substr(0, eq)));
            std::string v = Trim(line.substr(eq + 1));
            // strip optional surrounding quotes
            if (v.length() >= 2 && ((v[0] == '"' && v[v.length() - 1] == '"') || (v[0] == '\'' && v[v.length() - 1] == '\''))) {
                v = v.substr(1, v.length() - 2);
            }
            kv[k] = v;
        }
    }
}

GusekAiConfig::GusekAiConfig()
    : enabled(true), hotkey("T"),
      server_exe("ai/llama/llama-server.exe"),
      model("ai/models/Qwen3.5-4B-Q4_K_M.gguf"),
      model_url("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/Qwen3.5-4B-Q4_K_M.gguf"),
      model_sha256("00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4"),
      model_bytes(2740937888ULL),
      vision(true),
      vision_model("ai/models/Qwen3.5-4B-mmproj-F16.gguf"),
      vision_url("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/mmproj-F16.gguf"),
      vision_sha256("cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864"),
      vision_bytes(672423616ULL),
      image_max_tokens(256),
      host("127.0.0.1"), port(28713),
      autostart(true), startup_timeout(240), request_timeout(120),
      extra_args(""),
      ctx_size(8192), n_predict(1024), threads(0), gpu_layers(0),
      temperature(0.3), top_p(0.9),
      thinking(false), strip_think(true),
      system_prompt_file("ai/system_prompt.txt"),
      context_dir("ai/context"),
      context_max_chars(12000), keep_history(12)
{
}

std::string GusekAiConfig::ResolvePath(const std::string &relOrAbs) const {
    if (relOrAbs.empty()) return "";
    if (relOrAbs.length() >= 2 && (relOrAbs[1] == ':' || (relOrAbs[0] == '\\' && relOrAbs[1] == '\\') || relOrAbs[0] == '/')) {
        return relOrAbs; // already absolute
    }
    std::string base = app_home;
    if (!base.empty() && base[base.length() - 1] != '\\' && base[base.length() - 1] != '/') {
        base += "\\";
    }
    return base + relOrAbs;
}

void GusekAiConfig::Load(const std::string &appDir) {
    app_home = appDir;

    // Determine data_dir (%GUSEK_AI_DATA% or %LOCALAPPDATA%\GusekAI)
    char envData[MAX_PATH] = {0};
    DWORD envLen = GetEnvironmentVariableA("GUSEK_AI_DATA", envData, sizeof(envData));
    if (envLen > 0 && envLen < sizeof(envData)) {
        data_dir = envData;
    } else {
        char localAppData[MAX_PATH] = {0};
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
            data_dir = std::string(localAppData) + "\\GusekAI";
        } else {
            data_dir = app_home + "\\data";
        }
    }
    CreateDirectoryA(data_dir.c_str(), NULL);
    CreateDirectoryA((data_dir + "\\logs").c_str(), NULL);
    CreateDirectoryA((data_dir + "\\models").c_str(), NULL);

    std::map<std::string, std::string> kv;

    // 1. Shipped defaults
    std::string defaultIni = ResolvePath("ai/defaults/GusekAI.ini");
    if (FileExists(defaultIni)) {
        ParseIniFile(defaultIni, kv);
    }

    // 2. User overrides in %LOCALAPPDATA%\GusekAI\GusekAI.ini
    std::string userIni = data_dir + "\\GusekAI.ini";
    if (FileExists(userIni)) {
        ParseIniFile(userIni, kv);
    }

    // Apply values
    if (kv.count("enabled")) {
        std::string v = ToLower(kv["enabled"]);
        enabled = (v == "1" || v == "yes" || v == "true" || v == "on");
    }
    if (kv.count("hotkey")) hotkey = kv["hotkey"];
    if (kv.count("server_exe")) server_exe = kv["server_exe"];
    if (kv.count("model")) model = kv["model"];
    if (kv.count("model_url")) model_url = kv["model_url"];
    if (kv.count("model_sha256")) model_sha256 = kv["model_sha256"];
    if (kv.count("model_bytes")) model_bytes = _strtoui64(kv["model_bytes"].c_str(), NULL, 10);

    if (kv.count("vision")) {
        std::string v = ToLower(kv["vision"]);
        vision = (v == "1" || v == "yes" || v == "true" || v == "on");
    }
    if (kv.count("vision_model")) vision_model = kv["vision_model"];
    if (kv.count("vision_url")) vision_url = kv["vision_url"];
    if (kv.count("vision_sha256")) vision_sha256 = kv["vision_sha256"];
    if (kv.count("vision_bytes")) vision_bytes = _strtoui64(kv["vision_bytes"].c_str(), NULL, 10);
    if (kv.count("image_max_tokens")) image_max_tokens = atoi(kv["image_max_tokens"].c_str());

    if (kv.count("host")) host = kv["host"];
    if (kv.count("port")) port = atoi(kv["port"].c_str());
    if (kv.count("autostart")) {
        std::string v = ToLower(kv["autostart"]);
        autostart = (v == "1" || v == "yes" || v == "true" || v == "on");
    }
    if (kv.count("startup_timeout")) startup_timeout = atoi(kv["startup_timeout"].c_str());
    if (kv.count("request_timeout")) request_timeout = atoi(kv["request_timeout"].c_str());
    if (kv.count("extra_args")) extra_args = kv["extra_args"];

    if (kv.count("ctx_size")) ctx_size = atoi(kv["ctx_size"].c_str());
    if (kv.count("n_predict")) n_predict = atoi(kv["n_predict"].c_str());
    if (kv.count("threads")) threads = atoi(kv["threads"].c_str());
    if (kv.count("gpu_layers")) gpu_layers = atoi(kv["gpu_layers"].c_str());
    if (kv.count("temperature")) temperature = atof(kv["temperature"].c_str());
    if (kv.count("top_p")) top_p = atof(kv["top_p"].c_str());

    if (kv.count("thinking")) {
        std::string v = ToLower(kv["thinking"]);
        thinking = (v == "1" || v == "yes" || v == "true" || v == "on");
    }
    if (kv.count("strip_think")) {
        std::string v = ToLower(kv["strip_think"]);
        strip_think = (v == "1" || v == "yes" || v == "true" || v == "on");
    }

    if (kv.count("system_prompt_file")) system_prompt_file = kv["system_prompt_file"];
    if (kv.count("context_dir")) context_dir = kv["context_dir"];
    if (kv.count("context_max_chars")) context_max_chars = atoi(kv["context_max_chars"].c_str());
    if (kv.count("keep_history")) keep_history = atoi(kv["keep_history"].c_str());

    // Resolve paths
    resolved_server_exe = ResolvePath(server_exe);
    
    // Check if model exists in data_dir, app_home, or peer cache
    resolved_model = ResolvePath(model);
    if (!FileExists(resolved_model)) {
        std::string inData = data_dir + "\\models\\" + model.substr(model.find_last_of("/\\") + 1);
        if (FileExists(inData)) resolved_model = inData;
        else {
            // Check D:\rgui-build\cache and D:\rstudio-build\cache
            std::string fileName = model.substr(model.find_last_of("/\\") + 1);
            if (FileExists("D:\\rgui-build\\cache\\" + fileName)) resolved_model = "D:\\rgui-build\\cache\\" + fileName;
            else if (FileExists("D:\\rstudio-build\\cache\\" + fileName)) resolved_model = "D:\\rstudio-build\\cache\\" + fileName;
            else resolved_model = inData; // target download destination
        }
    }

    resolved_vision_model = ResolvePath(vision_model);
    if (!FileExists(resolved_vision_model)) {
        std::string inData = data_dir + "\\models\\" + vision_model.substr(vision_model.find_last_of("/\\") + 1);
        if (FileExists(inData)) resolved_vision_model = inData;
        else {
            std::string fileName = vision_model.substr(vision_model.find_last_of("/\\") + 1);
            if (FileExists("D:\\rgui-build\\cache\\" + fileName)) resolved_vision_model = "D:\\rgui-build\\cache\\" + fileName;
            else if (FileExists("D:\\rstudio-build\\cache\\" + fileName)) resolved_vision_model = "D:\\rstudio-build\\cache\\" + fileName;
            else resolved_vision_model = inData;
        }
    }

    resolved_prompt_file = ResolvePath(system_prompt_file);
    resolved_context_dir = ResolvePath(context_dir);
    resolved_log_file = data_dir + "\\logs\\llama-server.log";
}

std::string GusekAiConfig::GetSystemPrompt() const {
    if (FileExists(resolved_prompt_file)) {
        std::ifstream in(resolved_prompt_file.c_str(), std::ios::binary);
        if (in.is_open()) {
            std::stringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }
    }
    // Check shipped fallback in ai/defaults/system_prompt.txt
    std::string defPrompt = ResolvePath("ai/defaults/system_prompt.txt");
    if (FileExists(defPrompt)) {
        std::ifstream in(defPrompt.c_str(), std::ios::binary);
        if (in.is_open()) {
            std::stringstream buffer;
            buffer << in.rdbuf();
            return buffer.str();
        }
    }

    // Built-in hardcoded fallback
    return "You are an assistant built into GUSEK. You help one student with the mathematical programming and optimization modelling used in their operations research coursework.\n\n"
           "HOW TO ANSWER\n"
           "- Your main job is linear and integer programming, GNU MathProg / GMPL modelling, GLPK solver diagnostics, and writing up optimization models for this course.\n"
           "- If a question is about something else, answer it briefly and plainly. Do not refuse ordinary questions, and do not lecture or philosophise. Where it fits, offer to get back to the course.\n"
           "- Put every piece of runnable model or data code in a fenced block, for example starting with ```mathprog and ending with ```.\n"
           "- Keep models clean and well-commented. Explain decision variables, objective, and constraints clearly.\n\n"
           "WHEN DEBUGGING\n"
           "- Ask for the exact solver message and model lines if not provided. Explain what the error means before giving a fix.\n"
           "- Give the smallest change that fixes the problem, not a complete rewrite.\n\n"
           "WHEN A PICTURE IS ATTACHED\n"
           "- First say briefly what you see in it (graph, network, table, error). Then answer.\n\n"
           "The student reads, checks and runs all code themselves. Nothing you write is executed automatically.\n";
}

struct ScoredContextFile {
    std::string path;
    std::string filename;
    int score;
    std::string content;
};

static bool CompareScored(const ScoredContextFile &a, const ScoredContextFile &b) {
    if (a.score != b.score) return a.score > b.score;
    return a.filename < b.filename;
}

std::string GusekAiConfig::GetCourseContext(const std::string &question, const std::string &activeDocDir) const {
    std::vector<std::string> dirsToScan;
    if (!activeDocDir.empty()) {
        std::string localCtx = activeDocDir + "\\.ai-context";
        if (DirExists(localCtx)) dirsToScan.push_back(localCtx);
        if (DirExists(activeDocDir)) dirsToScan.push_back(activeDocDir);
    }
    if (DirExists(resolved_context_dir)) {
        dirsToScan.push_back(resolved_context_dir);
    }
    std::string userCtx = data_dir + "\\context";
    if (DirExists(userCtx)) {
        dirsToScan.push_back(userCtx);
    }

    if (dirsToScan.empty()) return "";

    // Extract search words of length >= 4 from question
    std::set<std::string> searchWords;
    std::string current;
    for (size_t i = 0; i <= question.length(); i++) {
        char c = (i < question.length()) ? question[i] : ' ';
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            current += (char)tolower((unsigned char)c);
        } else {
            if (current.length() >= 4) {
                searchWords.insert(current);
            }
            current.clear();
        }
    }

    std::vector<ScoredContextFile> candidates;
    std::set<std::string> seenPaths;

    for (size_t d = 0; d < dirsToScan.size(); d++) {
        std::string pattern = dirsToScan[d] + "\\*.*";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    std::string fname = fd.cFileName;
                    std::string lowerName = ToLower(fname);
                    if (lowerName == "readme.txt" || lowerName == "readme.md") continue;

                    // Match extension
                    size_t dot = lowerName.find_last_of('.');
                    if (dot != std::string::npos) {
                        std::string ext = lowerName.substr(dot);
                        if (ext == ".txt" || ext == ".md" || ext == ".csv" ||
                            ext == ".mod" || ext == ".dat" || ext == ".lp" || ext == ".mps") {
                            std::string fullPath = dirsToScan[d] + "\\" + fname;
                            if (seenPaths.count(fullPath) == 0) {
                                seenPaths.insert(fullPath);
                                std::ifstream in(fullPath.c_str(), std::ios::binary);
                                if (in.is_open()) {
                                    std::stringstream sb;
                                    sb << in.rdbuf();
                                    std::string content = sb.str();
                                    int score = 0;
                                    std::string lowerContent = ToLower(content);
                                    for (std::set<std::string>::iterator it = searchWords.begin(); it != searchWords.end(); ++it) {
                                        size_t pos = 0;
                                        while ((pos = lowerContent.find(*it, pos)) != std::string::npos) {
                                            score++;
                                            pos += it->length();
                                        }
                                    }
                                    ScoredContextFile item;
                                    item.path = fullPath;
                                    item.filename = fname;
                                    item.score = score;
                                    item.content = content;
                                    candidates.push_back(item);
                                }
                            }
                        }
                    }
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
    }

    if (candidates.empty()) return "";

    std::sort(candidates.begin(), candidates.end(), CompareScored);

    std::string result = "\n\nCOURSE REFERENCE MATERIAL\n\n";
    int charsLeft = context_max_chars;

    for (size_t i = 0; i < candidates.size() && charsLeft > 200; i++) {
        std::string header = "--- Excerpt from: " + candidates[i].filename + " ---\n";
        result += header;
        charsLeft -= (int)header.length();

        if ((int)candidates[i].content.length() <= charsLeft) {
            result += candidates[i].content + "\n\n";
            charsLeft -= (int)candidates[i].content.length() + 2;
        } else {
            result += candidates[i].content.substr(0, (size_t)charsLeft) + "\n[...remainder omitted for length...]\n\n";
            charsLeft = 0;
            break;
        }
    }

    return result;
}
