#include "logger.h"
#include <filesystem>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace fs = std::filesystem;

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

void Logger::setup(const std::string& log_dir, bool use_daily_folder) {
    std::string actual_log_dir = log_dir;
    
    if (use_daily_folder) {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &time_t);
        
        char date_buffer[32];
        std::strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", &tm);
        actual_log_dir = (fs::path(log_dir) / date_buffer).string();
    }

    fs::create_directories(actual_log_dir);

    // 生成日志文件名
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time_t);
    
    char filename_buffer[64];
    std::strftime(filename_buffer, sizeof(filename_buffer), "backup_log_%Y-%m-%d.log", &tm);
    std::string log_filepath = (fs::path(actual_log_dir) / filename_buffer).string();

    // 创建 sinks
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_filepath, true);

    // 设置格式
    console_sink->set_pattern("%Y-%m-%d %H:%M:%S - %^%l%$ - %v");
    file_sink->set_pattern("%Y-%m-%d %H:%M:%S - %l - %v");

    // 创建 logger
    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    logger_ = std::make_shared<spdlog::logger>("MultiSourceBackupLogger", sinks.begin(), sinks.end());
    logger_->set_level(spdlog::level::info);
    logger_->flush_on(spdlog::level::info);

    spdlog::register_logger(logger_);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    return logger_;
}
