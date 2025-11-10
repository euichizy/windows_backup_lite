#include "hash_utils.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <windows.h>
#include <wincrypt.h>

namespace fs = std::filesystem;

std::optional<std::string> HashUtils::calculateFileHash(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    // 使用 Windows Crypto API 计算 SHA-256
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    
    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return std::nullopt;
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return std::nullopt;
    }

    // 分块读取文件
    const size_t BUFFER_SIZE = 8192;
    std::vector<char> buffer(BUFFER_SIZE);
    
    while (file.read(buffer.data(), BUFFER_SIZE) || file.gcount() > 0) {
        if (!CryptHashData(hHash, reinterpret_cast<BYTE*>(buffer.data()), 
                          static_cast<DWORD>(file.gcount()), 0)) {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return std::nullopt;
        }
    }

    // 获取哈希值
    BYTE hash[32];
    DWORD hashLen = 32;
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return std::nullopt;
    }

    // 转换为十六进制字符串
    std::stringstream ss;
    for (DWORD i = 0; i < hashLen; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return ss.str();
}

std::string HashUtils::calculateDataHash(const uint8_t* data, size_t size) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    
    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    if (!CryptHashData(hHash, data, static_cast<DWORD>(size), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    BYTE hash[32];
    DWORD hashLen = 32;
    if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    std::stringstream ss;
    for (DWORD i = 0; i < hashLen; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return ss.str();
}

bool HashUtils::quickCompare(const std::string& file1, const std::string& file2) {
    std::error_code ec1, ec2;
    
    auto size1 = fs::file_size(file1, ec1);
    auto size2 = fs::file_size(file2, ec2);
    
    if (ec1 || ec2 || size1 != size2) {
        return false;
    }
    
    auto time1 = fs::last_write_time(file1, ec1);
    auto time2 = fs::last_write_time(file2, ec2);
    
    if (ec1 || ec2) {
        return false;
    }
    
    return time1 == time2;
}
