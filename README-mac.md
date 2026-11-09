# SumatraReader — macOS 构建说明

项目首页：[README.md](README.md)（English）· [README.zh-CN.md](README.zh-CN.md)（中文）。

本仓库是 [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf) 上游 ng 重写版
（`src/ng/`）的 macOS fork，改名为 **SumatraReader**。官方目前没有 macOS 版本，
本仓库提供可直接从源码复现的本地构建。许可：GPLv3（与上游一致，见 `COPYING`）。

范围：macOS 13 / 14 / 15 / 26，**Intel x86_64 与 Apple Silicon arm64 统一二进制**
（universal 2，一包同跑两类机器），界面语言仅英文 + 简体中文。
macOS 27 暂不支持（未在新系统上验证，`MAC_MIN_VERSION` 与 `minos` 均为 13.0，
向下兼容由部署目标保证）。

## 产品名的一致性

改名已落实在代码里，构建侧与 C++ 侧各有一个唯一来源，`tools/verify_build.py` 会断言两者一致：

| 位置 | 值 |
|---|---|
| `cmd/helper/ng-app-config.ts` | `APP_NAME = "SumatraReader"`、`BUNDLE_ID = "org.sumatrareader.SumatraReader"` |
| `src/ng/Version.h` | `kAppName = "SumatraReader"`、`kUpstreamAppName = "SumatraPDF"`（仅致谢与升级迁移） |
| 产物 | `out/mac/rel/SumatraReader.app`，可执行文件 `Contents/MacOS/SumatraReader` |
| 数据 | `~/Library/Application Support/SumatraReader/SumatraReader-settings.txt` |

旧版升级时，若本机已存在 `~/Library/Application Support/SumatraPDF/`，程序会继续沿用旧目录，
避免设置、收藏、历史丢失。版权头、上游链接、apptranslator 应用 id 与
`SumatraPDF.cpp` / `SumatraPDF.icns` 等源文件名保持上游原样，便于后续合并。

## 从源码构建

