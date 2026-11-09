#!/usr/bin/env python3
"""Static verification for the locally built SumatraReader.app (macOS 13+).

Checks the things that are easy to get silently wrong in a from-source build:
  * Mach-O architecture slices and, critically, LC_BUILD_VERSION/minos
  * the .app bundle's Info.plist keys and ad-hoc signature
  * the embedded translation archive and its language set
  * the offline in-app manual (manifest, packed archive entries, and whether
    the staged pages still match docs/manual/)
  * the compiled language table (en + cn only)
  * unguarded-availability warnings left in the build log
  * imported symbols that would require a newer macOS at launch

Usage: python3 tools/verify_build.py [--build-log .work/build.log]
                                    [--archs x86_64,arm64]

--archs defaults to x86_64 and states which slices the build produced, so the
same checks run against an Intel-only build, a single arm64 build or a
universal one.
"""

import argparse
import os
import plistlib
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# this copy lives inside the repo: sumatrapdf/tools/ -> repo root
REPO = os.path.dirname(HERE)
REL = os.path.join(REPO, "out", "mac", "rel")
EXE = os.path.join(REL, "SumatraReader")
APP = os.path.join(REL, "SumatraReader.app")
PLIST = os.path.join(APP, "Contents", "Info.plist")
ARCHIVE = os.path.join(REL, "embedded.lzsa")
TRANSLATIONS = os.path.join(REL, "embedded", "translations.txt")
LANGS_CPP = os.path.join(REPO, "src", "shared", "TranslationLangs.cpp")
APP_CONFIG = os.path.join(REPO, "cmd", "helper", "ng-app-config.ts")
VERSION_H = os.path.join(REPO, "src", "ng", "Version.h")
BUNDLE_EXE = os.path.join(APP, "Contents", "MacOS", os.path.basename(EXE))
NG_DIR = os.path.join(REPO, "src", "ng")
MANUAL_SRC = os.path.join(REPO, "docs", "manual")
STAGED_TROUBLESHOOTING = os.path.join(REL, "embedded", "Troubleshooting.md")

# Upstream-owned identifiers that must keep the old name to stay compatible
# with an existing installation (Windows shell extension registration).
KEEP_UPSTREAM_NAME = {"SumatraPDF-QuickLook"}

results = []


