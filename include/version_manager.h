#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include "backup_strategy.h"

namespace fs = std::filesystem;

// 版本信息
struct VersionInfo {
    fs::path file_path;
    std::chrono::system_clock::time_point timestamp;
    size_t file_size;
    bool is_compressed;
    bool is_incremental;
    int version_number;
};

class VersionManager {
public:
    VersionManager(const std::string& backup_base_path, const BackupStrategy& strategy);
    
    // 清理过期版本
    size_t cleanupOldVersions(const std::string& relative_path);
    
    // 获取文件的所有版本
    std::vector<VersionInfo> getFileVersions(const std::string& relative_path);
    
    // 获取最新版本
    std::optional<VersionInfo> getLatestVersion(const std::string& relative_path);
    
    // 获取下一个版本号
    int getNextVersionNumber(const std::string& relative_path);
    
    // 清理整个备份目录的过期版本
    size_t cleanupAllOldVersions();
    
    // 获取备份目录总大小
    size_t getTotalBackupSize();

private:
    std::string backup_base_path_;
    BackupStrategy strategy_;
    
    // 解析版本文件名，提取时间戳和版本号
    std::optional<VersionInfo> parseVersionFile(const fs::path& file_path);
    
    // 检查版本是否过期
    bool isVersionExpired(const VersionInfo& version);
};
