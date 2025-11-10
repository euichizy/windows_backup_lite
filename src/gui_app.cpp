#include "gui_app.h"
#include "logger.h" // 假设这些头文件存在
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <filesystem>
#include <sstream>
#include <fstream>

namespace fs = std::filesystem;

namespace CodeBackup { // 添加命名空间

// UTF-8 到 UTF-16 转换辅助函数
inline std::wstring Utf8ToWide(const std::string& utf8str) {
    if (utf8str.empty()) return std::wstring();
    
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8str.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        // 转换失败，尝试使用简单转换
        return std::wstring(utf8str.begin(), utf8str.end());
    }
    
    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8str.c_str(), -1, &wstr[0], wlen);
    // 移除末尾的 null 字符
    if (!wstr.empty() && wstr.back() == 0) {
        wstr.pop_back();
    }
    return wstr;
}

// UTF-16 到 UTF-8 转换辅助函数
inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        // 转换失败，尝试使用简单转换
        return std::string(wstr.begin(), wstr.end());
    }
    
    std::string str(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], len, nullptr, nullptr);
    // 移除末尾的 null 字符
    if (!str.empty() && str.back() == 0) {
        str.pop_back();
    }
    return str;
}

// 设置对话框数据结构
struct SettingsDialogData {
    GuiApp* app;
    Config tempConfig;
    HWND hEditDest;
    HWND hListSources;
    HWND hwndDlg;
    HFONT hFont;
};

// 源配置对话框数据结构
struct SourceConfigData {
    GuiApp* app;
    BackupSource* source;
    nlohmann::json* presets;
    HWND hwndDlg;
    HWND hListPresets;
    HWND hListExtensions;
    HWND hComboMode;
    HFONT hFont;
    bool isNewSource;
};

