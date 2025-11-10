#include "backup_handler.h"
#include "logger.h"
#include "hash_utils.h"
#include "compression_utils.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <algorithm>

namespace fs = std::filesystem;

BackupHandler::BackupHandler(const std::string& source_path,
                             const std::string& dest_base_path,
                             const FilterConfig& filter_config,
                             const BackupStrategy& strategy)
    : source_path_(source_path)
    , dest_base_path_(dest_base_path)
    , filter_config_(filter_config)
    , strategy_(strategy)
    , version_manager_(std::make_unique<VersionManager>(dest_base_path, strategy)) {
}

BackupHandler::~BackupHandler() {
    stopAsyncBackup();
}

bool BackupHandler::shouldUseCompression(const std::string& file_path, size_t file_size) {
    if (!strategy_.enable_compression) {
        return false;
    }
    
    // 小文件不压缩
    if (file_size < strategy_.compression_threshold) {
        return false;
    }
    
    // 已压缩的文件格式不再压缩
    fs::path path(file_path);
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    static const std::vector<std::string> compressed_exts = {
        ".zip", ".rar", ".7z", ".gz", ".bz2", ".xz",
        ".jpg", ".jpeg", ".png", ".gif", ".mp4", ".mp3",
        ".avi", ".mkv", ".pdf", ".docx", ".xlsx"
    };
    
    return std::find(compressed_exts.begin(), compressed_exts.end(), ext) == compressed_exts.end();
}

bool BackupHandler::shouldUseIncremental(const std::string& file_path, size_t file_size) {
    if (!strategy_.enable_incremental) {
        return false;
    }
    
    // 只对大文件使用增量备份
    return file_size >= strategy_.incremental_threshold;
}

std::optional<std::string> BackupHandler::getLastBackupHash(const std::string& relative_path) {
    std::lock_guard<std::mutex> lock(hash_cache_mutex_);
    
    auto it = file_hash_cache_.find(relative_path);
    if (it != file_hash_cache_.end()) {
        return it->second;
    }
    
    return std::nullopt;
}

size_t BackupHandler::cleanupOldVersions() {
    if (!version_manager_) {
        return 0;
    }
    return version_manager_->cleanupAllOldVersions();
}

void BackupHandler::handleFileAction(efsw::WatchID watchid, const std::string& dir,
                                    const std::string& filename, efsw::Action action,
                                    std::string oldFilename) {
    // 只处理修改和创建事件
    if (action != efsw::Actions::Modified && action != efsw::Actions::Add) {
        return;
    }

    std::string source_file_path = (fs::path(dir) / filename).string();
    
    // 检查是否是目录（使用 error_code 避免异常）
    std::error_code ec;
    if (fs::is_directory(source_file_path, ec) || ec) {
        return;
    }

    // 使用异步队列处理备份
    enqueueBackup(source_file_path);
}

bool BackupHandler::isAllowed(const std::string& file_path) const {
    if (filter_config_.mode == FilterConfig::Mode::None) {
        return true;
    }

    fs::path path(file_path);
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto& exts = filter_config_.extensions;
    bool found = std::find(exts.begin(), exts.end(), ext) != exts.end();

    if (filter_config_.mode == FilterConfig::Mode::Whitelist) {
        return found;
    } else if (filter_config_.mode == FilterConfig::Mode::Blacklist) {
        return !found;
    }

    return true;
}

bool BackupHandler::isDriveAvailable(const std::string& path) const {
    fs::path p(path);
    auto root = p.root_path();
    
    std::error_code ec;
    return fs::exists(root, ec) && !ec;
}

bool BackupHandler::shouldBackup(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(debounce_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto it = last_backup_time_.find(file_path);
    
    if (it != last_backup_time_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second).count();
        
        if (elapsed < DEBOUNCE_SECONDS) {
            skipped_backups_++;
            return false; // 跳过重复备份
        }
    }
    
    last_backup_time_[file_path] = now;
    return true;
}

void BackupHandler::enqueueBackup(const std::string& file_path) {
    // 防抖动检查
    if (!shouldBackup(file_path)) {
        return;
    }
    
    BackupTask task;
    task.source_file_path = file_path;
    task.enqueue_time = std::chrono::steady_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        backup_queue_.push(task);
    }
    queue_cv_.notify_one();
}

void BackupHandler::startAsyncBackup(int num_threads) {
    should_stop_ = false;
    for (int i = 0; i < num_threads; ++i) {
        worker_threads_.emplace_back([this] { processBackupQueue(); });
    }
}

void BackupHandler::stopAsyncBackup() {
    should_stop_ = true;
    queue_cv_.notify_all();
    
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

void BackupHandler::processBackupQueue() {
    while (!should_stop_) {
        BackupTask task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !backup_queue_.empty() || should_stop_; 
            });
            
            if (should_stop_ && backup_queue_.empty()) {
                break;
            }
            
            if (!backup_queue_.empty()) {
                task = backup_queue_.front();
                backup_queue_.pop();
            } else {
                continue;
            }
        }
        
        // 处理备份任务
        backupFile(task.source_file_path);
    }
}

