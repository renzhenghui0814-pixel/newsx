# NewsX Native

C++20 / Qt Quick 重构版。此目录独立于原 Python 应用；旧版源码与构建脚本不参与本工程。

## 构建

Qt 6.8.3 MSVC x64 SDK 安装在本目录 `qt/`（不纳入 Git）。
将 TORA C++ SDK 解压在 `C++_API_v1.0.9_20250825/`（不纳入 Git）。
安装 Visual Studio 2026 C++ 工具后运行：

```cmd
native-newsx\build.cmd
```

构建使用 CMake、MSVC x64；程序输出在 `build/app/NewsXNative.exe`。
`build.cmd` 只编译原生程序，不运行测试。`run.cmd` 启动 `build/app/NewsXNative.exe`。
运行 `powershell -ExecutionPolicy Bypass -File package.ps1` 可部署 Qt 运行库并用 NSIS 3.13 生成 `dist/NewsX-NSIS-Setup.exe`。脚本将便携版 NSIS 下载到被忽略的 `.tools/` 并校验哈希。安装向导包括欢迎页、目录选择、桌面快捷方式选项、安装进度和完成页；完成页可勾选立即运行。默认安装目录为 `%LOCALAPPDATA%\Programs\NewsX`，开始菜单快捷方式始终创建，卸载项写入当前用户注册表。选择受保护的目录需以管理员身份运行安装包。
应用沿用 `%APPDATA%\NewsXWidget\config.json` 和 `cache.json` 格式。
微博热搜与财联社头条分别使用 `hot_refresh_minutes` 和 `headline_refresh_minutes` 配置自动刷新间隔，单位为分钟，默认各 10 分钟；页面顶部可手动刷新，底部齿轮可设置 1–1440 分钟。
