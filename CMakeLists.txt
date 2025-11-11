cmake_minimum_required(VERSION 3.15)
project(CodeBackup VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(efsw CONFIG REQUIRED)
find_package(ZLIB REQUIRED)

# 共享源文件
set(COMMON_SOURCES
    src/backup_handler.cpp
    src/config_loader.cpp
    src/logger.cpp
    src/hash_utils.cpp
    src/compression_utils.cpp
    src/version_manager.cpp
)

# # 控制台版本
# add_executable(codebackup
#     src/main.cpp
#     ${COMMON_SOURCES}
# )

# target_include_directories(codebackup PRIVATE include)

# target_link_libraries(codebackup PRIVATE
#     nlohmann_json::nlohmann_json
#     spdlog::spdlog
#     efsw::efsw
# )

# GUI 版本
add_executable(codebackup_gui WIN32
    src/main_gui.cpp
    src/gui_app.cpp
    ${COMMON_SOURCES}
    resources/app_icon.rc  # 添加图标资源
)

target_include_directories(codebackup_gui PRIVATE include)

target_link_libraries(codebackup_gui PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    efsw::efsw
    ZLIB::ZLIB
)

# Windows 特定设置
if(WIN32)
    # target_compile_definitions(codebackup PRIVATE UNICODE _UNICODE)
    target_compile_definitions(codebackup_gui PRIVATE UNICODE _UNICODE)
endif()
