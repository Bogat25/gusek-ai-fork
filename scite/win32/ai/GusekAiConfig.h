#ifndef GUSEK_AI_CONFIG_H
#define GUSEK_AI_CONFIG_H

#include "GusekAiDef.h"

struct GusekAiConfig {
    bool enabled;
    std::string hotkey;

    std::string server_exe;
    std::string model;
    std::string model_url;
    std::string model_sha256;
    unsigned __int64 model_bytes;

    bool vision;
    std::string vision_model;
    std::string vision_url;
    std::string vision_sha256;
    unsigned __int64 vision_bytes;
    int image_max_tokens;

    std::string host;
    int port;

    bool autostart;
    int startup_timeout;
    int request_timeout;

    std::string extra_args;
    int ctx_size;
    int n_predict;
    int threads;
    int gpu_layers;
    double temperature;
    double top_p;

    bool thinking;
    bool strip_think;

    std::string system_prompt_file;
    std::string context_dir;
    int context_max_chars;
    int keep_history;

    // Derived paths
    std::string app_home;
    std::string data_dir;
    std::string resolved_server_exe;
    std::string resolved_model;
    std::string resolved_vision_model;
    std::string resolved_prompt_file;
    std::string resolved_context_dir;
    std::string resolved_log_file;

    GusekAiConfig();
    void Load(const std::string &appDir);
    std::string ResolvePath(const std::string &relOrAbs) const;
    std::string GetSystemPrompt() const;
    std::string GetCourseContext(const std::string &question, const std::string &activeDocDir) const;
};

#endif // GUSEK_AI_CONFIG_H
