#include "version_manager.h"
#include "logger.h"
#include <algorithm>
#include <regex>

VersionManager::VersionManager(const std::string& backup_base_path, const BackupStrategy& strategy)
    : backup_base_path_(backup_base_path), strategy_(strategy) {
}

std::optional<VersionInfo> VersionManager::parseVersionFile(const fs::path& file_path) {
    // 文件名格式: filename.YYYYMMDD_HHMMSS.ext 或 filename.YYYYMMDD_HHMMSS.ext.gz
    std::string filename = file_path.filename().string();
    
    // 正则表达式匹配时间戳
    std::regex pattern(R"(\.(\d{8})_(\d{6}))");
    std::smatch match;
    
    if (!std::regex_search(filename, match, pattern)) {
        return std::nullopt;
    }

    VersionInfo info;
    info.file_path = file_path;
    
    // 解析时间戳
    std::string date_str = match[1].str();
    std::string time_str = match[2].str();
    
    std::tm tm = {};
    tm.tm_year = std::stoi(date_str.substr(0, 4)) - 1900;
    tm.tm_mon = std::stoi(date_str.substr(4, 2)) - 1;
    tm.tm_mday = std::stoi(date_str.substr(6, 2));
    tm.tm_hour = std::stoi(time_str.substr(0, 2));
    tm.tm_min = std::stoi(time_str.substr(2, 2));
    tm.tm_sec = std::stoi(time_str.substr(4, 2));
    
    info.timestamp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    
    // 检查文件大小
    std::error_code ec;
    info.file_size = fs::file_size(file_path, ec);
    if (ec) {
        return std::nullopt;
    }
    
    // 检查是否压缩
    info.is_compressed = (filename.length() >= 3 && 
                         filename.substr(filename.length() - 3) == ".gz");
    
    // 检查是否增量（简化：通过文件名中的 .delta 标记）
    info.is_incremental = filename.find(".delta") != std::string::npos;
    
    // 版本号（使用时间戳的秒数，避免溢出）
    info.version_number = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(
            info.timestamp.time_since_epoch()
        ).count() % 2147483647  // 取模避免溢出
    );
    
    return info;
}

bool VersionManager::isVersionExpired(const VersionInfo& version) {
    auto now = std::chrono::system_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::hours>(now - version.timestamp).count() / 24;
    
    return age > strategy_.retention_days;
}

std::vector<VersionInfo> VersionManager::getFileVersions(const std::string& relative_path) {
    std::vector<VersionInfo> versions;
    
    // 遍历所有日期目录
    std::error_code ec;
    for (const auto& date_entry : fs::directory_iterator(backup_base_path_, ec)) {
        if (!date_entry.is_directory(ec)) {
            continue;
        }
        
        fs::path target_dir = date_entry.path() / fs::path(relative_path).parent_path();
        if (!fs::exists(target_dir, ec)) {
            continue;
        }
        
        // 查找匹配的版本文件
        std::string base_name = fs::path(relative_path).stem().string();
        
        for (const auto& file_entry : fs::directory_iterator(target_dir, ec)) {
            if (!file_entry.is_regular_file(ec)) {
                continue;
            }
            
            std::string filename = file_entry.path().filename().string();
            if (filename.find(base_name) == 0) {
                auto version_info = parseVersionFile(file_entry.path());
                if (version_info) {
                    versions.push_back(*version_info);
                }
            }
        }
    }
    
    // 按时间戳排序（最新的在前）
    std::sort(versions.begin(), versions.end(), 
        [](const VersionInfo& a, const VersionInfo& b) {
            return a.timestamp > b.timestamp;
        });
    
    return versions;
}

std::optional<VersionInfo> VersionManager::getLatestVersion(const std::string& relative_path) {
    auto versions = getFileVersions(relative_path);
    if (versions.empty()) {
        return std::nullopt;
    }
    return versions[0];
}

int VersionManager::getNextVersionNumber(const std::string& relative_path) {
    auto versions = getFileVersions(relative_path);
    if (versions.empty()) {
        return 1;
    }
    return versions.size() + 1;
}

size_t VersionManager::cleanupOldVersions(const std::string& relative_path) {
    auto versions = getFileVersions(relative_path);
    size_t deleted_count = 0;
    
    auto logger = Logger::get();
    
    // 策略1: 删除超过保留天数的版本
    // 策略2: 保留最近 N 个版本
    
    for (size_t i = 0; i < versions.size(); ++i) {
        bool should_delete = false;
        
        // 总是保留最近的 max_versions_per_file 个版本
        if (i >= static_cast<size_t>(strategy_.max_versions_per_file)) {
            should_delete = true;
        }
        
        // 删除过期版本（但保留最近的几个）
        if (i >= 3 && isVersionExpired(versions[i])) {
            should_delete = true;
        }
        
        if (should_delete) {
            std::error_code ec;
            fs::remove(versions[i].file_path, ec);
            if (!ec) {
                deleted_count++;
                if (logger) {
                    logger->debug("已删除过期版本: {}", versions[i].file_path.string());
                }
            }
        }
    }
    
    return deleted_count;
}

size_t VersionManager::cleanupAllOldVersions() {
    size_t total_deleted = 0;
    auto logger = Logger::get();
    
    if (logger) {
        logger->info("开始清理过期备份版本...");
    }
    
    std::error_code ec;
    for (const auto& date_entry : fs::recursive_directory_iterator(backup_base_path_, ec)) {
        if (!date_entry.is_regular_file(ec)) {
            continue;
        }
        
        auto version_info = parseVersionFile(date_entry.path());
        if (!version_info) {
            continue;
        }
        
        if (isVersionExpired(*version_info)) {
            fs::remove(date_entry.path(), ec);
            if (!ec) {
                total_deleted++;
            }
        }
    }
    
    if (logger) {
        logger->info("清理完成，共删除 {} 个过期版本", total_deleted);
    }
    
    return total_deleted;
}

size_t VersionManager::getTotalBackupSize() {
    size_t total_size = 0;
    std::error_code ec;
    
    for (const auto& entry : fs::recursive_directory_iterator(backup_base_path_, ec)) {
        if (entry.is_regular_file(ec)) {
            total_size += fs::file_size(entry.path(), ec);
        }
    }
    
    return total_size;
}