// 格式窗口过程
LRESULT CALLBACK FormatsDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_COMMAND: {
            WORD ctrlId = LOWORD(wParam);
            WORD notifyCode = HIWORD(wParam);
            
            if (notifyCode != 0) return 0;
            
            if (ctrlId == IDOK) {
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
            break;
        }
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 源配置对话框窗口过程
LRESULT CALLBACK SourceConfigDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    SourceConfigData* data = (SourceConfigData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {
        case WM_COMMAND: {
            WORD ctrlId = LOWORD(wParam);
            WORD notifyCode = HIWORD(wParam);
            
            if (notifyCode != 0 && notifyCode != CBN_SELCHANGE) return 0;
            if (!data) break;

            switch (ctrlId) {
                case IDC_BTN_BROWSE_SOURCE: {
                    std::wstring folder = data->app->browseForFolder(hwnd, L"选择要备份的文件夹");
                    if (!folder.empty()) {
                        SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_SOURCE_PATH), folder.c_str());
                    }
                    return 0;
                }

                case IDC_BTN_BROWSE_FILE: {
                    OPENFILENAMEW ofn = {};
                    wchar_t fileName[MAX_PATH] = L"";
                    
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFile = fileName;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.lpstrFilter = L"所有文件\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrTitle = L"选择要备份的文件";
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    
                    if (GetOpenFileNameW(&ofn)) {
                        SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_SOURCE_PATH), fileName);
                    }
                    return 0;
                }

                case IDC_BTN_ADD_PRESET: {
                    int sel = (int)SendMessageW(data->hListPresets, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) {
                        wchar_t preset[256];
                        SendMessageW(data->hListPresets, LB_GETTEXT, sel, (LPARAM)preset);
                        
                        std::wstring presetStr(preset);
                        std::string presetName = WideToUtf8(presetStr);
                        
                        // 检查是否已存在
                        bool exists = false;
                        for (const auto& p : data->source->presets) {
                            if (p == presetName) {
                                exists = true;
                                break;
                            }
                        }
                        
                        if (!exists) {
                            data->source->presets.push_back(presetName);
                            MessageBoxW(hwnd, (L"已添加预设: " + presetStr).c_str(), L"成功", MB_ICONINFORMATION);
                        }
                    }
                    return 0;
                }

                case IDC_BTN_ADD_EXT: {
                    wchar_t ext[256];
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_EXTENSION), ext, 256);
                    
                    if (wcslen(ext) > 0) {
                        std::wstring extStr(ext);
                        if (extStr[0] != L'.') extStr = L"." + extStr;
                        
                        SendMessageW(data->hListExtensions, LB_ADDSTRING, 0, (LPARAM)extStr.c_str());
                        SetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_EXTENSION), L"");
                    }
                    return 0;
                }

                case IDC_BTN_REMOVE_EXT: {
                    int sel = (int)SendMessageW(data->hListExtensions, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) {
                        SendMessageW(data->hListExtensions, LB_DELETESTRING, sel, 0);
                    }
                    return 0;
                }

                case IDC_BTN_SOURCE_OK: {
                    wchar_t path[MAX_PATH];
                    GetWindowTextW(GetDlgItem(hwnd, IDC_EDIT_SOURCE_PATH), path, MAX_PATH);
                    
                    if (wcslen(path) == 0) {
                        MessageBoxW(hwnd, L"请选择路径或文件", L"错误", MB_ICONERROR);
                        return 0;
                    }
                    
                    std::wstring pathWstr(path);
                    data->source->path = WideToUtf8(pathWstr);
                    data->source->enabled = (SendMessageW(GetDlgItem(hwnd, IDC_CHECK_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    
                    // 获取模式
                    int modeIdx = (int)SendMessageW(data->hComboMode, CB_GETCURSEL, 0, 0);
                    if (modeIdx != CB_ERR && data->source->custom_filter) {
                        if (modeIdx == 0) data->source->custom_filter->mode = FilterConfig::Mode::Whitelist;
                        else if (modeIdx == 1) data->source->custom_filter->mode = FilterConfig::Mode::Blacklist;
                        else data->source->custom_filter->mode = FilterConfig::Mode::None;
                        
                        // 获取扩展名列表
                        data->source->custom_filter->extensions.clear();
                        int count = (int)SendMessageW(data->hListExtensions, LB_GETCOUNT, 0, 0);
                        for (int i = 0; i < count; i++) {
                            wchar_t ext[256];
                            SendMessageW(data->hListExtensions, LB_GETTEXT, i, (LPARAM)ext);
                            std::wstring extWstr(ext);
                            data->source->custom_filter->extensions.push_back(WideToUtf8(extWstr));
                        }
                    }
                    
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                }

                case IDC_BTN_SOURCE_CANCEL:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
        }
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// 设置对话框窗口过程
LRESULT CALLBACK SettingsDialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    SettingsDialogData* data = (SettingsDialogData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    
    switch (uMsg) {
        case WM_COMMAND: {
            WORD ctrlId = LOWORD(wParam);
            WORD notifyCode = HIWORD(wParam);
            
            // 只处理按钮点击和列表框双击
            if (notifyCode != 0 && notifyCode != LBN_DBLCLK) {
                return 0;
            }
            
            if (!data) break;

            switch (ctrlId) {
                case IDC_BTN_BROWSE_DEST: {
                    std::wstring folder = data->app->browseForFolder(hwnd, L"选择备份目标位置");
                    if (!folder.empty()) {
                        SetWindowTextW(data->hEditDest, folder.c_str());
                    }
                    return 0;
                }

                case IDC_BTN_ADD_SOURCE: {
                    BackupSource new_source;
                    new_source.enabled = true;
                    new_source.presets = {"default"};
                    
                    if (data->app->showSourceConfigDialog(new_source, true)) {
                        // 检查是否已存在
                        bool exists = false;
                        for (const auto& source : data->tempConfig.backup_sources) {
                            if (source.path == new_source.path) {
                                exists = true;
                                break;
                            }
                        }

                        if (!exists) {
                            data->tempConfig.backup_sources.push_back(new_source);
                            
                            std::wstring pathWstr = Utf8ToWide(new_source.path);
                            std::wstring display = (new_source.enabled ? L"✓ " : L"✗ ") + pathWstr;
                            SendMessageW(data->hListSources, LB_ADDSTRING, 0, (LPARAM)display.c_str());
                        } else {
                            MessageBoxW(hwnd, L"该路径已在备份列表中", L"提示", MB_ICONINFORMATION);
                        }
                    }
                    return 0;
                }

                case IDC_BTN_CONFIG_SOURCE: {
                    int sel = (int)SendMessageW(data->hListSources, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR && sel >= 0 && sel < (int)data->tempConfig.backup_sources.size()) {
                        if (data->app->showSourceConfigDialog(data->tempConfig.backup_sources[sel], false)) {
                            // 更新列表显示
                            auto& source = data->tempConfig.backup_sources[sel];
                            std::wstring pathWstr = Utf8ToWide(source.path);
                            std::wstring display = (source.enabled ? L"✓ " : L"✗ ") + pathWstr;
                            
                            SendMessageW(data->hListSources, LB_DELETESTRING, sel, 0);
                            SendMessageW(data->hListSources, LB_INSERTSTRING, sel, (LPARAM)display.c_str());
                            SendMessageW(data->hListSources, LB_SETCURSEL, sel, 0);
                        }
                    } else {
                        MessageBoxW(hwnd, L"请先选择要配置的项", L"提示", MB_ICONINFORMATION);
                    }
                    return 0;
                }

                case IDC_BTN_REMOVE_SOURCE: {
                    int sel = (int)SendMessageW(data->hListSources, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR && sel >= 0 && sel < (int)data->tempConfig.backup_sources.size()) {
                        data->tempConfig.backup_sources.erase(data->tempConfig.backup_sources.begin() + sel);
                        SendMessageW(data->hListSources, LB_DELETESTRING, sel, 0);
                    } else {
                        MessageBoxW(hwnd, L"请先选择要删除的项", L"提示", MB_ICONINFORMATION);
                    }
                    return 0;
                }

                case IDC_LIST_SOURCES: {
                    if (notifyCode == LBN_DBLCLK) {
                        int sel = (int)SendMessageW(data->hListSources, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR && sel >= 0 && sel < (int)data->tempConfig.backup_sources.size()) {
                            data->tempConfig.backup_sources[sel].enabled = !data->tempConfig.backup_sources[sel].enabled;

                            std::wstring path(data->tempConfig.backup_sources[sel].path.begin(),
                                            data->tempConfig.backup_sources[sel].path.end());
                            std::wstring display = (data->tempConfig.backup_sources[sel].enabled ? L"✓ " : L"✗ ") + path;

                            SendMessageW(data->hListSources, LB_DELETESTRING, sel, 0);
                            SendMessageW(data->hListSources, LB_INSERTSTRING, sel, (LPARAM)display.c_str());
                            SendMessageW(data->hListSources, LB_SETCURSEL, sel, 0);
                        }
                    }
                    return 0;
                }

                case IDC_BTN_SAVE: {
                    wchar_t dest_path[MAX_PATH];
                    GetWindowTextW(data->hEditDest, dest_path, MAX_PATH);

                    if (wcslen(dest_path) == 0) {
                        MessageBoxW(hwnd, L"请设置备份目标位置", L"错误", MB_ICONERROR);
                        return 0;
                    }

                    std::wstring dest_wstr(dest_path);
                    data->tempConfig.backup_destination_base = WideToUtf8(dest_wstr);
                    
                    data->app->applyConfig(data->tempConfig);
                    data->app->saveConfiguration();
                    
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
                }

                case IDC_BTN_CANCEL:
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
        }
            
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

GuiApp::GuiApp() 
    : hwnd_(nullptr), hMenu_(nullptr), 
      is_monitoring_(false), should_stop_(false) {
    ZeroMemory(&nid_, sizeof(nid_));
    // 初始化COM库，用于 SHBrowseForFolder
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
}

GuiApp::~GuiApp() {
    stopMonitoring();
    removeTrayIcon();
    // 释放COM库
    CoUninitialize();
}

LRESULT CALLBACK GuiApp::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    GuiApp* app = nullptr;
    
    if (uMsg == WM_CREATE) {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        app = reinterpret_cast<GuiApp*>(pCreate->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    } else {
        app = reinterpret_cast<GuiApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (!app) {
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) {
                app->showContextMenu();
            }
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_START:
                    app->startMonitoring();
                    break;
                case ID_TRAY_STOP:
                    app->stopMonitoring();
                    break;
                case ID_TRAY_STATUS:
                    app->showStatusDialog();
                    break;
                case ID_TRAY_FORMATS:
                    app->showFormatsWindow();
                    break;
                case ID_TRAY_SETTINGS:
                    app->showSettingsDialog();
                    break;
                case ID_TRAY_CONFIG:
                    app->showConfigDialog();
                    break;
                case ID_TRAY_EXIT:
                    PostQuitMessage(0);
                    break;
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

int GuiApp::run() {
    // 注册窗口类
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CodeBackupTrayApp";
    
    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"窗口类注册失败", L"错误", MB_ICONERROR);
        return 1;
    }

    // 创建隐藏窗口
    hwnd_ = CreateWindowExW(
        0, L"CodeBackupTrayApp", L"CodeBackup",
        0, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), this
    );

    if (!hwnd_) {
        MessageBoxW(nullptr, L"窗口创建失败", L"错误", MB_ICONERROR);
        return 1;
    }

    // 加载配置
    if (!loadConfiguration()) {
        MessageBoxW(hwnd_, L"配置文件加载失败，请检查 config.json", L"错误", MB_ICONERROR);
        // 即使加载失败，也可能需要打开设置
        // return 1; 
    }

    // 创建托盘图标
    createTrayIcon();

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}

void GuiApp::createTrayIcon() {
    nid_.cbSize = sizeof(NOTIFYICONDATAW);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    // 使用标准图标，或加载自定义图标
    // nid_.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_YOUR_ICON));
    nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION); // 暂用系统图标
    wcscpy_s(nid_.szTip, L"CodeBackup - 已停止");

    Shell_NotifyIconW(NIM_ADD, &nid_);
}

void GuiApp::removeTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
}

void GuiApp::showContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    if (!hMenu_) {
        hMenu_ = CreatePopupMenu();
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_START, L"启动监控");
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_STOP, L"停止监控");
        AppendMenuW(hMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_STATUS, L"查看状态");
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_FORMATS, L"备份格式");
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_SETTINGS, L"备份设置");
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_CONFIG, L"编辑配置文件");
        AppendMenuW(hMenu_, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hMenu_, MF_STRING, ID_TRAY_EXIT, L"退出");
    }

    // 更新菜单状态
    EnableMenuItem(hMenu_, ID_TRAY_START, is_monitoring_ ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(hMenu_, ID_TRAY_STOP, is_monitoring_ ? MF_ENABLED : MF_GRAYED);

    SetForegroundWindow(hwnd_);
    TrackPopupMenu(hMenu_, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
}

void GuiApp::showStatusDialog() {
    std::wstringstream ss;
    ss << L"CodeBackup 监控状态\n\n";
    ss << L"状态: " << (is_monitoring_ ? L"运行中" : L"已停止") << L"\n";
    ss << L"备份目标: " << std::wstring(config_.backup_destination_base.begin(), 
                                       config_.backup_destination_base.end()) << L"\n\n";
    ss << L"监控路径:\n";
    
    for (const auto& source : config_.backup_sources) {
        if (source.enabled) {
            std::wstring path = Utf8ToWide(source.path);
            ss << L"  • " << path << L"\n";
        }
    }

    MessageBoxW(hwnd_, ss.str().c_str(), L"状态信息", MB_ICONINFORMATION);
}

void GuiApp::showFormatsWindow() {
    // 注册对话框窗口类
    static const wchar_t* className = L"FormatsDialogClass";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = FormatsDialogProc; // 使用专门的窗口过程
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = className;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    // 创建对话框窗口
    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        className,
        L"备份文件格式",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 500,
        hwnd_,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!hwndDlg) {
        MessageBoxW(hwnd_, L"无法创建窗口", L"错误", MB_ICONERROR);
        return;
    }

    // 居中窗口
    RECT rect;
    GetWindowRect(hwndDlg, &rect);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2;
    SetWindowPos(hwndDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // 创建列表框
    HWND hList = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"LISTBOX",
        nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOINTEGRALHEIGHT,
        10, 10, 665, 410,
        hwndDlg,
        (HMENU)IDC_LIST_FORMATS, // 使用定义好的ID
        GetModuleHandle(nullptr),
        nullptr
    );

    // 设置字体
    HFONT hFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
    SendMessageW(hList, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 填充数据
    if (presets_.is_object()) {
        for (auto it = presets_.begin(); it != presets_.end(); ++it) {
            std::wstring preset_name = Utf8ToWide(it.key());
            std::wstring header = L"【" + preset_name + L"】";
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)header.c_str());

            auto& preset = it.value();
            if (preset.contains("mode")) {
                std::string mode = preset["mode"];
                std::wstring mode_str = L"  模式: ";
                if (mode == "whitelist") {
                    mode_str += L"白名单（仅备份以下格式）";
                } else if (mode == "blacklist") {
                    mode_str += L"黑名单（排除以下格式）";
                } else {
                    mode_str += L"无";
                }
                SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)mode_str.c_str());
            }

            if (preset.contains("extensions") && preset["extensions"].is_array()) {
                std::wstring exts = L"  格式: ";
                auto extensions = preset["extensions"];
                int count = 0;
                for (const auto& ext : extensions) {
                    if (count > 0) exts += L", ";
                    std::string ext_str = ext.get<std::string>();
                    exts += Utf8ToWide(ext_str);
                    count++;

                    if (count % 10 == 0 && count < (int)extensions.size()) {
                        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)exts.c_str());
                        exts = L"        "; // 缩进
                    }
                }
                if (exts != L"  格式: " && exts != L"        ") {
                    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)exts.c_str());
                }
            }
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L""); // 空行
        }
    }

    // 显示当前启用的源路径配置
    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"【当前监控路径配置】");
    SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"");

    for (const auto& source : config_.backup_sources) {
        if (!source.enabled) continue;

        std::wstring path = Utf8ToWide(source.path);
        std::wstring line = L"路径: " + path;
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)line.c_str());

        if (!source.presets.empty()) {
            std::wstring presets_line = L"  应用预设: ";
            for (size_t i = 0; i < source.presets.size(); ++i) {
                if (i > 0) presets_line += L", ";
                std::wstring preset = Utf8ToWide(source.presets[i]);
                presets_line += preset;
            }
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)presets_line.c_str());
        }

        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)L"");
    }

    // 创建关闭按钮
    HWND hBtnClose = CreateWindowW(
        L"BUTTON",
        L"关闭",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        300, 430, 100, 30,
        hwndDlg,
        (HMENU)IDOK,
        GetModuleHandle(nullptr),
        nullptr
    );
    SendMessageW(hBtnClose, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 设置窗口数据，用于在窗口过程中访问
    SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)this);

    // 消息循环 - 暂时不使用 IsDialogMessageW 进行调试
    EnableWindow(hwnd_, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT || !IsWindow(hwndDlg)) {
            break;
        }
        
        // 调试：直接分发消息，不使用 IsDialogMessageW
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hwnd_, TRUE);
    SetForegroundWindow(hwnd_);
    if (IsWindow(hwndDlg)) {
        DestroyWindow(hwndDlg);
    }
    DeleteObject(hFont);
}



