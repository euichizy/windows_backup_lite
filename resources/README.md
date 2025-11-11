# 应用图标资源

## 文件说明

- `favicon.ico` - 当前使用的应用程序图标文件
- `app_icon.rc` - Windows 资源脚本文件
- `resource.h` - 资源 ID 定义头文件

## 如何替换图标

### 方法 1：替换现有图标文件（推荐）
1. 准备你的图标文件（.ico 格式）
2. 将图标文件命名为 `favicon.ico`
3. 替换 `resources/` 目录下的现有文件
4. 重新编译项目

### 方法 2：使用新的图标文件名
1. 将你的图标文件放到 `resources/` 目录
2. 编辑 `resources/app_icon.rc`，修改图标文件名：
   ```rc
   IDI_ICON1 ICON "你的图标文件名.ico"
   ```
3. 重新编译项目

## 图标规格建议

- **格式**：ICO
- **推荐尺寸**：16x16, 32x32, 48x48, 256x256
- **颜色深度**：32位（支持透明度）
- **用途**：
  - 16x16 和 32x32：系统托盘图标
  - 48x48：任务栏和窗口标题栏
  - 256x256：高分辨率显示

## 在线图标制作工具

- https://www.icoconverter.com/
- https://convertio.co/zh/png-ico/
- https://favicon.io/
- https://www.favicon-generator.org/

## 编译说明

修改图标后需要重新编译项目：

```cmd
# 重新配置（如果需要）
cmake --preset=default

# 编译
cmake --build build --config Release

# 如果图标没有更新，清理后重新编译
cmake --build build --target clean
cmake --build build --config Release
```

## 技术细节

- 图标资源 ID：`IDI_ICON1` (值为 101)
- 资源在 `src/gui_app.cpp` 的 `createTrayIcon()` 方法中加载
- 使用 Windows API `LoadIcon()` 函数加载
- 如果自定义图标加载失败，会自动回退到系统默认图标
