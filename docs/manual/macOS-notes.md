# macOS 集成 / macOS integration

本构建针对 macOS 做了以下适配。Most of these are produced by the app's native
`NSWindow`/`NSMenu` layer (gpui) plus a small amount of platform glue.

## 系统版本与架构 / Deployment target

- 部署目标 `minos 13.0`，即 macOS 13 Ventura、14 Sonoma、15 Sequoia、26 Tahoe 均可启动。
- **通用二进制**：同一个 `.app` 内含 `x86_64` 与 `arm64` 两个切片，Intel 与 Apple Silicon
  都原生运行，不需要 Rosetta 2。可用 `lipo -archs SumatraReader.app/Contents/MacOS/SumatraReader`
  确认（输出 `x86_64 arm64`）。
- 链接时保留 `__availability_version_check` 弱引用：gpui 用 `@available()` 保护 10.15/11.3/13/14 才有的 API，
  在旧系统上不会因强引用新符号而启动失败。
- `Info.plist` 里 `LSMinimumSystemVersion = 13.0`，`CFBundleLocalizations = en, zh_CN`。

The app is a universal binary: one `.app` with both `x86_64` and `arm64` slices, so Intel and
Apple Silicon Macs each run natively with no Rosetta 2 involved.

## 原生界面行为 / Native behaviour

| 特性 | 说明 |
|---|---|
| 应用菜单 | 原生 `NSMenu`（程序名菜单、关于、设置、退出、窗口等），菜单文字随中英切换 |
| 输入法 | 实现 `NSTextInputClient`，中文输入法候选/组合文本可用 |
| 深色模式 | 主题选 `System` 时跟随系统外观；未强制 Aqua |
| Retina | `NSHighResolutionCapable`；画布按 `backingScaleFactor` 渲染 |
| 双显卡 | `NSSupportsAutomaticGraphicsSwitching`，Intel 双显卡自动切换 |
| 全屏 | 原生全屏，非自绘假全屏 |
| 标签 | 使用程序自带标签页，已关闭 macOS 自动窗口标签以免出现两层标签栏 |
| 文件代理图标 | 标题栏显示文档代理图标，可拖拽；`⌘` 点击标题显示所在路径层级 |
| 最近打开 | 打开的文档登记到 Dock / 苹果菜单的「打开最近使用」 |
| 打开方式 | Finder「打开方式」与拖放通过 Apple Event 传递，启动期间的文档会排队 |

## 中文字体 / Chinese fonts

中文界面由 CoreText 自动回退到 `.PingFang UI SC`（实测），不需要额外字体即可显示中文。
CJK 文档内嵌字体缺失时，程序仍会按需下载 Noto CJK 回退字体（与上游原版行为一致）。

Chinese UI text is rendered via CoreText's automatic cascade to `.PingFang UI SC`; no extra font
install is needed. PDFs that lack embedded CJK fonts may still trigger the app's on-demand
Noto CJK font download, as in the original build.

## 签名 / Code signing

本地构建使用 ad-hoc 签名并通过 `codesign --verify --deep --strict`，但未使用 Developer ID 公证。
从压缩包解压后若被 Gatekeeper 拦截，右键「打开」或执行：

```
xattr -cr /path/to/SumatraReader.app
```

This is an ad-hoc signed, unnotarized local build. Gatekeeper may require the command above.

## 尚未具备 / Known gaps

- 内置手册是本地编写的精简版，不是上游网站完整文档；上游手册源仓库是私有的，CI 构建同样不含手册。
- 部分 Windows 专属功能（安装器、资源管理器预览、注册表关联）在 macOS 上不存在。
- 未做 Developer ID 公证，因此首次打开可能需要 `xattr -cr`（见上）。

The upstream full manual lives in a private repository, so CI builds ship without it too; this
build instead includes a compact locally written manual.