void BackupHandler::backupFile(const std::string& source_file_path) {
    // 检查源文件是否存在（使用 error_code 避免异常）
    std::error_code ec;
    if (!fs::exists(source_file_path, ec) || ec) {
        return; // 文件可能已被删除或不可访问
    }

    if (!isAllowed(source_file_path)) {
        return;
    }
    
    // 检查文件大小限制
    size_t file_size = fs::file_size(source_file_path, ec);
    if (ec || file_size > strategy_.max_file_size) {
        auto logger = Logger::get();
        if (logger && file_size > strategy_.max_file_size) {
            logger->warn("文件 {} 超过大小限制 ({} MB)，跳过备份", 
                        source_file_path, strategy_.max_file_size / 1048576);
        }
        return;
    }

    // 检查目标驱动器是否可用
    if (!isDriveAvailable(dest_base_path_)) {
        auto logger = Logger::get();
        fs::path dest_path(dest_base_path_);
        logger->warn("目标驱动器 {} 不可用，跳过此次备份: {}", 
                    dest_path.root_path().string(), source_file_path);
        return;
    }

    auto logger = Logger::get();
    std::string log_prefix = "[" + source_path_ + "]";
    logger->info("{} 检测到变动: {} ({:.2f} KB)", log_prefix, source_file_path, file_size / 1024.0);

    try {
        fs::path source_path(source_file_path);
        fs::path relative_path = fs::relative(source_path, source_path_);
        
        // 计算文件哈希（用于去重和增量备份）
        auto current_hash = HashUtils::calculateFileHash(source_file_path);
        if (!current_hash) {
            logger->error("{} 无法计算文件哈希: {}", log_prefix, source_file_path);
            failed_backups_++;
            return;
        }
        
        // 检查是否与上次备份相同
        auto last_hash = getLastBackupHash(relative_path.string());
        if (last_hash && *last_hash == *current_hash) {
            logger->debug("{} 文件内容未变化，跳过备份: {}", log_prefix, source_file_path);
            skipped_backups_++;
            return;
        }
        
        std::string file_name = relative_path.stem().string();
        std::string file_ext = relative_path.extension().string();

        // 生成时间戳
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &time_t);
        
        char timestamp[32];
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm);
        
        // 决定是否使用压缩
        bool use_compression = shouldUseCompression(source_file_path, file_size);
        std::string versioned_filename = file_name + "." + timestamp + file_ext;
        if (use_compression) {
            versioned_filename += ".gz";
        }

        // 生成目标路径
        char today_str[32];
        std::strftime(today_str, sizeof(today_str), "%Y-%m-%d", &tm);
        
        fs::path dest_directory = fs::path(dest_base_path_) / today_str / relative_path.parent_path();
        fs::create_directories(dest_directory);
        
        fs::path dest_file_path = dest_directory / versioned_filename;

        // 指数退避重试机制
        int delay = 1;
        bool backup_success = false;
        
        for (int attempt = 0; attempt < MAX_RETRIES; ++attempt) {
            try {
                if (use_compression) {
                    // 压缩备份
                    auto result = CompressionUtils::compressFile(
                        source_file_path, 
                        dest_file_path.string(),
                        strategy_.compression_level
                    );
                    
                    if (result) {
                        compressed_backups_++;
                        backup_success = true;
                        logger->info("{} 压缩备份成功 -> {} (压缩率: {:.1f}%)", 
                                   log_prefix, dest_file_path.string(),
                                   (1.0 - fs::file_size(dest_file_path) / (double)file_size) * 100);
                    } else {
                        logger->warn("{} 压缩失败，使用普通备份", log_prefix);
                        // 降级到普通备份
                        dest_file_path = dest_directory / (file_name + "." + timestamp + file_ext);
                        fs::copy_file(source_file_path, dest_file_path, 
                                    fs::copy_options::overwrite_existing);
                        backup_success = true;
                    }
                } else {
                    // 普通备份
                    fs::copy_file(source_file_path, dest_file_path, 
                                fs::copy_options::overwrite_existing);
                    backup_success = true;
                    logger->info("{} 版本备份成功 -> {}", log_prefix, dest_file_path.string());
                }
                
                if (backup_success) {
                    // 更新统计信息
                    total_backups_++;
                    total_bytes_ += file_size;
                    
                    // 更新哈希缓存
                    {
                        std::lock_guard<std::mutex> lock(hash_cache_mutex_);
                        file_hash_cache_[relative_path.string()] = *current_hash;
                    }
                    
                    // 清理旧版本
                    if (version_manager_) {
                        size_t deleted = version_manager_->cleanupOldVersions(relative_path.string());
                        if (deleted > 0) {
                            logger->debug("{} 清理了 {} 个旧版本", log_prefix, deleted);
                        }
                    }
                    
                    return;
                }
            } catch (const fs::filesystem_error& e) {
                if (attempt < MAX_RETRIES - 1) {
                    logger->warn("{} 文件被占用，将在 {} 秒后重试... (尝试 {}/{})",
                               log_prefix, delay, attempt + 2, MAX_RETRIES);
                    std::this_thread::sleep_for(std::chrono::seconds(delay));
                    delay *= 2; // 指数增长: 1, 2, 4, 8, 16 秒
                } else {
                    failed_backups_++;
                    logger->error("{} 备份文件 {} 失败，文件持续被占用: {}",
                                log_prefix, source_file_path, e.what());
                }
            }
        }
    } catch (const std::exception& e) {
        failed_backups_++;
        logger->error("{} 处理事件 {} 时发生严重错误: {}",
                     log_prefix, source_file_path, e.what());
    }
}
