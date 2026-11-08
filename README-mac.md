# SumatraReader — macOS 构建说明

项目首页：[README.md](README.md)（English）· [README.zh-CN.md](README.zh-CN.md)（中文）。

本仓库是 [SumatraPDF](https://github.com/sumatrapdfreader/sumatrapdf) 上游 ng 重写版
（`src/ng/`）的 macOS Intel fork，改名为 **SumatraReader**。官方目前没有 macOS 版本，
本仓库提供可直接从源码复现的本地构建。许可：GPLv3（与上游一致，见 `COPYING`）。

范围：macOS 13 / 14 / 15 / 26，**Intel x86_64**，界面语言仅英文 + 简体中文。
Apple Silicon（arm64）与 macOS 27 暂不支持。

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
SUMATRA_LANGS=en,cn bun cmd/ng-build.ts -mac -rel    # 产物 out/mac/rel/SumatraReader.app
bun cmd/ng-build.ts -mac -rel -clean                 # 全量重建
bun cmd/ng-build.ts -mac -rel -rev 3                 # 手工指定当日版本后缀
```

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
python3 tools/verify_build.py                          # 静态检查：架构/minos/bundle 身份/签名/翻译/手册
bash tools/trans_runtime_test.sh                       # 翻译子系统运行时验证
bash tools/ocr_endtoend_test.sh <某本书.pdf>            # OCR 端到端：渲染真书页 → Vision 识别
python3 tools/make_translations.py /tmp/trans_cn.json  # 从 apptranslator 历史重建翻译表
```

`make_translations.py` 用公开接口 `https://www.apptranslator.org/api/transhist?app=SumatraPDF&lang=cn`
取翻译数据（先自行下载为 JSON）。它按源码中实际存在的 `Tr()` 字面量匹配；产品名改名后的词条
会自动复用上游同名中文翻译并替换品牌，不会因改名丢中文。

## 与上游的主要差异

- `cmd/helper/ng-app-config.ts`（新）：产品名与 bundle id 唯一来源
- `cmd/helper/ng-mac-config.ts`：`MAC_MIN_VERSION = 13.0`
- `cmd/helper/ng-compile.ts` / `ng-targets.ts` / `ng-mac-bundle.ts`：部署目标注入、Vision framework、bundle 与文档类型（含 OFD）
- `cmd/helper/ng-version.ts` + `cmd/ng-build.ts`：自动版本号（禁用数字规则）与构建历史
- `cmd/trans-gen.ts`：`SUMATRA_LANGS` 语言裁剪
- `cmd/gen-docs.ts` + `docs/manual/`（新）：离线手册的 5 页中英 markdown 随仓库提供，
  构建不再依赖仓库外的 `sumatra-website` 检出；静态资源在 `docs/`
- `src/shared/Translations.cpp`、`AppSettings.cpp`：中文运行时、系统语言识别、设置文件名
- `src/ng/Version.h`、`AppTools_posix.cpp`、`HomePage.cpp`、`gui/AppShell.cpp`：产品名与数据路径
- `src/ng/ocr/`、`src/ng/aitoc/`、`src/ng/dict/`：OCR / AI 目录 / 离线词典
- `src/ng/PlusCmd.h`、`OcrCmd.cpp`、`AiTocCmd.cpp`、`DictCmd.cpp`：命令胶水层
- `tools/`：验证、翻译生成与回归脚本

上游原始 README 已归档为 `UPSTREAM-README.md`。
