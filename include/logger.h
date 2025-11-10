#pragma once

#include <string>
#include <memory>
#include <spdlog/spdlog.h>

class Logger {
public:
    static void setup(const std::string& log_dir, bool use_daily_folder = false);
    static std::shared_ptr<spdlog::logger> get();

private:
    static std::shared_ptr<spdlog::logger> logger_;
};
