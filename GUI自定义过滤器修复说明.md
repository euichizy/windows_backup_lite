# GUI 自定义过滤器修复说明

## 🐛 问题描述

**现象**：在 GUI 中设置自定义过滤器后，保存的配置文件中仍然包含 `"presets": ["default"]`，导致自定义过滤器被忽略。

**原因**：
1. 添加备份源时，默认设置了 `presets = {"default"}`
2. 在源配置对话框中设置自定义过滤器后，没有清空预设列表
3. 保存配置时，同时保存了 `presets` 和 `filter`，导致冲突

## ✅ 修复方案

### 修复 1：移除默认预设

**文件**：`src/gui_app.cpp`  
**位置**：`IDC_BTN_ADD_SOURCE` 按钮处理

**修改前**：
```cpp
BackupSource new_source;
new_source.enabled = true;
new_source.presets = {"default"};  // ❌ 默认添加 default 预设
```

**修改后**：
```cpp
BackupSource new_source;
new_source.enabled = true;
new_source.presets = {};  // ✅ 不设置默认预设，让用户自己选择
```

### 修复 2：设置自定义过滤器时清空预设

**文件**：`src/gui_app.cpp`  
**位置**：`IDC_BTN_SOURCE_OK` 按钮处理

**新增逻辑**：
```cpp
// 如果设置了自定义过滤器（非None模式且有扩展名），清空预设列表
// 因为自定义过滤器优先级更高
if (data->source->custom_filter->mode != FilterConfig::Mode::None && 
    !data->source->custom_filter->extensions.empty()) {
    data->source->presets.clear();
}
```

### 修复 3：添加预设时清空自定义过滤器

**文件**：`src/gui_app.cpp`  
**位置**：`IDC_BTN_ADD_PRESET` 按钮处理

**新增逻辑**：
```cpp
// 添加预设时，清空自定义过滤器（因为预设优先级较低）
if (data->source->custom_filter) {
    data->source->custom_filter->mode = FilterConfig::Mode::None;
    data->source->custom_filter->extensions.clear();
    
    // 清空界面上的扩展名列表
    SendMessageW(data->hListExtensions, LB_RESETCONTENT, 0, 0);
    // 重置模式选择为"无"
    SendMessageW(data->hComboMode, CB_SETCURSEL, 2, 0);
}
```

## 🎯 修复后的行为

### 场景 1：只使用预设
1. 添加备份源
2. 双击左侧预设列表（如 `code`）
3. 点击"确定"保存

**生成的配置**：
```json
{
  "path": "D:\\Projects",
  "enabled": true,
  "presets": ["code"]
}
```
✅ 正确：只有预设，没有 filter

### 场景 2：只使用自定义过滤器
1. 添加备份源
2. 选择模式：白名单
3. 添加扩展名：`.py`, `.js`
4. 点击"确定"保存

**生成的配置**：
```json
{
  "path": "D:\\Projects",
  "enabled": true,
  "presets": [],
  "filter": {
    "mode": "whitelist",
    "extensions": [".py", ".js"]
  }
}
```
✅ 正确：只有 filter，presets 为空

### 场景 3：先添加预设，再设置自定义过滤器
1. 添加备份源
2. 双击添加预设 `code`
3. 然后选择白名单模式
4. 添加扩展名：`.txt`
5. 点击"确定"保存

**生成的配置**：
```json
{
  "path": "D:\\Projects",
  "enabled": true,
  "presets": [],
  "filter": {
    "mode": "whitelist",
    "extensions": [".txt"]
  }
}
```
✅ 正确：自定义过滤器优先，预设被清空

### 场景 4：先设置自定义过滤器，再添加预设
1. 添加备份源
2. 选择白名单模式，添加扩展名 `.py`
3. 然后双击添加预设 `code`
4. 点击"确定"保存

**生成的配置**：
```json
{
  "path": "D:\\Projects",
  "enabled": true,
  "presets": ["code"],
  "filter": {
    "mode": "none",
    "extensions": []
  }
}
```
✅ 正确：预设生效，自定义过滤器被清空

