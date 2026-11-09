# 常见问题 / Troubleshooting

## 提示「无法验证开发者」/ Gatekeeper warning

这是 ad-hoc 签名、未公证的本地构建。右键点击 `.app` 选择「打开」，或执行一次：

```
xattr -cr /path/to/SumatraReader.app
```

This build is ad-hoc signed and not notarized. Right-click → Open, or clear the quarantine attribute.

## 切换中文没反应 / Language switch does nothing

本构建已把简体中文翻译数据（1208 条）嵌入程序，语言表只保留 `English` 与 `简体中文`。
若仍无变化，请确认：

1. 完全退出旧进程后重新打开（旧二进制内没有翻译数据）。
2. `~/Library/Application Support/SumatraReader/SumatraReader-settings.txt` 里 `UiLanguage = cn`。
3. 在 `Settings → Change Language` 里显式选择 `简体中文`。

The very first local build shipped an empty `translations.txt`, which made the app silently fall
back to English. That is fixed in this build. Quit the old process before launching the new `.app`.

## 系统语言是中文但首次启动仍是英文 / First launch stayed English

Finder 启动的程序没有 `LANG` / `LC_ALL` 环境变量，只读环境变量的探测必然读到空值。
本构建的判定顺序是：先 `CFLocaleCopyPreferredLanguages()`（系统偏好语言），
无结果才看 `LC_ALL` → `LC_MESSAGES` → `LANG`，最后才回退 `en`。
每个值都会做别名映射与逐段裁剪，`zh-Hans-CN` → `cn`、`en_GB.UTF-8` → `en`；
`fr-FR`、`C`、`POSIX` 这类没有对应语言表的输入按「没有意见」处理，不会被猜成中文。
注意判定是**系统偏好优先、环境变量兜底**：在中文系统上即使 `LANG=en_US.UTF-8`，
首次启动仍会是中文。想用英文，请在 `Settings → Change Language` 里明确选一次
（选择结果写进设置文件，之后以设置为准）。

The system preferred languages win over the environment, so exporting `LANG=en_US.UTF-8` on a
Chinese system will still start in Chinese. Pick the language once in
`Settings → Change Language` instead; the choice is persisted in the settings file.

## 中文显示为方块 / Chinese text shows as boxes

界面文字走 CoreText，实测会自动回退到 `.PingFang UI SC`。如果 PDF 正文的中文缺字，
那是文档没有内嵌 CJK 字体，程序会在首次使用时下载 **Source Han Serif（思源宋体，
`SourceHanSerif-Regular.ttc`，约 24 MB）** 作为回退，需要一次联网。

首次下载时 macOS 会弹出「SumatraReader 想连接到 files.sumatrapdfreader.org」并要求确认，
这是系统的**网络访问授权**提示，不是应用在采集数据。本构建只有 ad-hoc 签名（无开发者
身份），所以系统会同时显示「Suspicious package」字样。点「允许」即可，字体只下载一次
并缓存，之后离线可用。若不想联网：把该字体放进缓存目录，或放弃对无内嵌字体的扫描件的
中文回退。

UI text uses CoreText's font cascade and should never be boxes. Missing glyphs inside a PDF mean
the document lacks an embedded CJK font; SumatraReader downloads Source Han Serif (~24 MB) once.
macOS will ask for permission to reach `files.sumatrapdfreader.org` at that moment, and flags the
ad-hoc signed build as a "suspicious package" - that is the OS network prompt, not telemetry.
Allow it once; the font is cached and later opens work offline.

## 手册打开后跳到浏览器 / Help opens a browser

只有当前二进制内嵌了手册资源时才会显示离线手册窗口。本构建已嵌入；若仍跳浏览器，
说明运行的是旧版本，或 `embedded.lzsa` 被替换过。按 `F1` 打开，再按一次关闭。

## macOS 13/14/15 无法启动 / Won't launch on older macOS

确认运行的是这个构建（不是最初那版 `minos 26.0`）。可用以下命令查看：

```
otool -l /path/to/SumatraReader.app/Contents/MacOS/SumatraReader | grep -A4 LC_BUILD_VERSION
```

应显示 `minos 13.0`。若显示 `minos 26.0`，那是旧产物。

## 设置与日志位置 / Files

- 设置：`~/Library/Application Support/SumatraReader/SumatraReader-settings.txt`
- 从改名前的版本升级且本机已存在 `~/Library/Application Support/SumatraPDF/` 时，
  程序会继续沿用旧目录，设置与历史不会丢失（全新安装才使用 `SumatraReader/`）。
- 高级设置、快捷键、主题、历史记录都写在这里；删除该文件可恢复默认（会重新按系统语言选择中/英）。

## 性能 / Performance

Release 构建使用 `-O2` 与系统框架，无调试日志开销。页面渲染走 mupdf，
首次打开超大 PDF 或扫描版会有缓存预热；连续滚动通常明显更快。

相关页面：[快速开始](Quick-start.md)、[macOS 集成](macOS-notes.md)、[快捷键](Keyboard-shortcuts.md)。
