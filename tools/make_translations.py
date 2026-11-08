#!/usr/bin/env python3
"""Build .work/translations.txt for the local SumatraPDF ng build.

Upstream's cmd/trans-dl.ts requires TRANS_UPLOAD_SECRET; without it the build
embeds an EMPTY translations.txt and Translations.cpp silently calls
FallbackToEnglish(), so switching the UI to Chinese does nothing.

apptranslator exposes the per-language history without a secret:
  https://www.apptranslator.org/api/transhist?app=SumatraPDF&lang=cn
This script matches those translations against the Tr()/TrN()/HostTr()
literals actually compiled into this checkout and emits the exact format
ParseTranslationsTxt() expects:

  AppTranslator: SumatraPDF
  <sha1>
  :<C-escaped english literal>
  cn:<C-escaped chinese translation>
  ...

The matching is deliberately conservative: the left-hand side is verbatim
the C++ literal, because ParseTranslationsTxt() runs UnescapeTemp() on it and
compares the result with the runtime string passed to Tr(). Only the lookup
key (used to find a translation) is normalised.

Usage: python3 tools/make_translations.py [history.json] [repo_root]
"""

import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
WORKSPACE = os.path.dirname(HERE)
DEFAULT_HISTORY = "/tmp/trans_cn.json"
DEFAULT_ROOT = os.path.join(WORKSPACE, "sumatrapdf")

# Matches trans-dl.ts: /\bTrN?\("(.*?)"\)/g plus /\bHostTr\((?:StrL\()?"(.*?)"\)/g
LITERAL_PATTERNS = [
    re.compile(r'\bTrN?\("((?:[^"\\]|\\.)*)"'),
    re.compile(r'\bHostTr\((?:StrL\()?"((?:[^"\\]|\\.)*)"'),
]

# C++ concatenates adjacent string literals at compile time, so
#   Tr("Could not update %s." " Please close the file.")
# reaches Tr() as one string, not two. Emitting only the first literal would
# produce a translations.txt key that never matches at runtime.
STRING_LIT = r'"(?:[^"\\]|\\.)*"'
ADJACENT_CALL = re.compile(r'\b(?:TrN?|HostTr)\(\s*((?:' + STRING_LIT + r'\s*)+)\)')

TIPS_PATTERN = re.compile(
    r'(?:static )?Str sumatraTips = StrL\(R"tips\(([\s\S]*?)\)tips"\);', re.M
)

SOURCE_EXTS = (".cpp", ".mm")

