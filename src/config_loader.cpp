#include "config_loader.h"
#include <fstream>
#include <iostream>
#include <algorithm>

std::optional<nlohmann::json> ConfigLoader::loadJsonFile(
    const std::string& file_path, 
    const std::string& description) {
    
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "警告: " << description << " '" << file_path << "' 不存在。" << std::endl;
        return std::nullopt;
    }

    try {
        nlohmann::json data;
        file >> data;
        return data;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "错误: " << description << " '" << file_path 
                  << "' 格式错误，无法解析JSON: " << e.what() << std::endl;
        return std::nullopt;
    }
}

BackupStrategy ConfigLoader::loadStrategy(const nlohmann::json& json) {
    BackupStrategy strategy;
    
    if (json.contains("strategy")) {
        auto s = json["strategy"];
        strategy.retention_days = s.value("retention_days", 30);
        strategy.max_versions_per_file = s.value("max_versions_per_file", 10);
        strategy.enable_compression = s.value("enable_compression", true);
        strategy.compression_level = s.value("compression_level", 6);
        strategy.compression_threshold = s.value("compression_threshold", 1024);
        strategy.enable_incremental = s.value("enable_incremental", true);
        strategy.incremental_threshold = s.value("incremental_threshold", 1048576);
        strategy.full_backup_interval = s.value("full_backup_interval", 10);
        strategy.delta_ratio_threshold = s.value("delta_ratio_threshold", 0.3f);
        strategy.max_file_size = s.value("max_file_size", 104857600);
    }
    
    return strategy;
}

std::optional<Config> ConfigLoader::loadConfig(const std::string& config_file) {
    auto json_opt = loadJsonFile(config_file, "主配置文件");
    if (!json_opt) {
        return std::nullopt;
    }

    auto json = *json_opt;
    Config config;

    try {
        config.backup_destination_base = json["backup_destination_base"].get<std::string>();
        
        // 加载备份策略
        config.strategy = loadStrategy(json);

        if (json.contains("backup_sources")) {
            for (const auto& source_json : json["backup_sources"]) {
                BackupSource source;
                source.path = source_json["path"].get<std::string>();
                source.enabled = source_json.value("enabled", false);

                if (source_json.contains("presets")) {
                    source.presets = source_json["presets"].get<std::vector<std::string>>();
                } else if (source_json.contains("preset")) {
                    auto preset_val = source_json["preset"];
                    if (preset_val.is_string()) {
                        source.presets.push_back(preset_val.get<std::string>());
                    } else if (preset_val.is_array()) {
                        source.presets = preset_val.get<std::vector<std::string>>();
                    }
                }

                if (source_json.contains("filter")) {
                    FilterConfig filter;
                    auto filter_json = source_json["filter"];
                    std::string mode_str = filter_json.value("mode", "none");
                    
                    if (mode_str == "whitelist") {
                        filter.mode = FilterConfig::Mode::Whitelist;
                    } else if (mode_str == "blacklist") {
                        filter.mode = FilterConfig::Mode::Blacklist;
                    }
                    
                    if (filter_json.contains("extensions")) {
                        filter.extensions = filter_json["extensions"].get<std::vector<std::string>>();
                    }
                    
                    source.custom_filter = filter;
                }

                config.backup_sources.push_back(source);
            }
        }

        return config;
    } catch (const nlohmann::json::exception& e) {
        std::cerr << "解析配置文件时出错: " << e.what() << std::endl;
        return std::nullopt;
    }
}

std::optional<nlohmann::json> ConfigLoader::loadPresets(const std::string& presets_file) {
    return loadJsonFile(presets_file, "预设配置文件");
}

FilterConfig ConfigLoader::mergePresets(
    const std::vector<std::string>& preset_names,
    const nlohmann::json& presets) {
    
    FilterConfig result;
    std::vector<std::string> whitelisted_exts;
    std::vector<std::string> blacklisted_exts;
    bool has_whitelist = false;

    for (const auto& name : preset_names) {
        if (!presets.contains(name)) {
            continue;
        }

        auto preset = presets[name];
        std::string mode_str = preset.value("mode", "none");
        auto exts = preset.value("extensions", std::vector<std::string>());

        if (mode_str == "whitelist") {
            whitelisted_exts.insert(whitelisted_exts.end(), exts.begin(), exts.end());
            has_whitelist = true;
        } else if (mode_str == "blacklist") {
            blacklisted_exts.insert(blacklisted_exts.end(), exts.begin(), exts.end());
        }
    }

    if (has_whitelist) {
        result.mode = FilterConfig::Mode::Whitelist;
        // 从白名单中移除黑名单项
        for (const auto& ext : whitelisted_exts) {
            if (std::find(blacklisted_exts.begin(), blacklisted_exts.end(), ext) == blacklisted_exts.end()) {
                result.extensions.push_back(ext);
            }
        }
    } else if (!blacklisted_exts.empty()) {
        result.mode = FilterConfig::Mode::Blacklist;
        result.extensions = blacklisted_exts;
    }

    return result;
}