void GuiApp::showConfigDialog() {
    // 尝试用记事本打开 config.json
    ShellExecuteW(nullptr, L"open", L"notepad.exe", L"config.json", nullptr, SW_SHOW);
}

void GuiApp::showSettingsDialog() {
    // 注册对话框窗口类
    static const wchar_t* className = L"SettingsDialogClass";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SettingsDialogProc; // 使用专门的窗口过程
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = className;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    // 先创建对话框数据结构
    SettingsDialogData* dialogData = new SettingsDialogData();
    dialogData->app = this;
    dialogData->tempConfig = config_;
    
    // 创建对话框窗口
    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        className,
        L"备份设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 700, 550,
        hwnd_,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!hwndDlg) {
        MessageBoxW(hwnd_, L"无法创建窗口", L"错误", MB_ICONERROR);
        delete dialogData;
        return;
    }
    
    // 立即设置窗口数据，在创建任何控件之前
    dialogData->hwndDlg = hwndDlg;
    SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)dialogData);

    // 居中窗口
    RECT rect;
    GetWindowRect(hwndDlg, &rect);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2;
    SetWindowPos(hwndDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // 创建字体
    HFONT hFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );

    // 备份目标路径标签
    CreateWindowW(
        L"STATIC", L"备份目标位置:",
        WS_CHILD | WS_VISIBLE,
        20, 20, 150, 25,
        hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr
    );

    // 备份目标路径输入框
    HWND hEditDest = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
        20, 50, 550, 30,
        hwndDlg, (HMENU)IDC_EDIT_DESTINATION, GetModuleHandle(nullptr), nullptr
    );
    SendMessageW(hEditDest, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 设置当前备份目标路径
    std::wstring dest(config_.backup_destination_base.begin(),
                     config_.backup_destination_base.end());
    SetWindowTextW(hEditDest, dest.c_str());

    // 浏览按钮
    CreateWindowW(
        L"BUTTON", L"浏览...",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        580, 50, 90, 30,
        hwndDlg, (HMENU)IDC_BTN_BROWSE_DEST, GetModuleHandle(nullptr), nullptr
    );

    // 备份源列表标签
    CreateWindowW(
        L"STATIC", L"备份源文件夹 (双击切换启用/禁用):",
        WS_CHILD | WS_VISIBLE,
        20, 100, 350, 25,
        hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr
    );

    // 备份源列表框
    HWND hListSources = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        20, 130, 550, 300,
        hwndDlg, (HMENU)IDC_LIST_SOURCES, GetModuleHandle(nullptr), nullptr
    );
    SendMessageW(hListSources, WM_SETFONT, (WPARAM)hFont, TRUE);

    // 填充备份源列表
    for (const auto& source : config_.backup_sources) {
        std::wstring path = Utf8ToWide(source.path);
        std::wstring display = (source.enabled ? L"✓ " : L"✗ ") + path;
        SendMessageW(hListSources, LB_ADDSTRING, 0, (LPARAM)display.c_str());
    }

    // 添加按钮
    CreateWindowW(
        L"BUTTON", L"添加",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        580, 130, 90, 30,
        hwndDlg, (HMENU)IDC_BTN_ADD_SOURCE, GetModuleHandle(nullptr), nullptr
    );

    // 配置按钮
    CreateWindowW(
        L"BUTTON", L"配置",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        580, 165, 90, 30,
        hwndDlg, (HMENU)IDC_BTN_CONFIG_SOURCE, GetModuleHandle(nullptr), nullptr
    );

    // 删除按钮
    CreateWindowW(
        L"BUTTON", L"删除",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        580, 200, 90, 30,
        hwndDlg, (HMENU)IDC_BTN_REMOVE_SOURCE, GetModuleHandle(nullptr), nullptr
    );

    // 保存按钮
    CreateWindowW(
        L"BUTTON", L"保存",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        480, 460, 90, 35,
        hwndDlg, (HMENU)IDC_BTN_SAVE, GetModuleHandle(nullptr), nullptr
    );

    // 取消按钮
    CreateWindowW(
        L"BUTTON", L"取消",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        580, 460, 90, 35,
        hwndDlg, (HMENU)IDC_BTN_CANCEL, GetModuleHandle(nullptr), nullptr
    );
    
    // 给所有控件设置字体
    EnumChildWindows(hwndDlg, [](HWND hwnd, LPARAM lParam) -> BOOL {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
    }, (LPARAM)hFont);

    // 更新对话框数据中的控件句柄
    dialogData->hEditDest = hEditDest;
    dialogData->hListSources = hListSources;
    dialogData->hFont = hFont;

    // 消息循环 - 暂时不使用 IsDialogMessageW 进行调试
    EnableWindow(hwnd_, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT || !IsWindow(hwndDlg)) {
            break;
        }
        
        // 调试：直接分发消息，不使用 IsDialogMessageW
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hwnd_, TRUE);
    SetForegroundWindow(hwnd_);
    if (IsWindow(hwndDlg)) {
        DestroyWindow(hwndDlg);
    }
    delete dialogData;
    DeleteObject(hFont);
}

