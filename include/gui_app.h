#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <efsw/efsw.hpp>
#include "backup_handler.h"  // 假设这些头文件存在
#include "config_loader.h"   // 假设这些头文件存在
#include <nlohmann/json.hpp> // 假设你已正确包含 nlohmann/json

// 托盘图标消息
#define WM_TRAYICON (WM_USER + 1)

// 托盘菜单ID
#define ID_TRAY_START 1001
#define ID_TRAY_STOP 1002
#define ID_TRAY_STATUS 1003
#define ID_TRAY_FORMATS 1004
#define ID_TRAY_CONFIG 1005
#define ID_TRAY_SETTINGS 1006
#define ID_TRAY_PREVIEW 1008
#define ID_TRAY_EXIT 1007

// 设置对话框控件ID
#define IDC_LIST_SOURCES 2001
#define IDC_BTN_ADD_SOURCE 2002
#define IDC_BTN_REMOVE_SOURCE 2003
#define IDC_EDIT_DESTINATION 2004
#define IDC_BTN_BROWSE_DEST 2005
#define IDC_BTN_SAVE 2006
#define IDC_BTN_CANCEL 2007
#define IDC_BTN_CONFIG_SOURCE 2008

// 格式对话框控件ID
#define IDC_LIST_FORMATS 2010

// 源配置对话框控件ID
#define IDC_EDIT_SOURCE_PATH 2020
#define IDC_BTN_BROWSE_SOURCE 2021
#define IDC_BTN_BROWSE_FILE 2022
#define IDC_LIST_PRESETS 2023
#define IDC_BTN_ADD_PRESET 2024
#define IDC_BTN_REMOVE_PRESET 2025
#define IDC_COMBO_MODE 2026
#define IDC_LIST_EXTENSIONS 2027
#define IDC_EDIT_EXTENSION 2028
#define IDC_BTN_ADD_EXT 2029
#define IDC_BTN_REMOVE_EXT 2030
#define IDC_BTN_SOURCE_OK 2031
#define IDC_BTN_SOURCE_CANCEL 2032
#define IDC_CHECK_ENABLED 2033
// 新增：双列表模式控件
#define IDC_LIST_WHITELIST 2034
#define IDC_LIST_BLACKLIST 2035
#define IDC_EDIT_WHITELIST_EXT 2036
#define IDC_EDIT_BLACKLIST_EXT 2037
#define IDC_BTN_ADD_WHITELIST 2038
#define IDC_BTN_ADD_BLACKLIST 2039
#define IDC_BTN_REMOVE_WHITELIST 2040
#define IDC_BTN_REMOVE_BLACKLIST 2041
#define IDC_CHECK_USE_DUAL_MODE 2042
#define IDC_BTN_PREVIEW 2043

// 预览对话框控件ID
#define IDC_LIST_PREVIEW_INCLUDED 2050
#define IDC_LIST_PREVIEW_EXCLUDED 2051
#define IDC_STATIC_INCLUDED_COUNT 2052
#define IDC_STATIC_EXCLUDED_COUNT 2053
#define IDC_BTN_PREVIEW_CLOSE 2054

// 源配置预览对话框控件ID
#define IDC_LIST_SOURCE_PREVIEW_INCLUDED 2060
#define IDC_LIST_SOURCE_PREVIEW_EXCLUDED 2061
#define IDC_STATIC_SOURCE_INCLUDED_COUNT 2062
#define IDC_STATIC_SOURCE_EXCLUDED_COUNT 2063
#define IDC_BTN_SOURCE_PREVIEW_CLOSE 2064

namespace CodeBackup { // 添加命名空间

// 前向声明
struct SettingsDialogData;
struct SourceConfigData;
LRESULT CALLBACK FormatsDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SourceConfigDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

class GuiApp {
public:
    GuiApp();
    ~GuiApp();

    int run();
    
    // 辅助函数（需要被窗口过程访问）
    std::wstring browseForFolder(HWND hwndOwner, const wchar_t* title);
    void applyConfig(const Config& newConfig);
    void saveConfiguration();
    bool showSourceConfigDialog(BackupSource& source, bool isNew);

private:
    // 主窗口过程
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    
    // 托盘图标
    void createTrayIcon();
    void removeTrayIcon();
    void showContextMenu();
    
    // 对话框/窗口
    void showStatusDialog();
    void showFormatsWindow();
    void showConfigDialog();
    void showSettingsDialog();
    void showPreviewDialog();
    
    // 配置
    bool loadConfiguration();
    
    // 监控
    void startMonitoring();
    void stopMonitoring();
    void monitoringThread();
    
    void updateTrayTooltip();

    HWND hwnd_;
    NOTIFYICONDATAW nid_;
    HMENU hMenu_;
    
    std::atomic<bool> is_monitoring_;
    std::atomic<bool> should_stop_;
    std::unique_ptr<std::thread> monitor_thread_;
    
    Config config_;
    nlohmann::json presets_;
    
    std::vector<std::unique_ptr<BackupHandler>> handlers_;
    std::unique_ptr<efsw::FileWatcher> file_watcher_;
};

} // 结束命名空间