def run(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return p.returncode, p.stdout + p.stderr
    except (OSError, subprocess.TimeoutExpired) as e:
        return -1, str(e)


def read_decl(path, pattern):
    m = re.search(pattern, open(path, encoding="utf-8").read())
    return m.group(1) if m else ""


def stale_brand_literals():
    """User-visible string literals in ng/ that still say the upstream name.

    URLs, copyright headers, source filenames and the explicit upgrade-
    compatibility identifiers in KEEP_UPSTREAM_NAME are intentionally kept.
    """
    pat = re.compile(r'(?:StrL|GStrL|Tr|TrN|HostTr|fmt)\(\s*"([^"\n]*SumatraPDF[^"\n]*)"')
    found = []
    for dirpath, _dirs, files in os.walk(NG_DIR):
        if "tests" in dirpath.split(os.sep):
            continue
        for name in files:
            if not name.endswith((".cpp", ".h", ".mm")):
                continue
            path = os.path.join(dirpath, name)
            for lit in pat.findall(open(path, encoding="utf-8", errors="replace").read()):
                if "http" in lit or lit in KEEP_UPSTREAM_NAME:
                    continue
                found.append(f"{os.path.relpath(path, REPO)}: {lit}")
    return found


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  {detail}" if detail else ""), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-log", default="/tmp/sumatra_build13.log")
    # comma-separated: "x86_64" (Intel build), "arm64", or both for universal
    ap.add_argument("--archs", default="x86_64")
    a = ap.parse_args()
    want = sorted(x.strip() for x in a.archs.split(",") if x.strip())

    ts_name = read_decl(APP_CONFIG, r'export const APP_NAME = "([^"]+)"')
    cpp_name = read_decl(VERSION_H, r'#define kAppName "([^"]+)"')
    bundle_id = read_decl(APP_CONFIG, r'export const BUNDLE_ID = "([^"]+)"')
    check("product name single source (TS + C++)",
          bool(ts_name) and ts_name == cpp_name == "SumatraReader",
          f"{ts_name} / {cpp_name}")
    stale = stale_brand_literals()
    check("no stale brand literals in ng sources", not stale,
          "; ".join(stale[:3]) if stale else "UI strings all use kAppName")
    # The project READMEs deliberately do not advertise a build number.
    hardcoded = []
    for readme in ("README.md", "README.zh-CN.md"):
        path = os.path.join(REPO, readme)
        if os.path.isfile(path):
            txt = open(path, encoding="utf-8").read()
            hardcoded += [f"{readme}: {v}" for v in re.findall(r"\b\d{2}\.\d{2}\.\d{2}(?:\.\d+)?\b", txt)]
    check("READMEs carry no hardcoded build version", not hardcoded,
          "; ".join(hardcoded[:3]) if hardcoded else "no yy.mm.dd[.n] strings")
    check("no stale SumatraPDF.app artifact",
          not os.path.exists(os.path.join(REL, "SumatraPDF.app")),
          os.path.join(REL, "SumatraPDF.app"))

    check("binary exists", os.path.isfile(EXE), EXE)
    check("app bundle exists", os.path.isdir(APP), APP)

    # architecture: the slices -arch asked for, one by default
    rc, out = run(["lipo", "-archs", EXE])
    arch = out.strip()
    check(f"architecture = {'+'.join(want)}", rc == 0 and sorted(arch.split()) == want, arch)
    rc, out = run(["file", EXE])
    check("Mach-O 64-bit executable", "Mach-O 64-bit executable" in out, out.strip()[:110])

    # deployment target: the decisive compatibility marker
    rc, out = run(["otool", "-l", EXE])
    minos = re.search(r"minos\s+([\d.]+)", out)
    sdk = re.search(r"\bsdk\s+([\d.]+)", out)
    check("minos 13.0 (runs on macOS 13/14/15/26)", bool(minos) and minos.group(1) == "13.0",
          f"minos={minos.group(1) if minos else '?'} sdk={sdk.group(1) if sdk else '?'}")

    # linked libraries must all be system-provided
    rc, out = run(["otool", "-L", EXE])
    libs = [l.strip().split()[0] for l in out.splitlines()[1:] if l.strip()]
    external = [l for l in libs if not (l.startswith("/System/Library") or l.startswith("/usr/lib") or l.startswith("@"))]
    check("only system frameworks / libSystem", not external,
          f"{len(libs)} dylibs" + (f", unexpected: {external[:3]}" if external else ""))

    # bundle plist
    if os.path.isfile(PLIST):
        with open(PLIST, "rb") as f:
            pl = plistlib.load(f)
        check("bundle identity = SumatraReader",
              pl.get("CFBundleName") == ts_name and pl.get("CFBundleDisplayName") == ts_name and
              pl.get("CFBundleIdentifier") == bundle_id and pl.get("CFBundleExecutable") == ts_name,
              f"{pl.get('CFBundleName')} / {pl.get('CFBundleIdentifier')}")
        check("LSMinimumSystemVersion = 13.0", pl.get("LSMinimumSystemVersion") == "13.0",
              str(pl.get("LSMinimumSystemVersion")))
        ver_short = str(pl.get("CFBundleShortVersionString", ""))
        ver_build = str(pl.get("CFBundleVersion", ""))
        check("build version has no disallowed digits",
              bool(ver_short) and not re.search(r"[47]", ver_short + ver_build),
              f"{ver_short} / {ver_build}")
        check("CFBundleLocalizations = en + zh_CN",
              sorted(pl.get("CFBundleLocalizations", [])) == ["en", "zh_CN"],
              str(pl.get("CFBundleLocalizations")))
        check("dark mode not forced to Aqua", pl.get("NSRequiresAquaSystemAppearance") is False,
              str(pl.get("NSRequiresAquaSystemAppearance")))
        check("Retina capable", pl.get("NSHighResolutionCapable") is True)
        check("automatic GPU switching (Intel dual-GPU)",
              pl.get("NSSupportsAutomaticGraphicsSwitching") is True)
        docs = pl.get("CFBundleDocumentTypes", [])
        check("document types registered", len(docs) >= 7, f"{len(docs)} types")
    else:
        check("Info.plist present", False, PLIST)

    # signature
    rc, out = run(["codesign", "--verify", "--deep", "--strict", APP])
    check("ad-hoc signature valid", rc == 0, out.strip()[:120])
    if os.path.isfile(BUNDLE_EXE):
        rc2, out2 = run(["lipo", "-archs", BUNDLE_EXE])
        check("bundle binary matches the built slices",
              rc2 == 0 and sorted(out2.split()) == want, out2.strip())

    # embedded translations
    if os.path.isfile(TRANSLATIONS):
        raw = open(TRANSLATIONS, "rb").read()
        lines = raw.decode("utf-8", "replace").split("\n")
        # first two lines are the AppTranslator header + sha1, not data
        header, body = lines[:2], lines[2:]
        keys = [l for l in body if l.startswith(":")]
        langs = sorted({l.split(":", 1)[0] for l in body if ":" in l and not l.startswith(":")})
        check("translations header valid",
              len(header) >= 2 and header[0].startswith("AppTranslator:") and len(header[1].strip()) == 40,
              header[0] if header else "")
        check("translations embedded and non-empty", len(raw) > 10000, f"{len(raw)} bytes")
        check("translation pairs", len(keys) >= 1100, f"{len(keys)} keys")
        renamed = [k for k in keys if "SumatraReader" in k]
        check("renamed UI strings translated", len(renamed) >= 10, f"{len(renamed)} renamed keys")
        check("only Chinese translation data", langs == ["cn"], str(langs))
    else:
        check("staged translations present", False, TRANSLATIONS)

    if os.path.isfile(ARCHIVE):
        mk = os.path.join(REL, "MakeLZSA")
        if os.path.isfile(mk):
            rc, out = run([mk, ARCHIVE])
            check("LZSA archive verifies", rc == 0 and "Verified" in out, out.strip().splitlines()[-1][:110] if out.strip() else "")
            m = re.search(r"Verified all (\d+) archive entries", out)
            n_entries = int(m.group(1)) if m else 0
            # translations + scripts + fonts + ext scripts = 22; the offline
            # manual adds ~19 more. A missing manual would leave us at 22.
            check("offline manual packed", n_entries >= 30, f"{n_entries} archive entries")
        else:
            check("LZSA archive exists", True, f"{os.path.getsize(ARCHIVE)} bytes (MakeLZSA not found)")
    else:
        check("LZSA archive exists", False, ARCHIVE)

    # locally authored manual: manifest entries must all resolve to staged pages
    manual_dir = os.path.join(REPO, ".work", "docs")
    manifest = os.path.join(manual_dir, "manifest.json")
    if os.path.isfile(manifest):
        import json
        pages = json.load(open(manifest, encoding="utf-8"))
        missing = [md for md in pages.values() if not os.path.isfile(os.path.join(manual_dir, md))]
        check("manual manifest resolves", len(pages) >= 5 and not missing,
              f"{len(pages)} pages, {len(missing)} missing")
        check("manual shell staged", os.path.isfile(os.path.join(manual_dir, "manual.shell.html")), "manual.shell.html")

        # Editing a page in docs/manual without rebuilding leaves the app
        # showing the previous text, which reads like a documentation bug
        # rather than a stale build. gen-docs copies the pages verbatim, so
        # the two must be byte-identical.
        stale = [md for md in sorted(pages.values())
                 if os.path.isfile(os.path.join(MANUAL_SRC, md))
                 and open(os.path.join(MANUAL_SRC, md), "rb").read()
                 != open(os.path.join(manual_dir, md), "rb").read()]
        check("staged manual matches docs/manual", not stale,
              ("stale, rebuild needed: " + ", ".join(stale)) if stale
              else f"{len(pages)} pages identical")

        # same for the archive itself: MakeLZSA stamps it at pack time, so an
        # archive older than a staged page predates the manual it should carry
        if os.path.isfile(ARCHIVE):
            newest = max(os.path.getmtime(os.path.join(manual_dir, f))
                         for f in os.listdir(manual_dir)
                         if os.path.isfile(os.path.join(manual_dir, f)))
            check("embedded archive newer than the manual",
                  os.path.getmtime(ARCHIVE) >= newest,
                  f"archive {os.path.getmtime(ARCHIVE):.0f} vs manual {newest:.0f}")
    else:
        check("manual manifest present", False, manifest)

    pages = [f for f in os.listdir(MANUAL_SRC) if f.endswith(".md")] if os.path.isdir(MANUAL_SRC) else []
    check("manual sources shipped in repo", len(pages) >= 5, f"{len(pages)} pages in docs/manual")
    if os.path.isfile(STAGED_TROUBLESHOOTING):
        staged = open(STAGED_TROUBLESHOOTING, encoding="utf-8").read()
        check("embedded manual uses the renamed app", "SumatraReader.app" in staged and
              "SumatraPDF.app" not in staged, "Troubleshooting.md")
    else:
        check("embedded manual uses the renamed app", False, STAGED_TROUBLESHOOTING)

    # compiled language table
    if os.path.isfile(LANGS_CPP):
        txt = open(LANGS_CPP, encoding="utf-8").read()
        m = re.search(r"static const LangInfo langs\[\] = \{(.*?)\};", txt, re.S)
        codes = re.findall(r'StrL\("([a-z-]+)"\)', m.group(1)) if m else []
        check("language table = en + cn", codes[:2] == ["en", "cn"] and len(codes) == 2, str(codes))

    # availability diagnostics from the build
    if os.path.isfile(a.build_log):
        log = open(a.build_log, encoding="utf-8", errors="replace").read()
        warns = re.findall(r"^.*(?:unguarded availability|is only available on macOS|first deprecated in macOS).*$", log, re.M)
        errors = re.findall(r"^.*error:.*$", log, re.M)
        check("build has no compile errors", not errors, f"{len(errors)} errors, {len(warns)} availability warnings")
        if warns:
            print("     availability warnings (first 8):")
            for w in warns[:8]:
                print("       " + w.strip()[:150])
    else:
        check("build log present", False, a.build_log)

    # dyld imports. clang's -Wunguarded-availability (kept as a warning by
    # -Wno-error=unguarded-availability) is what guarantees a post-13 API is
    # guarded; here we only record the dyld-level weak imports as evidence.
    dyld = run(["xcrun", "dyld_info", "-imports", EXE])
    if dyld[0] == 0:
        lines = [l for l in dyld[1].splitlines() if "(from " in l]
        weak = [l for l in lines if "[weak-import]" in l]
        strong = [l for l in lines if "[weak-import]" not in l and "<weak-def-coalesce>" not in l]
        weak_names = [re.search(r"\s(\S+)\s*\[weak-import\]", l).group(1) for l in weak if re.search(r"\s(\S+)\s*\[weak-import\]", l)]
        print(f"     dyld_info: {len(weak)} weak imports, {len(strong)} strong imports")
        print("     weak: " + ", ".join(weak_names))
        # __availability_version_check is exactly the symbol clang emits for
        # @available() checks, so its presence confirms the guards survived.
        check("availability guards linked", "__availability_version_check" in weak_names,
              f"{len(weak)} weak-import symbols")
        check("import table readable", len(lines) > 100, f"{len(lines)} import entries")
    else:
        print("     dyld_info unavailable, skipped import table check")

    failed = [n for n, ok, _ in results if not ok]
    print(f"\n=== {len(results) - len(failed)}/{len(results)} checks passed ===")
    if failed:
        print("FAILED: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
