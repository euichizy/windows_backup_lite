#pragma once

#include <string>
#include <optional>

class HashUtils {
public:
    // 计算文件的 SHA-256 哈希
    static std::optional<std::string> calculateFileHash(const std::string& file_path);
    
    // 计算数据的 SHA-256 哈希
    static std::string calculateDataHash(const uint8_t* data, size_t size);
    
    // 快速检查文件是否可能相同（基于大小和修改时间）
    static bool quickCompare(const std::string& file1, const std::string& file2);
};
