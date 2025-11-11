#include "gui_app.h"
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 创建互斥量，确保只运行一个实例
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"CodeBackup_SingleInstance_Mutex_2024");
    
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // 已经有一个实例在运行
        MessageBoxW(nullptr, 
                   L"CodeBackup 已经在运行中！\n\n请在系统托盘中查找图标。", 
                   L"CodeBackup", 
                   MB_ICONINFORMATION | MB_OK);
        
        if (hMutex) {
            CloseHandle(hMutex);
        }
        return 0;
    }
    
    // 运行应用程序
    int result = 0;
    {
        CodeBackup::GuiApp app;
        result = app.run();
    }
    
    // 释放互斥量
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    
    return result;
}