std::wstring GuiApp::browseForFolder(HWND hwndOwner, const wchar_t* title) {
    BROWSEINFOW bi = {};
    bi.hwndOwner = hwndOwner;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl != nullptr) {
        wchar_t path[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, path)) {
            CoTaskMemFree(pidl);
            return std::wstring(path);
        }
        CoTaskMemFree(pidl);
    }
    return L"";
}

void GuiApp::applyConfig(const Config& newConfig) {
    config_ = newConfig;
}

void GuiApp::saveConfiguration() {
    try {
        nlohmann::json config_json;
        
        // 保存备份目标路径
        config_json["backup_destination_base"] = config_.backup_destination_base;
        
        // 保存备份源
        config_json["backup_sources"] = nlohmann::json::array();
        for (const auto& source : config_.backup_sources) {
            nlohmann::json source_json;
            source_json["path"] = source.path;
            source_json["enabled"] = source.enabled;
            source_json["presets"] = source.presets;
            
            // 保存自定义过滤器配置
            if (source.custom_filter && source.custom_filter->mode != FilterConfig::Mode::None) {
                nlohmann::json filter_json;
                
                // 保存模式
                if (source.custom_filter->mode == FilterConfig::Mode::Whitelist) {
                    filter_json["mode"] = "whitelist";
                } else if (source.custom_filter->mode == FilterConfig::Mode::Blacklist) {
                    filter_json["mode"] = "blacklist";
                }
                
                // 保存扩展名列表
                if (!source.custom_filter->extensions.empty()) {
                    filter_json["extensions"] = source.custom_filter->extensions;
                }
                
                source_json["filter"] = filter_json;
            }
            
            config_json["backup_sources"].push_back(source_json);
        }
        
        // 保存策略配置
        config_json["strategy"]["retention_days"] = config_.strategy.retention_days;
        config_json["strategy"]["max_versions_per_file"] = config_.strategy.max_versions_per_file;
        config_json["strategy"]["enable_compression"] = config_.strategy.enable_compression;
        config_json["strategy"]["compression_level"] = config_.strategy.compression_level;
        config_json["strategy"]["compression_threshold"] = config_.strategy.compression_threshold;
        config_json["strategy"]["enable_incremental"] = config_.strategy.enable_incremental;
        config_json["strategy"]["incremental_threshold"] = config_.strategy.incremental_threshold;
        config_json["strategy"]["full_backup_interval"] = config_.strategy.full_backup_interval;
        config_json["strategy"]["delta_ratio_threshold"] = config_.strategy.delta_ratio_threshold;
        config_json["strategy"]["max_file_size"] = config_.strategy.max_file_size;
        
        // 写入文件
        std::ofstream file("config.json");
        file << config_json.dump(4);
        file.close();
        
        MessageBoxW(hwnd_, L"配置已保存", L"成功", MB_ICONINFORMATION);
    } catch (const std::exception& e) {
        std::string error = "保存配置失败: " + std::string(e.what());
        std::wstring werror = Utf8ToWide(error);
        MessageBoxW(hwnd_, werror.c_str(), L"错误", MB_ICONERROR);
    }
}



