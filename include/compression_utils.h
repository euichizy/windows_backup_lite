#pragma once

#include <string>
#include <vector>
#include <optional>

class CompressionUtils {
public:
    // 压缩文件
    static std::optional<std::string> compressFile(
        const std::string& source_path,
        const std::string& dest_path,
        int compression_level = 6
    );
    
    // 解压文件
    static bool decompressFile(
        const std::string& source_path,
        const std::string& dest_path
    );
    
    // 压缩数据到内存
    static std::optional<std::vector<uint8_t>> compressData(
        const std::vector<uint8_t>& data,
        int compression_level = 6
    );
    
    // 解压数据
    static std::optional<std::vector<uint8_t>> decompressData(
        const std::vector<uint8_t>& compressed_data
    );
    
    // 估算压缩后大小（用于判断是否值得压缩）
    static size_t estimateCompressedSize(size_t original_size);
};
