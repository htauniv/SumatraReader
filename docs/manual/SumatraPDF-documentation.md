# SumatraReader 使用手册 / User Manual

这是为 **macOS 本地构建** 编写的内置简明手册，可完全离线阅读（按 `F1` 或 `⌘F1` 打开/关闭）。
This is the built-in, offline manual written for this **local macOS build**.

- 适用系统 / Supported systems: macOS 13 (Ventura) ~ macOS 26 (Tahoe)
- 架构 / Architecture: universal binary — x86_64 + arm64 in one `.app`, ad-hoc signed, not notarized
- 界面语言 / UI languages: English + 简体中文 (Simplified Chinese)

上游完整在线文档（英文）：`https://www.sumatrapdfreader.org/docs/SumatraPDF-documentation`

## 目录 / Contents

- [快速开始 / Quick start](Quick-start.md)
- [快捷键 / Keyboard shortcuts](Keyboard-shortcuts.md)
- [macOS 集成 / macOS integration](macOS-notes.md)
- [常见问题 / Troubleshooting](Troubleshooting.md)

## 这个版本是什么 / About this build

上游 SumatraPDF 原版是 Windows 专用的 Win32 程序。本构建（SumatraReader）来自上游仓库里的 `src/ng/` 重写版，
它使用 gpui 自绘界面并以 macOS / Linux / WebAssembly 为目标，因此可以直接编译成原生 `.app`，
不是移植层或兼容层。

The original SumatraPDF is Windows-only. This build comes from the upstream `src/ng/` rewrite,
which targets macOS / Linux / WebAssembly with a gpui-drawn UI, so it compiles into a native `.app`
rather than running through a compatibility layer.

需要说明：`src/ng/` 仍是上游进行中的重写，功能与观感与成熟的 Windows 版存在结构性差距
（例如界面为自绘而非原生 Cocoa 控件）。本构建在其之上补齐了可修的部分：macOS 13+ 兼容、
中英双语、字体回退、原生标题栏文件代理、内置手册。

Note that `src/ng/` is still an in-progress rewrite. Its feature set and polish differ structurally
from the mature Windows build. This local build fixes the parts that can be fixed: macOS 13+
compatibility, the English/Chinese UI, CJK font fallback, the native title-bar file proxy and this manual.