void GuiApp::startMonitoring() {
    if (is_monitoring_) {
        return;
    }

    // 重新加载配置以防万一
    if (!loadConfiguration()) {
        MessageBoxW(hwnd_, L"无法加载配置，启动失败", L"错误", MB_ICONERROR);
        return;
    }

    should_stop_ = false;
    monitor_thread_ = std::make_unique<std::thread>(&GuiApp::monitoringThread, this);
    
    is_monitoring_ = true;
    updateTrayTooltip();
    
    MessageBoxW(hwnd_, L"监控服务已启动", L"CodeBackup", MB_ICONINFORMATION);
}

void GuiApp::stopMonitoring() {
    if (!is_monitoring_) {
        return;
    }

    should_stop_ = true;
    
    // 唤醒监控线程（如果它正在休眠）
    // (efsw::FileWatcher::watch() 是阻塞的，但它应该会响应)
    // 如果 efsw 没有正确停止，可能需要更复杂的中断机制
    
    if (monitor_thread_ && monitor_thread_->joinable()) {
        monitor_thread_->join();
    }
    
    handlers_.clear();
    file_watcher_.reset();
    
    is_monitoring_ = false;
    updateTrayTooltip();
    
    MessageBoxW(hwnd_, L"监控服务已停止", L"CodeBackup", MB_ICONINFORMATION);
}

