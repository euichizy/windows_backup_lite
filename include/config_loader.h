#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>
#include "backup_handler.h"
#include "backup_strategy.h"

struct BackupSource {
    std::string path;
    bool enabled = false;
    std::vector<std::string> presets;
    std::optional<FilterConfig> custom_filter;
};

struct Config {
    std::string backup_destination_base;
    std::vector<BackupSource> backup_sources;
    BackupStrategy strategy;  // 新增：备份策略配置
};

class ConfigLoader {
public:
    static std::optional<Config> loadConfig(const std::string& config_file);
    static std::optional<nlohmann::json> loadPresets(const std::string& presets_file);
    static FilterConfig mergePresets(const std::vector<std::string>& preset_names,
                                     const nlohmann::json& presets);
    static BackupStrategy loadStrategy(const nlohmann::json& json);
    
    // 新增：合并预设和自定义过滤器
    static FilterConfig mergeFilters(const std::vector<std::string>& preset_names,
                                     const nlohmann::json& presets,
                                     const std::optional<FilterConfig>& custom_filter);

private:
    static std::optional<nlohmann::json> loadJsonFile(const std::string& file_path,
                                                      const std::string& description);
};
