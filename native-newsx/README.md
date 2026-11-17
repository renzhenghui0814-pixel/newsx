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
运行 `powershell -ExecutionPolicy Bypass -File package.ps1` 可将 Qt 运行库部署到安装包，生成仓库根目录 `dist/NewsX-Setup.exe`。安装按当前用户写入 `%LOCALAPPDATA%\Programs\NewsX`，创建开始菜单快捷方式与卸载项，无需管理员权限。
应用沿用 `%APPDATA%\NewsXWidget\config.json` 和 `cache.json` 格式。
微博热搜与财联社头条分别使用 `hot_refresh_minutes` 和 `headline_refresh_minutes` 配置自动刷新间隔，单位为分钟，默认各 10 分钟；页面顶部可手动刷新，底部齿轮可设置 1–1440 分钟。
