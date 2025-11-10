@echo off
chcp 65001 >nul
echo ========================================
echo 中文预设测试脚本
echo ========================================
echo.

echo 1. 检查预设文件...
if exist "presets_中文.json" (
    echo    ✓ 找到 presets_中文.json
) else (
    echo    ✗ 未找到 presets_中文.json
    goto :end
)

echo.
echo 2. 备份现有 presets.json...
if exist "presets.json" (
    copy /Y "presets.json" "presets.json.bak" >nul
    echo    ✓ 已备份到 presets.json.bak
) else (
    echo    ! 没有现有的 presets.json
)

echo.
echo 3. 复制中文预设文件...
copy /Y "presets_中文.json" "presets.json" >nul
if %errorlevel% == 0 (
    echo    ✓ 已复制 presets_中文.json 为 presets.json
) else (
    echo    ✗ 复制失败
    goto :end
)

echo.
echo 4. 创建测试文件夹...
if not exist "测试文件夹" (
    mkdir "测试文件夹" >nul
    echo    ✓ 已创建测试文件夹
) else (
    echo    ! 测试文件夹已存在
)

echo.
echo 5. 创建测试文件...
echo 这是一个测试文件 > "测试文件夹\测试.txt"
echo print("Hello") > "测试文件夹\测试.py"
echo #include ^<iostream^> > "测试文件夹\测试.cpp"
echo    ✓ 已创建测试文件

echo.
echo ========================================
echo 准备完成！
echo ========================================
echo.
echo 现在可以：
echo 1. 运行 build\Release\main_gui.exe
echo 2. 右键托盘图标 → "备份格式"
echo 3. 查看中文预设是否正确显示
echo.
echo 预设包括：
echo    - 代码文件
echo    - 文档
echo    - 图片
echo    - 音频
echo    - 视频
echo    - 压缩包
echo    - 电子书
echo    - C++项目
echo    - Python项目
echo    - Web前端
echo    - 排除临时文件
echo    - 排除编译产物
echo.

:end
pause
