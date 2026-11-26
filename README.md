# NewsX

Windows 桌面悬浮组件，采用 C++20、Qt Quick 6.8 和 WebView2。

- TORA 实时行情、自选股票和五档详情。
- 微博热搜、账号登录及官方网页详情。
- 小红书官方网页登录与浏览、财联社头条阅读。
- 深浅主题、透明度、置顶及常规 / 极简 / 四边隐藏模式。

## 使用

安装 `dist/NewsX-NSIS-Setup.exe`；源码构建后运行 `native-newsx/run.cmd`。
行情服务器地址在界面中设置。浏览网页需要 Microsoft Edge WebView2 Runtime。

快捷键：`Ctrl+1` 自选、`Ctrl+2` 微博、`Ctrl+3` 财联社、`Ctrl+5` 小红书；`Ctrl+R` 刷新，`Esc` 返回或关闭弹层。

## 构建与打包

需要 64 位 Windows、Visual Studio 2026 C++ 工具和 CMake 3.24+。准备以下依赖（不提交 Git）：

- Qt 6.8.3 MSVC x64 SDK：`native-newsx/qt/6.8.3/msvc2022_64/`
- TORA C++ SDK：`native-newsx/C++_API_v1.0.9_20250825/`

在仓库根目录执行：

```powershell
.\native-newsx\build.cmd
.\native-newsx\run.cmd
```

生成 NSIS 安装包：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\native-newsx\package.ps1
```

`build.cmd` 全量编译原生程序，不运行测试；默认输出 `native-newsx/build/app/NewsXNative.exe`。启动和打包脚本均读取 CMake 当前输出目录。WebView2 SDK 与 NSIS 工具按需下载并校验，安装包输出至 `dist/NewsX-NSIS-Setup.exe`。

## 数据与目录

配置、缓存及账号数据保存在 `%APPDATA%\NewsXWidget\`；微博会话使用 Windows DPAPI 加密。官网账号由官方登录页面处理。

- `native-newsx/src/`：业务、行情和窗口控制。
- `native-newsx/qml/`：界面及网页容器。
- `native-newsx/installer/`：NSIS 安装脚本。

仓库仅保留 C++ 版本。Qt SDK、TORA SDK、构建产物和安装包不纳入 Git。品牌图标来自 Simple Icons（CC0）。
