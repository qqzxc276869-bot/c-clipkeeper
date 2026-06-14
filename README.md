# ClipKeeper C：Windows 剪贴板历史小工具

ClipKeeper 是一个用纯 C 和 Win32 API 写的小型桌面效率工具。它会监听 Windows 剪贴板里的文本变化，自动保存历史记录，并提供搜索、预览、置顶、删除、清空、复制回剪贴板等功能。

这个项目适合面试展示，因为它不是玩具算法题，而是一个完整的本地小工具：有 GUI、有系统 API、有动态内存管理、有本地持久化，也能讲清楚真实用户场景。

## 功能

- 自动监听 `CF_UNICODETEXT` 剪贴板文本。
- 保存历史到 `clips.tsv`，关闭程序后下次仍可恢复。
- 搜索历史内容，支持中文文本。
- 选中记录后查看完整预览。
- 一键复制历史记录回剪贴板。
- 置顶常用片段。
- 删除单条记录或清空全部记录。
- 暂停监听，避免临时复制内容进入历史。
- 支持开机自启动，使用当前用户的注册表 Run 项，不需要管理员权限。
- 支持白天/夜间配色切换，并保存到 `clipkeeper.ini`。
- 支持最小化/关闭到系统托盘，托盘菜单可打开窗口、暂停监听或退出。
- 支持可选全局快捷键唤起主窗口；默认关闭，可在软件内自行录入组合键。
- 支持开机自启动时隐藏到托盘；手动双击 exe 会正常打开主窗口。
- 界面采用 iOS 17 灵感的拟液态玻璃风格：渐变背景、圆角玻璃面板、胶囊按钮和柔和列表高光。
- 设置项使用自绘胶囊开关，避免默认 Win32 复选框破坏整体视觉。

## 项目结构

```text
c-clipkeeper/
  main.c             Win32 GUI、窗口事件、控件布局
  clip_store.h       剪贴板历史数据结构和接口
  clip_store.c       动态数组、去重、搜索、TSV 保存/加载
  clipboard_win.h    Windows 剪贴板接口声明
  clipboard_win.c    OpenClipboard / GetClipboardData / SetClipboardData 封装
  build_gcc.bat      MinGW/GCC 编译脚本
  build_msvc.bat     Visual Studio cl 编译脚本
  clipkeeper.ini     运行后生成，保存主题、启动隐藏和快捷键设置
```

## 编译

### MinGW / GCC

```powershell
cd C:\Users\27686\Desktop\gpt\c-clipkeeper
gcc -Wall -Wextra -std=c11 main.c clip_store.c clipboard_win.c -o ClipKeeper.exe -mwindows -ladvapi32 -lshell32
```

或者直接运行：

```powershell
.\build_gcc.bat
```

### Visual Studio Developer Command Prompt

```powershell
cd C:\Users\27686\Desktop\gpt\c-clipkeeper
cl /W4 /D_CRT_SECURE_NO_WARNINGS main.c clip_store.c clipboard_win.c /Fe:ClipKeeper.exe user32.lib gdi32.lib advapi32.lib shell32.lib
```

或者直接运行：

```powershell
.\build_msvc.bat
```

## 使用

1. 启动 `ClipKeeper.exe`。
2. 复制任意文本，窗口左侧会出现一条历史记录。
3. 在搜索框输入关键词，可以快速过滤历史。
4. 选中记录后，右侧预览完整内容。
5. 点击 `复制到剪贴板`，把历史记录重新写回剪贴板。
6. 点击 `置顶`，把常用片段固定在列表前面。
7. 勾选 `暂停监听`，暂停自动监听。
8. 勾选 `开机自启动`，下次登录 Windows 后自动启动。
9. 勾选 `启动时隐藏`，下次开机自启动时直接进入托盘；手动双击 exe 仍会打开窗口。
10. 勾选 `白天模式/夜间模式`，切换界面配色。
11. 最小化或关闭窗口时，程序会隐藏到系统托盘；右键托盘图标可以退出。
12. 需要快捷键时，勾选 `启用快捷键`，点击 `设置快捷键`，再按下你想使用的组合键；默认不会注册任何快捷键。

数据文件 `clips.tsv` 和设置文件 `clipkeeper.ini` 会生成在程序同目录，方便查看和备份。

## 面试讲解点

- 为什么选择 C：这个工具需要直接调用 Windows 剪贴板 API，C 可以展示系统编程和资源管理能力。
- GUI 架构：`main.c` 只处理窗口消息和用户交互，数据逻辑放在 `clip_store.c`，剪贴板 API 放在 `clipboard_win.c`。
- 剪贴板监听：使用 `AddClipboardFormatListener` 接收 `WM_CLIPBOARDUPDATE`，比定时轮询更轻量。
- 开机自启动：写入 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`，属于用户级设置，不要求管理员权限。
- 主题系统：白天/夜间配色由 `clipkeeper.ini` 持久化，窗口重绘时动态选择背景、列表和文字颜色。
- 托盘常驻：使用 `Shell_NotifyIconW` 创建通知区图标，关闭窗口时隐藏而不是退出。
- 全局快捷键：默认不注册，用户在界面中录入组合键后才调用 `RegisterHotKey`；如果组合键被占用，会提示并自动关闭快捷键。
- UI 自绘：使用 GDI 绘制渐变背景、圆角玻璃卡片、owner-draw 按钮、开关和列表项，不依赖第三方 UI 库。
- Unicode 支持：内部使用 `wchar_t`，剪贴板读写使用 `CF_UNICODETEXT`，文件保存为 UTF-8 TSV。
- 持久化设计：TSV 文件简单可读，换行、Tab、反斜杠会转义，避免破坏记录结构。
- 去重策略：复制到已有文本时刷新时间，不重复堆积相同记录。
- 内存管理：历史记录使用动态数组，文本内容单独分配，退出时统一释放。

## 可继续扩展

- 增加导出 JSON。
- 增加最大历史条数配置。
- 增加敏感词过滤，避免保存密码或验证码。

## 本轮新增亮点

- 敏感内容过滤：默认跳过疑似密码、验证码、Token、密钥、银行卡号等内容，减少隐私泄露风险。
- 保存上限设置：主界面可设置最大历史条数，超出后优先清理未置顶的旧记录。
- 快速搜索弹窗：点击 `快速搜索` 或使用已设置的全局快捷键，可弹出轻量搜索窗口；输入关键字后按回车或双击记录即可复制回剪贴板。
