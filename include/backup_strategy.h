#pragma once

#include <string>
#include <chrono>
#include <optional>

// 备份策略配置
struct BackupStrategy {
    // 版本保留策略
    int retention_days = 30;              // 保留最近 N 天的版本
    int max_versions_per_file = 10;       // 每个文件最多保留 N 个版本
    
    // 压缩配置
    bool enable_compression = true;       // 是否启用压缩
    int compression_level = 6;            // 压缩级别 (1-9)
    size_t compression_threshold = 1024;  // 小于此大小的文件不压缩（字节）
    
    // 增量备份配置
    bool enable_incremental = true;       // 是否启用增量备份
    size_t incremental_threshold = 1048576; // 大于此大小才考虑增量（1MB）
    int full_backup_interval = 10;        // 每 N 个增量后做一次完整备份
    float delta_ratio_threshold = 0.3f;   // 差异小于 30% 才使用增量
    
    // 文件大小限制
    size_t max_file_size = 104857600;     // 最大备份文件大小（100MB）
};

// 备份元数据
struct BackupMetadata {
    std::string file_hash;                // 文件内容哈希（SHA-256）
    size_t file_size;                     // 文件大小
    std::chrono::system_clock::time_point backup_time; // 备份时间
    bool is_compressed;                   // 是否压缩
    bool is_incremental;                  // 是否增量备份
    std::string base_version;             // 增量备份的基础版本（如果是增量）
    int version_number;                   // 版本号
};
