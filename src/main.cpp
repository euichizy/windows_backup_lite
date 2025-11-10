#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <csignal>
#include <mutex>
#include <condition_variable>
#include <efsw/efsw.hpp>
#include "backup_handler.h"
#include "config_loader.h"
#include "logger.h"

namespace fs = std::filesystem;

// 使用条件变量替代轮询
std::mutex g_mutex;
std::condition_variable g_cv;
bool g_should_exit = false;

void signal_handler(int signal) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_should_exit = true;
    g_cv.notify_one();
}

int main() {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 加载配置
    auto config_opt = ConfigLoader::loadConfig("config.json");
    if (!config_opt) {
        std::cerr << "无法加载配置文件，程序退出。" << std::endl;
        return 1;
    }
    auto config = *config_opt;

    // 加载预设
    auto presets = ConfigLoader::loadPresets("presets.json");
    if (!presets) {
        presets = nlohmann::json::object();
    }

    // 设置日志
    Logger::setup(config.backup_destination_base, true);
    auto logger = Logger::get();

    // 创建文件监控器
    efsw::FileWatcher file_watcher;
    std::vector<std::unique_ptr<BackupHandler>> handlers;
    std::vector<efsw::WatchID> watch_ids;

    // 为每个启用的源路径创建监控
    for (const auto& source : config.backup_sources) {
        if (!source.enabled) {
            continue;
        }

        if (!fs::exists(source.path)) {
            logger->warn("配置的源路径不存在，已跳过: {}", source.path);
            continue;
        }

        // 确定过滤配置
        FilterConfig filter_config;
        if (source.custom_filter) {
            filter_config = *source.custom_filter;
            logger->info("为 {} 应用了自定义过滤器。", source.path);
        } else if (!source.presets.empty()) {
            filter_config = ConfigLoader::mergePresets(source.presets, *presets);
            logger->info("为 {} 应用了合并预设。", source.path);
        }

        // 创建处理器并添加监控
        auto handler = std::make_unique<BackupHandler>(
            source.path, 
            config.backup_destination_base,
            filter_config,
            config.strategy  // 传递策略配置
        );
        
        // 启动异步备份队列（每个处理器使用 2 个工作线程）
        handler->startAsyncBackup(2);

        efsw::WatchID watch_id = file_watcher.addWatch(
            source.path, 
            handler.get(), 
            true  // recursive
        );

        if (watch_id > 0) {
            handlers.push_back(std::move(handler));
            watch_ids.push_back(watch_id);
            logger->info("正在监控 -> {}", source.path);
        } else {
            logger->error("无法监控路径: {}", source.path);
        }
    }

    if (handlers.empty()) {
        logger->error("没有有效的、已启用的源路径可供监控。脚本退出。");
        return 1;
    }

    // 启动监控
    file_watcher.watch();
    
    logger->info("--- 监控服务已启动 (V3.0 - 智能备份版) ---");
    logger->info("所有备份将存至 -> {}", config.backup_destination_base);
    logger->info("备份策略: 保留{}天 | 最多{}版本 | 压缩:{} | 增量:{}", 
                config.strategy.retention_days,
                config.strategy.max_versions_per_file,
                config.strategy.enable_compression ? "启用" : "禁用",
                config.strategy.enable_incremental ? "启用" : "禁用");
    logger->info("性能优化: 异步队列 + 防抖动 + 指数退避 + 智能压缩");
    std::cout << "监控已在后台运行，详细信息请查看日志文件。按 Ctrl+C 停止。" << std::endl;

    // 使用条件变量等待退出信号（替代轮询）
    {
        std::unique_lock<std::mutex> lock(g_mutex);
        g_cv.wait(lock, [] { return g_should_exit; });
    }

    logger->info("--- 用户手动停止监控服务 ---");
    
    // 打印统计信息
    logger->info("=== 备份统计信息 ===");
    size_t total_backups = 0;
    size_t total_bytes = 0;
    size_t failed_backups = 0;
    size_t skipped_backups = 0;
    
    for (const auto& handler : handlers) {
        total_backups += handler->getTotalBackups();
        total_bytes += handler->getTotalBytes();
        failed_backups += handler->getFailedBackups();
        skipped_backups += handler->getSkippedBackups();
    }
    
    size_t compressed_backups = 0;
    size_t incremental_backups = 0;
    
    for (const auto& handler : handlers) {
        compressed_backups += handler->getCompressedBackups();
        incremental_backups += handler->getIncrementalBackups();
    }
    
    logger->info("成功备份: {} 个文件", total_backups);
    logger->info("备份大小: {:.2f} MB", total_bytes / 1024.0 / 1024.0);
    logger->info("压缩备份: {} 个文件", compressed_backups);
    logger->info("增量备份: {} 个文件", incremental_backups);
    logger->info("失败备份: {} 个文件", failed_backups);
    logger->info("跳过备份: {} 个文件 (防抖动/去重)", skipped_backups);
    logger->info("--- 监控服务已安全关闭 ---");

    return 0;
}