# Strings added upstream after the last apptranslator snapshot. These are the
# ones a mac user can actually see; the Windows-installer-only messages are
# deliberately left in English (that code never runs in this build). Keys are
# the runtime, unescaped English strings; printf placeholders must keep the
# same order and count.
EXTRA_CN = {
    # mac plus features (OCR / AI TOC / dictionary), not in apptranslator yet
    "AI table of contents failed": "AI 目录生成失败",
    "AI table of contents generated": "AI 目录已生成，见书签侧栏",
    "AI table of contents is only available for fixed-page documents": "AI 目录仅支持固定版式文档（PDF 等）",
    "Create ai-toc.json with baseUrl, apiKey and model in the app data folder": "请先在应用数据文件夹创建 ai-toc.json（含 baseUrl、apiKey、model）",
    "Dictionary: %s": "词典查询：%s",
    "Generate AI Table Of Contents": "生成 AI 目录",
    "Generating AI table of contents...": "正在生成 AI 目录……",
    "Lookup Selected Word": "查询选中的单词",
    "No dictionary entry found": "词典中未查到该词",
    "No dictionaries found in the dict folder": "dict 文件夹中没有可用词典",
    "No text recognized": "未识别到文字",
    "OCR Current Page": "OCR 识别当前页",
    "OCR failed": "OCR 识别失败",
    "Recognized text copied to clipboard": "识别结果已复制到剪贴板",
    "Select a word first": "请先选中一个单词",
    # existing hand-added translations
    "Paused · %s · %s": "已暂停 · %s · %s",
    "Paused · %s · page %d of %d · %s": "已暂停 · %s · 第 %d / %d 页 · %s",
    "Reading · %s · %s": "朗读中 · %s · %s",
    "Reading · %s · page %d of %d · %s": "朗读中 · %s · 第 %d / %d 页 · %s",
    "Rotated %d°": "已旋转 %d°",
    "↑↓ speed · 0-9 · − reverse · Space pause · Esc stop": "↑↓ 调速 · 0-9 · − 反向 · 空格暂停 · Esc 停止",
    'Font "%s" not found, using the default font': '未找到字体"%s"，改用默认字体',
    "SumatraPDF crashed.\n\nPress 'Cancel' to see how to report the crash.":
        "SumatraPDF 已崩溃。\n\n按“取消”查看如何报告此问题。",
    "You have version '%s' and version '%s' is available.\nDo you want to install the new version?":
        "当前版本为 '%s'，可用版本为 '%s'。\n是否安装新版本？",
    "Place rectangle annotation. Drag or click twice. **Shift** for a square. **Esc** to cancel.":
        "放置矩形注释：拖动或点击两次；按住 **Shift** 画正方形；**Esc** 取消。",
    "Place polyline annotation. **Double-click**, **right-click**, **Space**, or **Enter** to finish, **Ctrl+click** to close it. **Shift** to snap to multiples of 45 degrees. **Esc** to cancel.":
        "放置折线注释：**双击**、**右键**、**空格**或**回车**结束；**Ctrl+点击**闭合；**Shift** 按 45 度吸附；**Esc** 取消。",
    "**Arrows**: extend selection * **V**: cursor mode * **Esc**: exit keyboard selection":
        "**方向键**：扩展选区 * **V**：光标模式 * **Esc**：退出键盘选择",
    "**Arrows**: move the caret * **Shift+Arrows**: select * **V**: selection mode * **Esc**: exit keyboard selection":
        "**方向键**：移动光标 * **Shift+方向键**：选择 * **V**：选择模式 * **Esc**：退出键盘选择",
    "Claude Code is not signed in. Open a terminal, run \"claude auth login\", then try again.":
        "Claude Code 尚未登录。请在终端运行 “claude auth login” 后重试。",
    "Antigravity CLI is not signed in. Open a terminal, run \"antigravity auth login\", then try again.":
        "Antigravity CLI 尚未登录。请在终端运行 “antigravity auth login” 后重试。",
    "Sending selection failed: the server doesn't accept requests from web pages (CORS). Use Method = POST-VIA-BROWSER":
        "发送选区失败：服务器不接受来自网页的请求（CORS）。请将 Method 设为 POST-VIA-BROWSER",
    "This file is over 50 MiB. SumatraPDF will not upload it. The website still accepts files up to 200 MiB.":
        "该文件超过 50 MiB，SumatraPDF 不会上传。网站仍可接受最大 200 MiB 的文件。",
}


def c_unescape(s):
    """Byte-for-byte mirror of trans::UnescapeTemp() in Translations.cpp.

    The C++ version walks bytes, not code points: "\\xC2\\xB7" becomes the two
    bytes C2 B7, i.e. UTF-8 U+00B7. Treating each \\xNN as its own code point
    would produce the mojibake "Â·" and fail to find the translation for every
    string that uses the middle dot, degree sign or arrows.
    """
    buf = bytearray()
    i = 0
    n = len(s)
    simple = {
        "\\": b"\\",
        '"': b'"',
        "'": b"'",
        "t": b"\t",
        "n": b"\n",
        "r": b"\r",
    }
    while i < n:
        c = s[i]
        i += 1
        if c != "\\" or i >= n:
            buf.extend(c.encode("utf-8"))
            continue
        c2 = s[i]
        i += 1
        if c2 in simple:
            buf.extend(simple[c2])
        elif c2 == "x" and i + 2 <= n and all(ch in "0123456789abcdefABCDEF" for ch in s[i:i + 2]):
            buf.append(int(s[i:i + 2], 16))
            i += 2
        else:
            # UnescapeTemp keeps the backslash for unknown escapes
            buf.extend(b"\\")
            buf.extend(c2.encode("utf-8"))
    return buf.decode("utf-8", errors="replace")


def c_escape(s):
    """Inverse of c_unescape for the translation side of each pair."""
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\n":
            out.append("\\n")
        else:
            out.append(ch)
    return "".join(out)


def norm_key(s):
    """Loose lookup key: drop Win32 '&' accelerators, \t shortcuts, spacing."""
    s = s.replace("&", "")
    s = re.sub(r"\t.*$", "", s, flags=re.S)
    s = re.sub(r"\s+", " ", s).strip()
    return s.lower()


