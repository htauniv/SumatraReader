// The version of an ng build is the day it was built: yy.mm.dd, plus .n for
// the n-th further build of that day. The build passes it to the compiler as
// defines; see src/ng/Version.h.
//
// House rule: a version string must never contain the digits 4 or 7,
// anywhere. Both the per-day revision and, if necessary, the calendar date
// are adjusted so this holds. The real build date and the git commit are
// still recorded verbatim in .work/build-history.txt.
//
// Version management: the .n suffix is normally picked automatically. The
// counter lives in <root>/.work/build-version.json and grows with every
// build of the same day, resetting on a new day. An explicit -rev <n>
// overrides the counter (and does not touch it). Every finished build
// appends one line to <root>/.work/build-history.txt.
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

/** Upper bound for the per-day revision. Candidates with a 4 or 7 are skipped. */
export const maxRev = 99;

const FORBIDDEN = /[47]/;

/** True when a string contains neither of the two disallowed digits. */
export function isCleanVersion(s: string): boolean {
  return !FORBIDDEN.test(s);
}

/**
 * Smallest integer >= n that contains neither 4 nor 7:
 * 1, 2, 3, 5, 6, 8, 9, 10, 11, 12, 13, 15, ...
 * Returns -1 if the allowed range is exhausted.
 */
export function cleanRev(n: number): number {
  for (let i = Math.max(1, n); i <= maxRev; i++) {
    if (isCleanVersion(String(i))) return i;
  }
  return -1;
}

export type NgVersion = {
  /** 26.11.09.5 */
  ver: string;
  /** 26,11,9,5: FILEVERSION of a Windows version resource */
  rc: string;
  /** "" outside a git checkout */
  gitSha1: string;
};

const root = join(import.meta.dir, "..", "..");

function gitSha1(): string {
  const r = Bun.spawnSync(["git", "rev-parse", "HEAD"], { cwd: root, stdout: "pipe", stderr: "pipe" });
  const s = r.exitCode === 0 ? r.stdout.toString().trim() : "";
  return /^[0-9a-f]{40}$/.test(s) ? s : "";
}

const pad2 = (n: number) => String(n).padStart(2, "0");

/**
 * Some calendar dates carry a disallowed digit themselves (the 14th, 17th,
 * 24th, 27th, April, July, or a year ending in 4 or 7). Shift just those
 * digits to the next allowed one, so a version is always valid. Two different
 * dates can then map to the same prefix (the 14th and 15th both become 15);
 * autoRev() resolves that by skipping revisions already in the build history.
 */
function cleanDatePart(yyyy: number, mm: number, dd: number): string {
  const raw = `${pad2(yyyy % 100)}.${pad2(mm)}.${pad2(dd)}`;
  return raw.replace(/[47]/g, (d) => (d === "4" ? "5" : "8"));
}

const memo = new Map<number, NgVersion>();

export function ngVersion(rev = 0): NgVersion {
  let res = memo.get(rev);
  if (res) return res;
  const now = new Date();
  let ver = cleanDatePart(now.getFullYear(), now.getMonth() + 1, now.getDate());
  if (rev > 0) {
    const r = cleanRev(rev);
    if (r < 0) throw new Error(`no usable revision left below ${maxRev}`);
    ver += `.${r}`;
  }
  if (!isCleanVersion(ver)) {
    throw new Error(`internal error: build version ${ver} contains a disallowed digit`);
  }
  // FILEVERSION wants four numeric fields, all free of the disallowed digits.
  const parts = ver.split(".");
  while (parts.length < 4) parts.push("0");
  res = {
    ver,
    rc: parts.slice(0, 4).join(","),
    gitSha1: gitSha1(),
  };
  memo.set(rev, res);
  return res;
}

/** defines for the one source file that turns the version into variables */
export function versionDefines(v: NgVersion): string[] {
  const res = [`SUMATRA_VER=${v.ver}`];
  if (v.gitSha1) res.push(`GIT_COMMIT_ID=${v.gitSha1}`);
  return res;
}

function todayStr(): string {
  const now = new Date();
  return `${pad2(now.getFullYear() % 100)}.${pad2(now.getMonth() + 1)}.${pad2(now.getDate())}`;
}

function versionStatePath(rootDir: string): string {
  return join(rootDir, ".work", "build-version.json");
}

function buildHistoryPath(rootDir: string): string {
  return join(rootDir, ".work", "build-history.txt");
}

/** Versions already handed out, so a shifted date can never reuse one. */
function usedVersions(rootDir: string): Set<string> {
  const path = buildHistoryPath(rootDir);
  if (!existsSync(path)) return new Set();
  const used = new Set<string>();
  for (const line of readFileSync(path, "utf8").split("\n")) {
    const m = line.match(/\s(\d+(?:\.\d+)+)\s/);
    if (m) used.add(m[1]);
  }
  return used;
}

/**
 * The rev for this build when the caller did not pass -rev: the persisted
 * per-day counter plus one, skipping revisions that would put a 4 or 7 in
 * the version, and skipping any version already in the build history.
 * Reads and rewrites .work/build-version.json.
 */
export function autoRev(rootDir: string): number {
  const today = todayStr();
  const path = versionStatePath(rootDir);
  let prev = 0;
  if (existsSync(path)) {
    try {
      const s: { date?: string; rev?: number } = JSON.parse(readFileSync(path, "utf8"));
      if (s.date === today && Number.isInteger(s.rev) && s.rev! >= 0) {
        prev = s.rev!;
      }
      // a stale (other-day) state restarts at 1
    } catch {
      // unreadable state starts over
    }
  }
  const used = usedVersions(rootDir);
  let rev = cleanRev(prev + 1);
  while (rev > 0 && used.has(ngVersion(rev).ver)) {
    rev = cleanRev(rev + 1);
  }
  if (rev < 0) throw new Error(`no usable revision left below ${maxRev} for ${today}`);
  mkdirSync(join(rootDir, ".work"), { recursive: true });
  writeFileSync(path, `${JSON.stringify({ date: today, rev }, null, 2)}\n`);
  return rev;
}

/** Append one line to .work/build-history.txt after a successful build. */
export function recordBuild(rootDir: string, ver: string, detail: string): void {
  const sha = gitSha1();
  const git = sha ? sha.slice(0, 8) : "nogit";
  const time = new Date().toISOString().replace("T", " ").slice(0, 19);
  const dir = join(rootDir, ".work");
  mkdirSync(dir, { recursive: true });
  const line = `${time}  ${ver}  ${git}  ${detail}\n`;
  writeFileSync(buildHistoryPath(rootDir), line, { flag: "a" });
}
