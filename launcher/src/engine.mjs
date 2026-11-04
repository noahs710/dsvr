// DSVR launcher engine bridge.
// The ONLY thing this file does is run the co-located DSVR-Setup.exe with a
// fixed argv array chosen from a strict allow-list: the renderer can never
// compose arbitrary commands. All install/download/hash logic stays
// in the C++ engine; this module just spawns, waits and parses.
// Split from main.js so the same code is smoke-testable under plain node
// (npm run smoke) without opening an Electron window.
import { execFile } from "node:child_process";
import { createHash } from "node:crypto";
import { readFileSync, existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));

// verb -> exact argv. Nothing else can ever be spawned.
export const VERBS = {
  status: ["--json", "status"],
  doctor: ["--json", "doctor"],
  verify: ["--json", "verify"],
  "settings-index": ["--json", "settings-index"],
  // Mutating verbs reuse the frozen human CLI with unattended --yes; the
  // engine's own ConsoleConfirm(false) refuses destructive prompts there.
  install: ["--json", "install"],
  "install-vr": ["--json", "install-vr"],
  uninstall: ["--json", "uninstall"],
  "uninstall-remove-settings": ["--json", "uninstall", "--remove-settings"],
  "fpsunlock-install": ["--json", "fpsunlock-install"],
  "tpup-download": ["--json", "tpup-download"],
  "safe-defaults": ["--json", "safe-defaults"],
  "preset-index": ["--json", "preset-index"],
  "preset-comfort": ["--json", "preset-comfort"],
  "preset-balanced": ["--json", "preset-balanced"],
  "preset-sharper": ["--json", "preset-sharper"],
  "preset-match": ["--json", "preset-match"],
  // Legacy ids from the old five-preset set stay valid engine-side.
  "preset-perf": ["--json", "preset-perf"],
  "preset-veteran": ["--json", "preset-veteran"],
  "preset-smooth": ["--json", "preset-smooth"],
  "preset-res": ["--json", "preset-res"],
  "preset-comfort": ["--json", "preset-comfort"],
  "preset-perf": ["--json", "preset-perf"],
  "preset-veteran": ["--json", "preset-veteran"],
  "preset-smooth": ["--json", "preset-smooth"],
  "preset-res": ["--json", "preset-res"],
  // Phase 3: the engine verbs are live in the C++ seam. config-set takes a
  // payload of "key=value" strings (validated below, never arbitrary argv).
  "config-get": ["--json", "config-get"],
  "config-set": ["--json", "config-set"],
  launch: ["--json", "launch"],
  "launch-native": ["--json", "launch-native"],
};

const JSON_VERBS = new Set([
  "status",
  "doctor",
  "verify",
  "settings-index",
  "config-get",
  "config-set",
  "launch",
  "launch-native",
  "install",
  "install-vr",
  "uninstall",
  "uninstall-remove-settings",
  "fpsunlock-install",
  "tpup-download",
  "safe-defaults",
  "preset-index",
  "preset-comfort",
  "preset-balanced",
  "preset-sharper",
  "preset-match",
  "preset-perf",
  "preset-veteran",
  "preset-smooth",
  "preset-res",
  "preset-comfort",
  "preset-perf",
  "preset-veteran",
  "preset-smooth",
  "preset-res",
]);

// Payload keys are exactly the settings-index/DSVR.toml spellings; values are
// opaque to this layer (the engine validates them). No '=' inside a value's
// key position, no control characters: one payload string = one KEY=VALUE arg.
const PAYLOAD_KEY = /^[a-z][a-z0-9_]*=[^\0\r\n]*$/;
function payloadFor(verb, payload) {
  if (verb !== "config-set") return payload ? { error: "payload_not_allowed" } : null;
  if (!Array.isArray(payload) || payload.length === 0)
    return { error: "payload_required" };
  if (payload.length > 256 || !payload.every((p) => typeof p === "string" &&
      p.length <= 4096 && PAYLOAD_KEY.test(p)))
    return { error: "payload_not_allowed" };
  return { argv: payload };
}

// Locate the Setup exe: co-located first (packaged layout), then the dev
// build output. No PATH search.
export function resolveSetupExe({ appDir = path.join(here, "..") } = {}) {
  const colocated = path.join(appDir, "DSVR-Setup.exe");
  if (existsSync(colocated)) return { exe: colocated, dev: false };
  const dev = path.resolve(
    appDir,
    "..",
    "build",
    "src",
    "setup",
    "Release",
    "DSVR-Setup.exe",
  );
  if (existsSync(dev)) return { exe: dev, dev: true };
  return { exe: null, dev: false };
}

// Verify the complete native install payload against the adjacent manifest.
// Setup copies both DLLs from its own directory during installation.
export function verifyEngineResources(exe) {
  const manifest = path.join(path.dirname(exe), "DSVR-SHA256.txt");
  if (!existsSync(manifest)) return { ok: false, reason: "no_manifest" };
  const entries = new Map();
  for (const line of readFileSync(manifest, "utf8").split(/\r?\n/)) {
    const m = line.match(/^([0-9a-f]{64})\s+(\S+)$/);
    if (m) entries.set(path.posix.basename(m[2]), m[1]);
  }
  for (const name of ["dinput8.dll", "DSVR.dll", "DSVR-Setup.exe"]) {
    const expected = entries.get(name);
    if (!expected) return { ok: false, reason: `no_entry_${name}` };
    const file = path.join(path.dirname(exe), name);
    if (!existsSync(file)) return { ok: false, reason: `missing_${name}` };
    const hash = createHash("sha256");
    hash.update(readFileSync(file));
    if (hash.digest("hex") !== expected)
      return { ok: false, reason: `mismatch_${name}` };
  }
  return { ok: true };
}

// Run one allow-listed verb. Returns {ok, json?, log, exitCode}.
export function callEngine(verb, exe, payload, gamePath) {
  const argv = VERBS[verb];
  if (!argv) return Promise.resolve({ ok: false, error: "verb_not_allowed" });
  if (!exe) return Promise.resolve({ ok: false, error: "setup_exe_missing" });
  const extra = payloadFor(verb, payload);
  if (extra && extra.error) return Promise.resolve({ ok: false, error: extra.error });
  if (gamePath && (typeof gamePath !== "string" || gamePath.length > 32760 ||
      !path.win32.isAbsolute(gamePath) || /[\0\r\n]/.test(gamePath)))
    return Promise.resolve({ ok: false, error: "game_path_invalid" });
  const fullArgv = [
    ...argv,
    ...(extra ? extra.argv : []),
    ...(gamePath ? ["--game", gamePath] : []),
  ];
  return new Promise((resolve) => {
    execFile(
      exe,
      fullArgv,
      { windowsHide: true, maxBuffer: 8 * 1024 * 1024 },
      (err, stdout, stderr) => {
        const exitCode = err ? (err.code ?? 1) : 0;
        const log = (stdout + stderr).split(/\r?\n/).filter(Boolean);
        if (JSON_VERBS.has(verb)) {
          try {
            const doc = JSON.parse(stdout.trim());
            resolve({ ok: !err, json: doc, log, exitCode });
          } catch {
            resolve({ ok: false, error: "bad_json", log, exitCode });
          }
        } else {
          resolve({ ok: !err, log, exitCode });
        }
      },
    );
  });
}