## 📋 用户体验改进

### 1. 清晰的提示信息
添加预设时会显示：
```
已添加预设: code
提示：使用预设时，自定义过滤器已清空
```

### 2. 互斥逻辑
- 添加预设 → 自动清空自定义过滤器
- 设置自定义过滤器 → 自动清空预设列表
- 避免配置冲突和混淆

### 3. 界面同步
- 添加预设时，扩展名列表自动清空
- 模式选择自动重置为"无"
- 界面状态与实际配置保持一致

## 🔍 验证方法

### 测试步骤 1：验证自定义过滤器
1. 启动程序，右键托盘图标 → "备份设置"
2. 点击"添加"
3. 选择路径：`D:\Test`
4. 选择模式：白名单
5. 添加扩展名：`txt`, `md`
6. 点击"确定" → "保存"
7. 打开 `config.json` 文件

**预期结果**：
```json
{
  "path": "D:\\Test",
  "enabled": true,
  "presets": [],
  "filter": {
    "mode": "whitelist",
    "extensions": [".txt", ".md"]
  }
}
```

### 测试步骤 2：验证预设
1. 添加备份源
2. 双击预设 `code`
3. 点击"确定" → "保存"
4. 打开 `config.json` 文件

**预期结果**：
```json
{
  "path": "D:\\Test",
  "enabled": true,
  "presets": ["code"]
}
```
（没有 filter 字段，或 filter 为空）

### 测试步骤 3：验证互斥逻辑
1. 添加备份源
2. 先添加预设 `code`
3. 再选择白名单模式，添加 `.txt`
4. 点击"确定" → "保存"
5. 打开 `config.json` 文件

**预期结果**：
```json
{
  "path": "D:\\Test",
  "enabled": true,
  "presets": [],
  "filter": {
    "mode": "whitelist",
    "extensions": [".txt"]
  }
}
```
（presets 为空，只有 filter）

## ⚠️ 注意事项

### 1. 配置优先级
- **自定义过滤器优先级更高**
- 如果同时存在 `presets` 和 `filter`，`filter` 生效
- 修复后确保只保存一种配置方式

### 2. 向后兼容
- 旧的配置文件仍然可以正常加载
- 如果旧配置同时有 `presets` 和 `filter`，按优先级处理
- 建议重新保存配置以清理冲突

### 3. 用户操作建议
- **推荐**：优先使用预设（简单快速）
- **特殊需求**：使用自定义过滤器（精确控制）
- **避免混用**：不要同时设置预设和自定义过滤器

## 📊 修复前后对比

### 修复前
```json
{
  "path": "D:\\Projects",
  "enabled": true,
  "presets": ["default"],  // ❌ 不想要的默认预设
  "filter": {
    "mode": "whitelist",
    "extensions": [".py", ".js"]
  }
}
```
**问题**：同时存在 presets 和 filter，造成混淆

### 修复后
```json
{
  "path": "D:\\Projects",
  "enabled": true,
  "presets": [],  // ✅ 清空预设
  "filter": {
    "mode": "whitelist",
    "extensions": [".py", ".js"]
  }
}
```
**正确**：只有 filter，配置清晰

## 🎉 总结

### 修复内容
✅ 移除了默认的 "default" 预设  
✅ 设置自定义过滤器时自动清空预设  
✅ 添加预设时自动清空自定义过滤器  
✅ 添加了清晰的提示信息  
✅ 确保配置文件的一致性  

### 用户体验
✅ 配置更加清晰，不会混淆  
✅ 避免了预设和自定义过滤器的冲突  
✅ 界面操作更加直观  
✅ 保存的配置文件更加简洁  

### 建议
- 优先使用预设（适合大多数场景）
- 特殊需求才使用自定义过滤器
- 不要混用两种方式
- 修改配置后记得重启监控服务

---

**现在 GUI 中的自定义过滤器功能已经完全正常工作了！**