void GuiApp::monitoringThread() {
    // 设置日志
    Logger::setup(config_.backup_destination_base, true);
    auto logger = Logger::get();

    try {
        // 创建文件监控器
        file_watcher_ = std::make_unique<efsw::FileWatcher>();
        handlers_.clear();

        // 为每个启用的源路径创建监控
        for (const auto& source : config_.backup_sources) {
            if (!source.enabled || !fs::exists(source.path)) {
                logger->warn("跳过无效或禁用的路径: {}", source.path);
                continue;
            }

            FilterConfig filter_config;
            if (source.custom_filter) {
                filter_config = *source.custom_filter;
            } else if (!source.presets.empty()) {
                filter_config = ConfigLoader::mergePresets(source.presets, presets_);
            }

            auto handler = std::make_unique<BackupHandler>(
                source.path, 
                config_.backup_destination_base,
                filter_config,
                config_.strategy
            );
            
            // 启动异步备份队列
            handler->startAsyncBackup(2); // 假设 2 个工作线程

            efsw::WatchID watch_id = file_watcher_->addWatch(
                source.path, 
                handler.get(), 
                true // 递归
            );

            if (watch_id > 0) {
                handlers_.push_back(std::move(handler));
                logger->info("正在监控 -> {}", source.path);
            } else {
                logger->error("无法监控 -> {}", source.path);
            }
        }

        if (handlers_.empty()) {
            logger->error("没有有效的源路径可供监控");
            is_monitoring_ = false; // 自动停止
            updateTrayTooltip(); // 更新UI
            return;
        }

        file_watcher_->watch(); // 开始阻塞监控
        logger->info("--- 监控服务已启动 (GUI 版本 V3.0) ---");
        logger->info("备份策略: 保留{}天 | 最多{}版本 | 压缩:{} | 增量:{}", 
                    config_.strategy.retention_days,
                    config_.strategy.max_versions_per_file,
                    config_.strategy.enable_compression ? "启用" : "禁用",
                    config_.strategy.enable_incremental ? "启用" : "禁用");

        // 监控循环
        while (!should_stop_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 停止 file_watcher_ (它会停止 watch() 循环)
        file_watcher_.reset();
        
        // 停止所有 handler 的异步队列
        for (auto& handler : handlers_) {
            handler->stopAsyncBackup();
        }

        logger->info("--- 监控服务已停止 ---");

    } catch (const std::exception& e) {
        logger->error("监控线程异常: {}", e.what());
        is_monitoring_ = false;
        updateTrayTooltip();
    }
}

bool GuiApp::loadConfiguration() {
    try {
        auto config_opt = ConfigLoader::loadConfig("config.json");
        if (!config_opt) {
            return false;
        }
        config_ = *config_opt;

        auto presets_opt = ConfigLoader::loadPresets("presets.json");
        presets_ = presets_opt ? *presets_opt : nlohmann::json::object();

        return true;
    } catch (const std::exception& e) {
        std::string error = "加载配置时出错: " + std::string(e.what());
        std::wstring werror = Utf8ToWide(error);
        MessageBoxW(hwnd_, werror.c_str(), L"配置错误", MB_ICONERROR);
        return false;
    }
}

void GuiApp::updateTrayTooltip() {
    if (is_monitoring_) {
        wcscpy_s(nid_.szTip, L"CodeBackup - 运行中");
    } else {
        wcscpy_s(nid_.szTip, L"CodeBackup - 已停止");
    }
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

bool GuiApp::showSourceConfigDialog(BackupSource& source, bool isNew) {
    // 注册对话框窗口类
    static const wchar_t* className = L"SourceConfigDialogClass";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SourceConfigDialogProc;
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpszClassName = className;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    // 创建对话框数据
    SourceConfigData* dialogData = new SourceConfigData();
    dialogData->app = this;
    dialogData->source = &source;
    dialogData->presets = &presets_;
    dialogData->isNewSource = isNew;
    
    // 确保有 custom_filter
    if (!source.custom_filter) {
        source.custom_filter = std::make_optional<FilterConfig>();
    }

    // 创建对话框窗口
    HWND hwndDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        className,
        isNew ? L"添加备份源" : L"编辑备份源",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        hwnd_,
        nullptr,
        GetModuleHandle(nullptr),
        nullptr
    );

    if (!hwndDlg) {
        delete dialogData;
        return false;
    }

    dialogData->hwndDlg = hwndDlg;
    SetWindowLongPtrW(hwndDlg, GWLP_USERDATA, (LONG_PTR)dialogData);

    // 居中窗口
    RECT rect;
    GetWindowRect(hwndDlg, &rect);
    int x = (GetSystemMetrics(SM_CXSCREEN) - (rect.right - rect.left)) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - (rect.bottom - rect.top)) / 2;
    SetWindowPos(hwndDlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // 创建字体
    HFONT hFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        L"Segoe UI"
    );
    dialogData->hFont = hFont;

    // 路径/文件选择区域
    CreateWindowW(L"STATIC", L"需要备份的路径或文件:", WS_CHILD | WS_VISIBLE,
        20, 20, 120, 25, hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    HWND hEditPath = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        20, 50, 600, 30, hwndDlg, (HMENU)IDC_EDIT_SOURCE_PATH, GetModuleHandle(nullptr), nullptr);
    
    std::wstring pathWstr = Utf8ToWide(source.path);
    SetWindowTextW(hEditPath, pathWstr.c_str());

    CreateWindowW(L"BUTTON", L"选择文件夹", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        630, 50, 70, 30, hwndDlg, (HMENU)IDC_BTN_BROWSE_SOURCE, GetModuleHandle(nullptr), nullptr);
    
    CreateWindowW(L"BUTTON", L"选择文件", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        710, 50, 70, 30, hwndDlg, (HMENU)IDC_BTN_BROWSE_FILE, GetModuleHandle(nullptr), nullptr);

    // 启用复选框
    HWND hCheckEnabled = CreateWindowW(L"BUTTON", L"启用此备份源", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        20, 90, 150, 25, hwndDlg, (HMENU)IDC_CHECK_ENABLED, GetModuleHandle(nullptr), nullptr);
    SendMessageW(hCheckEnabled, BM_SETCHECK, source.enabled ? BST_CHECKED : BST_UNCHECKED, 0);

    // 预设列表
    CreateWindowW(L"STATIC", L"可用预设 (双击添加):", WS_CHILD | WS_VISIBLE,
        20, 130, 200, 25, hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    HWND hListPresets = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY,
        20, 160, 250, 200, hwndDlg, (HMENU)IDC_LIST_PRESETS, GetModuleHandle(nullptr), nullptr);
    dialogData->hListPresets = hListPresets;

    // 填充预设列表
    if (presets_.is_object()) {
        for (auto it = presets_.begin(); it != presets_.end(); ++it) {
            std::wstring presetName = Utf8ToWide(it.key());
            SendMessageW(hListPresets, LB_ADDSTRING, 0, (LPARAM)presetName.c_str());
        }
    }

    CreateWindowW(L"BUTTON", L"添加预设", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        20, 370, 120, 30, hwndDlg, (HMENU)IDC_BTN_ADD_PRESET, GetModuleHandle(nullptr), nullptr);

    // 自定义过滤器
    CreateWindowW(L"STATIC", L"自定义文件类型过滤 (会覆盖预设):", WS_CHILD | WS_VISIBLE,
        290, 130, 300, 25, hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    CreateWindowW(L"STATIC", L"模式:", WS_CHILD | WS_VISIBLE,
        290, 160, 60, 25, hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    HWND hComboMode = CreateWindowW(L"COMBOBOX", nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        350, 160, 200, 200, hwndDlg, (HMENU)IDC_COMBO_MODE, GetModuleHandle(nullptr), nullptr);
    dialogData->hComboMode = hComboMode;
    
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"白名单 (仅备份)");
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"黑名单 (排除)");
    SendMessageW(hComboMode, CB_ADDSTRING, 0, (LPARAM)L"无 (全部)");
    
    if (source.custom_filter) {
        if (source.custom_filter->mode == FilterConfig::Mode::Whitelist) SendMessageW(hComboMode, CB_SETCURSEL, 0, 0);
        else if (source.custom_filter->mode == FilterConfig::Mode::Blacklist) SendMessageW(hComboMode, CB_SETCURSEL, 1, 0);
        else SendMessageW(hComboMode, CB_SETCURSEL, 2, 0);
    } else {
        SendMessageW(hComboMode, CB_SETCURSEL, 2, 0);
    }

    CreateWindowW(L"STATIC", L"文件扩展名 (如: txt 或 .txt):", WS_CHILD | WS_VISIBLE,
        290, 200, 250, 25, hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    HWND hListExt = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        290, 230, 260, 130, hwndDlg, (HMENU)IDC_LIST_EXTENSIONS, GetModuleHandle(nullptr), nullptr);
    dialogData->hListExtensions = hListExt;

    // 填充扩展名
    if (source.custom_filter) {
        for (const auto& ext : source.custom_filter->extensions) {
            std::wstring extWstr = Utf8ToWide(ext);
            SendMessageW(hListExt, LB_ADDSTRING, 0, (LPARAM)extWstr.c_str());
        }
    }

    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", nullptr,
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        560, 230, 100, 25, hwndDlg, (HMENU)IDC_EDIT_EXTENSION, GetModuleHandle(nullptr), nullptr);

    CreateWindowW(L"BUTTON", L"添加", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        670, 230, 60, 25, hwndDlg, (HMENU)IDC_BTN_ADD_EXT, GetModuleHandle(nullptr), nullptr);

    CreateWindowW(L"BUTTON", L"删除", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        670, 265, 60, 25, hwndDlg, (HMENU)IDC_BTN_REMOVE_EXT, GetModuleHandle(nullptr), nullptr);

    // 添加说明文本
    CreateWindowW(L"STATIC", 
        L"💡 提示：\n"
        L"• 预设：快速应用预定义的过滤规则\n"
        L"• 自定义过滤器：精确控制要备份的文件类型\n"
        L"• 白名单：只备份列表中的文件类型\n"
        L"• 黑名单：排除列表中的文件类型，备份其他所有文件\n"
        L"• 如果同时设置预设和自定义过滤器，自定义过滤器优先",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        20, 410, 760, 90, hwndDlg, nullptr, GetModuleHandle(nullptr), nullptr);

    // 确定/取消按钮
    CreateWindowW(L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        580, 520, 100, 35, hwndDlg, (HMENU)IDC_BTN_SOURCE_OK, GetModuleHandle(nullptr), nullptr);

    CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        690, 520, 100, 35, hwndDlg, (HMENU)IDC_BTN_SOURCE_CANCEL, GetModuleHandle(nullptr), nullptr);

    // 设置字体
    EnumChildWindows(hwndDlg, [](HWND hwnd, LPARAM lParam) -> BOOL {
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
        return TRUE;
    }, (LPARAM)hFont);

    // 消息循环
    EnableWindow(hwnd_, FALSE);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT || !IsWindow(hwndDlg)) {
            break;
        }
        
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    EnableWindow(hwnd_, TRUE);
    SetForegroundWindow(hwnd_);
    if (IsWindow(hwndDlg)) {
        DestroyWindow(hwndDlg);
    }
    delete dialogData;
    DeleteObject(hFont);
    
    return true;
}

} // 结束命名空间