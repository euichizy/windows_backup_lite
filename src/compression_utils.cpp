#include "compression_utils.h"
#include <fstream>
#include <zlib.h>

std::optional<std::string> CompressionUtils::compressFile(
    const std::string& source_path,
    const std::string& dest_path,
    int compression_level) {
    
    // 读取源文件
    std::ifstream input(source_path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::vector<uint8_t> input_data(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    input.close();

    if (input_data.empty()) {
        return std::nullopt;
    }

    // 压缩数据
    uLongf compressed_size = compressBound(input_data.size());
    std::vector<uint8_t> compressed_data(compressed_size);

    int result = compress2(
        compressed_data.data(),
        &compressed_size,
        input_data.data(),
        input_data.size(),
        compression_level
    );

    if (result != Z_OK) {
        return std::nullopt;
    }

    compressed_data.resize(compressed_size);

    // 写入压缩文件
    std::ofstream output(dest_path, std::ios::binary);
    if (!output) {
        return std::nullopt;
    }

    // 写入原始大小（用于解压）
    uint64_t original_size = input_data.size();
    output.write(reinterpret_cast<const char*>(&original_size), sizeof(original_size));
    
    // 写入压缩数据
    output.write(reinterpret_cast<const char*>(compressed_data.data()), compressed_size);
    output.close();

    return dest_path;
}

bool CompressionUtils::decompressFile(
    const std::string& source_path,
    const std::string& dest_path) {
    
    std::ifstream input(source_path, std::ios::binary);
    if (!input) {
        return false;
    }

    // 读取原始大小
    uint64_t original_size;
    input.read(reinterpret_cast<char*>(&original_size), sizeof(original_size));

    // 读取压缩数据
    std::vector<uint8_t> compressed_data(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    input.close();

    // 解压数据
    std::vector<uint8_t> decompressed_data(original_size);
    uLongf decompressed_size = original_size;

    int result = uncompress(
        decompressed_data.data(),
        &decompressed_size,
        compressed_data.data(),
        compressed_data.size()
    );

    if (result != Z_OK || decompressed_size != original_size) {
        return false;
    }

    // 写入解压文件
    std::ofstream output(dest_path, std::ios::binary);
    if (!output) {
        return false;
    }

    output.write(reinterpret_cast<const char*>(decompressed_data.data()), decompressed_size);
    output.close();

    return true;
}

std::optional<std::vector<uint8_t>> CompressionUtils::compressData(
    const std::vector<uint8_t>& data,
    int compression_level) {
    
    if (data.empty()) {
        return std::nullopt;
    }

    uLongf compressed_size = compressBound(data.size());
    std::vector<uint8_t> compressed_data(compressed_size);

    int result = compress2(
        compressed_data.data(),
        &compressed_size,
        data.data(),
        data.size(),
        compression_level
    );

    if (result != Z_OK) {
        return std::nullopt;
    }

    compressed_data.resize(compressed_size);
    return compressed_data;
}

std::optional<std::vector<uint8_t>> CompressionUtils::decompressData(
    const std::vector<uint8_t>& compressed_data) {
    
    // 注意：这个函数需要知道原始大小，实际使用时需要在压缩数据前存储原始大小
    // 这里简化处理，假设最大解压大小
    uLongf decompressed_size = compressed_data.size() * 10; // 假设最多压缩10倍
    std::vector<uint8_t> decompressed_data(decompressed_size);

    int result = uncompress(
        decompressed_data.data(),
        &decompressed_size,
        compressed_data.data(),
        compressed_data.size()
    );

    if (result != Z_OK) {
        return std::nullopt;
    }

    decompressed_data.resize(decompressed_size);
    return decompressed_data;
}

size_t CompressionUtils::estimateCompressedSize(size_t original_size) {
    // 粗略估算：文本文件约 30-50% 压缩率，二进制文件约 70-90%
    // 保守估计返回 70%
    return static_cast<size_t>(original_size * 0.7);
}
