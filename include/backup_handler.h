#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <efsw/efsw.hpp>
#include "backup_strategy.h"
#include "version_manager.h"

struct FilterConfig {
    enum class Mode { None, Whitelist, Blacklist };
    Mode mode = Mode::None;
    std::vector<std::string> extensions;
};

// 备份任务结构
struct BackupTask {
    std::string source_file_path;
    std::chrono::steady_clock::time_point enqueue_time;
};

class BackupHandler : public efsw::FileWatchListener {
public:
    BackupHandler(const std::string& source_path, 
                  const std::string& dest_base_path,
                  const FilterConfig& filter_config = FilterConfig(),
                  const BackupStrategy& strategy = BackupStrategy());
    
    ~BackupHandler();

    void handleFileAction(efsw::WatchID watchid, const std::string& dir,
                         const std::string& filename, efsw::Action action,
                         std::string oldFilename) override;
    
    // 启动和停止异步备份队列
    void startAsyncBackup(int num_threads = 2);
    void stopAsyncBackup();
    
    // 获取统计信息
    size_t getTotalBackups() const { return total_backups_.load(); }
    size_t getTotalBytes() const { return total_bytes_.load(); }
    size_t getFailedBackups() const { return failed_backups_.load(); }
    size_t getSkippedBackups() const { return skipped_backups_.load(); }
    size_t getCompressedBackups() const { return compressed_backups_.load(); }
    size_t getIncrementalBackups() const { return incremental_backups_.load(); }
    
    // 清理过期版本
    size_t cleanupOldVersions();

private:
    bool isAllowed(const std::string& file_path) const;
    void backupFile(const std::string& source_file_path);
    bool isDriveAvailable(const std::string& path) const;
    bool shouldBackup(const std::string& file_path);
    
    // 异步备份队列处理
    void processBackupQueue();
    void enqueueBackup(const std::string& file_path);
    
    // 新增：智能备份决策
    bool shouldUseCompression(const std::string& file_path, size_t file_size);
    bool shouldUseIncremental(const std::string& file_path, size_t file_size);
    std::optional<std::string> getLastBackupHash(const std::string& relative_path);

    std::string source_path_;
    std::string dest_base_path_;
    FilterConfig filter_config_;
    BackupStrategy strategy_;
    std::unique_ptr<VersionManager> version_manager_;
    
    // 防抖动机制：记录每个文件的最后备份时间
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_backup_time_;
    std::mutex debounce_mutex_;
    
    // 异步备份队列
    std::queue<BackupTask> backup_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> should_stop_{false};
    
    // 统计信息
    std::atomic<size_t> total_backups_{0};
    std::atomic<size_t> total_bytes_{0};
    std::atomic<size_t> failed_backups_{0};
    std::atomic<size_t> skipped_backups_{0};
    std::atomic<size_t> compressed_backups_{0};
    std::atomic<size_t> incremental_backups_{0};
    
    // 文件哈希缓存（用于增量备份判断）
    std::unordered_map<std::string, std::string> file_hash_cache_;
    std::mutex hash_cache_mutex_;
    
    static constexpr int MAX_RETRIES = 5;
    static constexpr int RETRY_DELAY_SECONDS = 3;
    static constexpr int DEBOUNCE_SECONDS = 5; // 防抖动时间：5秒内同一文件只备份一次
};
