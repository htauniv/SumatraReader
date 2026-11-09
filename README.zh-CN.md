# SumatraReader

**为 macOS 做的轻量、离线优先电子书 / 文档阅读器 —— PDF 只是它支持的格式之一，中文界面真的能用。Intel 与 Apple Silicon 同一个包。**

**Modified from [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf)** —— 基于其尚未正式发布的跨平台重写版 `src/ng/`。这是社区非官方 fork，与上游作者无隶属关系，也不代表官方版本。

[English version](README.md) · [上游仓库](https://github.com/sumatrapdfreader/sumatrapdf) · [构建说明](README-mac.md) · 许可：GPLv3

## 为什么叫 Reader，而不是 PDF

上游的名字其实低估了这个程序：SumatraPDF 早已是通用电子书 / 文档阅读器，PDF 只是众多格式之一。这个 fork 保留了这种广度，改名为 **SumatraReader**，是让名字与实际能力一致，而不是让人误以为它只能看 PDF。

这些不是只在 Finder 里注册了扩展名——`ng` 构建确实把对应引擎编译进了应用（`src/ng/EngineCreate.cpp`、`src/base/GuessFileType.h`）：

| 类别 | 有对应引擎的格式 |
| --- | --- |
| 固定版式文档 | PDF、XPS / OXPS、DjVu、PostScript、DVI、CAJ（知网）、Markdown、Office Open XML（docx / xlsx / pptx，走 MuPDF） |
| 电子书与流式文本 | EPUB、MOBI / PalmDoc、FB2 / FB2Z、CHM、LIT、RTF、TXT、HTML |
| 漫画与图片书 | CBZ、CBR、CB7、CBT，以及单张图片（PNG、JPEG、GIF、TIFF、BMP、WebP、JPEG XL、JP2、HEIC、AVIF、SVG） |

两点必须说清楚：

- 上表描述的是**代码层面的支持**（已编译进二进制的引擎 + 文件类型表），并不等于每种格式都做过打开测试；目前用真书实测通过的是 PDF 与 EPUB。
- OFD 已在 Finder 注册，代码里也有通向 MuPDF 引擎的路径（`FileType::Ofd`），但 **macOS 上的实际渲染尚未验证**，因此不宣称支持该格式。

`SumatraPDF.cpp` 这类上游源文件名保持原名，以便将来继续与上游合并；改的只是用户可见的产品身份。

## 为什么要做这个 fork

SumatraPDF 是 Windows 平台阅读器；`src/ng/` 重写版虽然面向跨平台，但官方并未发布 macOS 构建。直接在 macOS 上编译 ng 版，还会碰到两个让「中文界面看起来坏了」的问题：

1. `cmd/trans-dl.ts` 依赖 `TRANS_UPLOAD_SECRET`。缺密钥时生成的 `.work/translations.txt` 是空文件，却照样被打包进 `embedded.lzsa`；运行时 `Translations.cpp` 静默回退英文，于是切换「中文」毫无反应，构建和运行都没有任何提示。
2. 非 Windows 的 `DetectUserLang()` 把 `zh_CN` / `zh-Hans-CN` 直接拿去查语言表，而 SumatraPDF 内部把简体中文叫 `cn`，匹配失败，中文系统首次启动仍是英文。
3. 即使语言码映射对了，从 Finder / Dock 启动的进程也拿不到 `$LANG`（launchd 不继承登录 shell 的环境），只看环境变量等于永远读到空值。

SumatraReader 把这三处修好。第 2、3 条已[反馈给上游](https://github.com/sumatrapdfreader/sumatrapdf/issues/6513)并由维护者以 `3c9b393` 修复（issue 已关闭）；第 1 条上游仍未处理——缺密钥时依然静默产出空翻译表，本 fork 的做法是自带翻译数据并让 `verify_build.py` 直接断言翻译表非空、键数正确，空表过不了构建检查。

范围收得很窄：macOS 13–26、Intel 与 Apple Silicon 统一二进制、只保留中英双语，再加几项对中文电子书真正有用的功能。

## 改了什么

| 方向 | 本 fork 的做法 |
| --- | --- |
| 产品名统一 | 改名为 **SumatraReader**，覆盖用户能看到的所有位置：`.app`、macOS 应用菜单、窗口标题、关于框、设置文件、应用数据目录、导出 PDF 的 `CreatorApp` 元数据；界面与文档中保留「基于 SumatraPDF」的致谢。 |
| 中文界面 | 用 apptranslator 公开历史接口重建 `translations.txt`（1208 组中英对照），把 `zh*` 语言环境映射到 `cn`，构建时 `SUMATRA_LANGS=en,cn`，并用运行时测试守住新增词条。改名后的词条自动复用上游中文翻译。 |
| 首次启动语言 | 先读 `CFLocaleCopyPreferredLanguages()`，再退回 `LC_ALL` / `LC_MESSAGES` / `LANG`，最后才是 `en`；每条 locale 都做别名表映射与逐段裁剪，`zh-Hans-CN` → `cn`，`en_GB.UTF-8` → `en`，`fr-FR` / `C` / `POSIX` 按"没有意见"处理。 |
| 两种 CPU 一个包 | `-arch universal` 分别编译 x86_64 与 arm64 切片再用 `lipo` 合并，两类 Mac 用同一个 `.app`；tesseract 的 x86 SIMD kernel、dav1d 的 CPU 探测按架构分派。 |
| macOS 部署目标 | 主程序与内嵌数据统一 `minos 13.0`（macOS 13 / 14 / 15 / 26），ad-hoc 签名，Info.plist 注册文档类型（含 OFD）。 |
| OCR 识别当前页 | Document 菜单，调用 Apple Vision 在设备本地识别中英混排，结果复制到剪贴板。 |
| AI 目录 | 为没有目录的 PDF 生成书签；从应用数据目录读取 `ai-toc.json`（`baseUrl` / `apiKey` / `model`），需要自备 LLM API key。 |
| 离线词典 | 查询选中单词，支持 StarDict 格式（`.ifo` / `.idx` / `.dict(.dz)`），词典放在 `<AppDataDir>/dict`。 |
| 内置离线手册 | 5 页中英手册打进应用，帮助菜单直接打开；源码在 `docs/manual/`，全新克隆无需外部文档仓库。 |

## 一个名字，一处来源

改名落实在代码里，不只是改文档：

- `cmd/helper/ng-app-config.ts` 定义 `APP_NAME = "SumatraReader"` 和 `BUNDLE_ID = "org.sumatrareader.SumatraReader"`，构建目标、可执行文件、bundle、Info.plist 全部读它。
- `src/ng/Version.h` 定义 `kAppName`，供 C++ 侧使用（应用菜单、窗口标题、关于框、应用数据目录、设置文件名）。
- `tools/verify_build.py` 会断言两边一致，并核对成品 `Info.plist`。

实际结果：产物 `out/mac/rel/SumatraReader.app`，bundle id `org.sumatrareader.SumatraReader`，数据目录 `~/Library/Application Support/SumatraReader/`，设置文件 `SumatraReader-settings.txt`。从改名前的版本升级时会自动沿用旧的 `SumatraPDF` 数据目录，设置、收藏、历史不会丢。

非用户可见处刻意保留上游名称：版权头、上游链接、apptranslator 的应用 id，以及 `SumatraPDF.cpp` / `SumatraPDF.icns` 这类源文件名——这样将来仍能与上游合并。上游原始 README 归档为 `UPSTREAM-README.md`。

## 当前状态

自动验证已通过：`tools/verify_build.py` 的全部 37 项静态检查（双架构、`minos 13.0`、bundle 身份、签名、翻译表 1208 键、内置手册 41 个归档条目及其与 `docs/manual/` 的逐字节一致）、翻译子系统运行时测试（17 个 locale 用例）、新增菜单词条的中文化、真书打开无崩溃，以及 Vision OCR 端到端测试（真实书页识别出 37 行中文）。

仍需人工验证：GUI 菜单点击全流程、AI 目录（需真实 API key）、词典查询（需真实 StarDict 词典文件）、arm64 切片在 Apple Silicon 实机上的运行（本机为 Intel，只验证到 `lipo` 架构正确、两切片 `minos` 一致、静态检查全过）。

暂不支持：macOS 27、中英以外的语言、Windows。这些是后续候选方向，不是承诺。

## 从源码构建

要求：macOS + Xcode Command Line Tools + [bun](https://bun.sh)。

```sh
SUMATRA_LANGS=en,cn bun cmd/ng-build.ts -mac -rel   # universal（默认），产物 out/mac/rel/SumatraReader.app
bun cmd/ng-build.ts -mac -rel -arch arm64           # 只出 Apple Silicon 切片
bun cmd/ng-build.ts -mac -rel -clean                # 全量重建
```

构建直接调用 clang++，不生成 Xcode 工程；`ext/` 里的第三方依赖首次全量编译，之后按切片增量复用。构建标识与本地构建历史由构建脚本自动管理，规则见 `README-mac.md`。

验证：

```sh
python3 tools/verify_build.py --archs x86_64,arm64
bash tools/trans_runtime_test.sh
bash tools/ocr_endtoend_test.sh 某本书.pdf
```

## 致谢

这个 fork 站在很多人的工作之上，感谢：

- **Krzysztof J. Kowalczyk 与 SumatraPDF 的每一位贡献者**（完整名单见 `AUTHORS`）——感谢他们多年维护这款阅读器，也感谢 `src/ng/` 重写版，本项目的地基在此。
- **[apptranslator.org](https://www.apptranslator.org/) 的志愿译者**——本项目内置的 1208 组中英对照文案，来自社区翻译历史的无偿贡献。
- **MuPDF / Artifex 团队，以及 `ext/` 中所有第三方库的作者**——PDF、EPUB、DjVu、漫画等格式能真正渲染出来，靠的是他们。
- **[SumatraPDF plus](https://github.com/dengxibo/sumatrapdf-plus)**——OCR、AI 目录、离线词典的功能思路参考；本 fork 针对自身 `ng` / macOS 栈重新实现。
- **Apple 的 Vision 框架**——让中文识别可以在本机离线完成，文字不必离开这台 Mac。
- **每一位提交 issue、给出反馈、借出真书做测试的朋友**——最初那句「切换中文没反应」，正是这个 fork 的起点。
- **AI 编程助手（WorkBuddy Agent）**——这次 macOS 移植里的构建排障、中文化、统一改名、验证脚本和文档，大部分由它与我结对完成。工具不署名，功劳得记上；方向和每一次验收仍然是我，代码里的错也该算我的。

也感谢上游以 GPLv3 开放代码，二次开发才成为可能。若还有该被写进这里的人或项目，欢迎提 issue 告诉我。

## 来源与许可

- **Modified from [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf)**，作者与贡献者见 `AUTHORS`，GPLv3（`COPYING`），部分代码为 BSD（`COPYING.BSD`）。
- 功能思路参考了 [SumatraPDF plus](https://github.com/dengxibo/sumatrapdf-plus)；本 fork 的 OCR、AI 目录、词典代码是面向自身 `src/ng/` + macOS 栈重新实现的。
- 界面文案来自 [apptranslator.org](https://www.apptranslator.org/) 社区翻译项目。
- MuPDF 及 `ext/` 内其它第三方库各自保留原许可。

本 fork 沿用上游的 **GPLv3** 许可，全部修改都在本仓库内。SumatraReader 与官方 SumatraPDF 无关联、未获背书，也不是官方替代品。