要求：macOS + Xcode Command Line Tools + [bun](https://bun.sh)。

```sh
SUMATRA_LANGS=en,cn bun cmd/ng-build.ts -mac -rel                  # universal，out/mac/rel/SumatraReader.app
bun cmd/ng-build.ts -mac -rel -arch universal                      # 同上，显式写法
bun cmd/ng-build.ts -mac -rel -arch arm64                          # 只出 Apple Silicon 切片
bun cmd/ng-build.ts -mac -rel -arch x86_64                         # 只出 Intel 切片
bun cmd/ng-build.ts -mac -rel -clean                               # 全量重建
bun cmd/ng-build.ts -mac -rel -rev 3                               # 手工指定当日版本后缀
```

`-arch` 的默认值是 `universal`：release 构建总是产出两类机器都能跑的包。
每个切片有独立输出目录 `out/mac/rel-<arch>/`，因此切片之间不会混用目标文件，
增量重编也只影响该切片；最后用 `lipo` 合并到 `out/mac/rel/SumatraReader` 再打包。
只在本机构建调试时用单切片会快很多（省掉另一半 `ext/` 依赖的编译）。

架构不是纯粹加个编译选项：tesseract 的 SIMD kernel 是 x86 汇编，arm64 无法汇编；
dav1d 的 CPU 探测按架构分 `x86/` 与 `arm/`；lzsa 内嵌对象若不显式传 `-arch`
会停在宿主架构导致另一切片链接时找不到 `gEmbeddedLzsa`。
这些都在 `cmd/helper/ng-targets.ts` 的 `perArch` 层里分别声明。

`SUMATRA_LANGS=en,cn` 必须带上：裸跑 `cmd/trans-gen.ts` 会恢复上游的全量语言表，
语言选择器里会出现大量实际回退英文的条目。

编译直接调用 clang++（不生成 Xcode 工程），首次会编译 `ext/` 里全部 vendored 依赖，
之后增量。构建标识由 `.work/build-version.json` 的当日计数器自动分配，历史写入
`.work/build-history.txt`（含真实日期与 git commit）。

约定：版本号中**不出现数字 4 或 7**。当日序号会自动跳过含这两个数字的候选；
若日期本身带这两个数字（如某些日、月、年份），只把该位平移到相邻允许数字，
并借助构建历史去重，保证每个版本仍然唯一。显式 `-rev` 同样遵守该规则。

## 验证工具（tools/）

```sh
python3 tools/verify_build.py --archs x86_64,arm64     # 静态检查：架构/minos/bundle 身份/签名/翻译/手册
python3 tools/verify_build.py --archs x86_64           # 只构建了 Intel 切片时
bash tools/trans_runtime_test.sh                       # 翻译子系统运行时验证
bash tools/ocr_endtoend_test.sh <某本书.pdf>            # OCR 端到端：渲染真书页 → Vision 识别
python3 tools/make_translations.py /tmp/trans_cn.json  # 从 apptranslator 历史重建翻译表
```

`verify_build.py` 的架构断言跟着 `--archs` 走（37 项检查其余部分与架构无关），
所以同一套检查可以分别对单切片和 universal 包执行。它会比对 `lipo -archs`、
`file` 的描述、`minos`，以及 `.app` 内那份二进制是否与 `out/mac/rel/SumatraReader`
切片一致——这份一致性很容易在重新打包时被漏掉。

`make_translations.py` 用公开接口 `https://www.apptranslator.org/api/transhist?app=SumatraPDF&lang=cn`
取翻译数据（先自行下载为 JSON）。它按源码中实际存在的 `Tr()` 字面量匹配；产品名改名后的词条
会自动复用上游同名中文翻译并替换品牌，不会因改名丢中文。

## 界面语言的判定顺序

macOS 从 Finder / Dock 启动的进程拿不到 `$LANG`（launchd 不继承登录 shell 的环境），
只读环境变量会导致中文系统上界面始终是英文。因此判定顺序是：

1. `CFLocaleCopyPreferredLanguages()` 返回的系统偏好语言（`src/ng/SystemLocale_mac.mm`）
2. 环境变量 `LC_ALL` → `LC_MESSAGES` → `LANG`（命令行直接启动时有效）
3. 都取不到时回退 `en`

每一段都走 `LangCodeFromLocale()`：小写化、`_` 归一为 `-`，然后逐段裁剪直到命中语言码。
`zh-Hans-CN` → `zh-hans` → `cn`，`en_GB.UTF-8` → `en`，`fr-FR` / `C` / `POSIX` / 空
都不命中，按"没有意见"处理而不是猜成英文。本构建只打包 `en` + `cn`，
所以 `zh-Hant` / `zh-TW` / `zh-HK` / `zh-MO` 一律落到 `cn`（上游指向 `tw`，
但这里没有 `tw` 数据，指向它等于给繁体用户一个空界面）。

`tools/trans_runtime_test.sh` 覆盖 17 个 locale 用例，包括上面这几个必须**不**映射的输入。

## 与上游的主要差异

- `cmd/helper/ng-app-config.ts`（新）：产品名与 bundle id 唯一来源
- `cmd/helper/ng-mac-config.ts`：`MAC_MIN_VERSION = 13.0`
- `cmd/helper/ng-compile.ts` / `ng-targets.ts` / `ng-mac-bundle.ts`：部署目标注入、架构切片
  （`-arch` 与 `perArch` 层）、Vision framework、bundle 与文档类型（含 OFD）
- `cmd/helper/embedded.ts`：内嵌 LZSA 归档按切片架构生成对象
- `cmd/ng-build.ts`：`-arch` 解析、逐切片构建与 `lipo` 合并
- `cmd/helper/ng-version.ts` + `cmd/ng-build.ts`：自动版本号（禁用数字规则）与构建历史
- `cmd/trans-gen.ts`：`SUMATRA_LANGS` 语言裁剪
- `cmd/gen-docs.ts` + `docs/manual/`（新）：离线手册的 5 页中英 markdown 随仓库提供，
  构建不再依赖仓库外的 `sumatra-website` 检出；静态资源在 `docs/`
- `src/ng/SystemLocale_mac.mm`（新）：直读 `CFLocaleCopyPreferredLanguages()`，
  解决 Finder 启动无 `$LANG` 导致的中文系统英文界面
- `src/shared/Translations.cpp`、`AppSettings.cpp`：locale 别名表与逐段裁剪、中文运行时、设置文件名
- `src/ng/Version.h`、`AppTools_posix.cpp`、`HomePage.cpp`、`gui/AppShell.cpp`：产品名与数据路径
- `src/ng/ocr/`、`src/ng/aitoc/`、`src/ng/dict/`：OCR / AI 目录 / 离线词典
- `src/ng/PlusCmd.h`、`OcrCmd.cpp`、`AiTocCmd.cpp`、`DictCmd.cpp`：命令胶水层
- `tools/`：验证、翻译生成与回归脚本

上游原始 README 已归档为 `UPSTREAM-README.md`。