def collect_literals(root):
    src = os.path.join(root, "src")
    literals = set()
    nfiles = 0
    for dirpath, _dirs, files in os.walk(src):
        for name in files:
            if not name.lower().endswith(SOURCE_EXTS):
                continue
            path = os.path.join(dirpath, name)
            nfiles += 1
            try:
                text = open(path, encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            # First collect calls whose whole argument is adjacent literals and
            # add the concatenated form. Remember their spans so the simple
            # patterns below don't also add the useless first fragment.
            joined_spans = []
            for m in ADJACENT_CALL.finditer(text):
                parts = re.findall(STRING_LIT, m.group(1))
                literals.add("".join(p[1:-1] for p in parts))
                joined_spans.append((m.start(), m.end()))
            for pat in LITERAL_PATTERNS:
                for m in pat.finditer(text):
                    if any(a <= m.start() < b for a, b in joined_spans):
                        continue
                    literals.add(m.group(1))
    # home-page tips are translated at runtime with Tr(line)
    tips_path = os.path.join(src, "shared", "HomePageCommon.cpp")
    if os.path.isfile(tips_path):
        text = open(tips_path, encoding="utf-8", errors="replace").read()
        m = TIPS_PATTERN.search(text)
        if m:
            for line in m.group(1).split("\n"):
                line = line.strip()
                if line and not line.startswith("//"):
                    literals.add(c_escape(line))
    return literals, nfiles


def load_history(hist_path):
    with open(hist_path, encoding="utf-8") as f:
        raw = json.load(f)
    exact = {}
    for rec in raw:
        if not (isinstance(rec, list) and len(rec) >= 2):
            continue
        eng, info = rec[0], rec[1]
        if not isinstance(info, list) or not info or not isinstance(info[0], str):
            continue
        tr = info[0]
        if tr.strip():
            exact[eng] = tr
    loose = {}
    for eng, tr in exact.items():
        loose.setdefault(norm_key(eng), tr)
    return exact, loose


def main():
    hist_path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_HISTORY
    root = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_ROOT

    exact, loose = load_history(hist_path)
    print(f"Chinese records: {len(exact)}", flush=True)

    literals, nfiles = collect_literals(root)
    print(f"Source literals from {nfiles} C++/ObjC++ files: {len(literals)}", flush=True)

    pairs = []
    n_exact = n_loose = n_extra = n_alias = 0
    unmatched = []
    for lit in sorted(literals):
        actual = c_unescape(lit)
        tr = exact.get(actual)
        if tr:
            n_exact += 1
        else:
            tr = loose.get(norm_key(actual))
            if tr:
                n_loose += 1
        if not tr:
            tr = EXTRA_CN.get(actual)
            if tr:
                n_extra += 1
        if not tr and "SumatraReader" in actual:
            # This fork renamed the product, but apptranslator's history is
            # keyed on the upstream English string. Reuse that translation for
            # the renamed key (swapping the brand inside the Chinese text too),
            # so a rename never silently costs us a translation.
            old = actual.replace("SumatraReader", "SumatraPDF")
            old_tr = exact.get(old) or loose.get(norm_key(old)) or EXTRA_CN.get(old)
            if old_tr:
                tr = old_tr.replace("SumatraPDF", "SumatraReader")
                n_alias += 1
        if not tr:
            unmatched.append(actual)
            continue
        # c_escape encodes real newlines as \n, which UnescapeTemp turns back
        # into newlines at runtime, so multi-line messages are safe here.
        pairs.append((":" + lit, "cn:" + c_escape(tr)))

    body = []
    for a, b in pairs:
        body.append(a)
        body.append(b)
    content = "\n".join(body) + "\n"
    digest = hashlib.sha1(content.encode("utf-8")).hexdigest()
    out = "AppTranslator: SumatraPDF\n" + digest + "\n" + content

    dst = os.path.join(root, ".work", "translations.txt")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)
    print(f"Matched: {len(pairs)} (exact {n_exact}, normalized {n_loose}, local {n_extra}, "
          f"renamed-brand {n_alias}, skipped {len(unmatched)})", flush=True)
    print(f"Wrote {dst} ({len(out.encode('utf-8'))} bytes)", flush=True)
    if unmatched:
        report = os.path.join(HERE, "untranslated.txt")
        with open(report, "w", encoding="utf-8") as f:
            for s in unmatched:
                f.write(s.replace("\n", "\\n") + "\n")
        print(f"Untranslated list: {report}", flush=True)
    if not pairs:
        print("ERROR: no translations matched", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
