// Electron main (docs/ELECTRON_LAUNCHER_PLAN.md phase 2). Deliberately dumb:
// locate the co-located DSVR-Setup.exe, hash-verify it, and answer renderer
// IPC by verb name only. All logic lives in the C++ engine (engine.mjs just
// spawns the allow-listed argv). --smoke runs the seam headless and exits.
import { app, BrowserWindow, dialog, ipcMain, shell } from "electron";
import path from "node:path";
import { readFileSync, renameSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { callEngine, resolveSetupExe, verifyEngineResources } from "./engine.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));

const engine = resolveSetupExe({
  appDir: app.isPackaged ? process.resourcesPath : path.join(here, ".."),
});
let engineStatus = { ok: true, dev: engine.dev };
if (!engine.exe) {
  engineStatus = { ok: false, error: "setup_exe_missing" };
} else {
  const hash = verifyEngineResources(engine.exe);
  if (!hash.ok && !(engine.dev && hash.reason === "no_manifest")) {
    engineStatus = { ok: false, error: "setup_hash_" + hash.reason };
  }
}

const stateFile = path.join(app.getPath("userData"), "launcher.json");
let gamePath = "";
try {
  const saved = JSON.parse(readFileSync(stateFile, "utf8"));
  if (typeof saved.gamePath === "string" && path.win32.isAbsolute(saved.gamePath))
    gamePath = saved.gamePath;
} catch {}

function saveGamePath(value) {
  const parent = path.dirname(stateFile);
  const temporary = stateFile + ".tmp";
  writeFileSync(temporary, JSON.stringify({ gamePath: value }), "utf8");
  renameSync(temporary, stateFile);
}

ipcMain.handle("engine:status", () => ({ ...engineStatus, exe: engine.exe }));
ipcMain.handle("engine:call", (_event, verb, payload) => {
  if (!engineStatus.ok) return { ok: false, error: engineStatus.error };
  if (typeof verb !== "string") return { ok: false, error: "verb_not_allowed" };
  return callEngine(verb, engine.exe, payload, gamePath);
});
ipcMain.handle("game:path", () => gamePath);
ipcMain.handle("game:choose", async () => {
  const picked = await dialog.showOpenDialog({
    title: "Choose the DARK SOULS REMASTERED game folder",
    properties: ["openDirectory", "dontAddToRecent"],
  });
  if (picked.canceled || !picked.filePaths[0]) return { ok: true, path: gamePath };
  gamePath = picked.filePaths[0];
  saveGamePath(gamePath);
  return { ok: true, path: gamePath };
});

// Same discipline as the argv allow-list: the renderer gets named links only.
const LINKS = {
  readme: () =>
    engine.exe
      ? path.join(path.dirname(engine.exe), "README.md")
      : null,
};
ipcMain.handle("engine:open", (_event, link) => {
  const target = LINKS[link] ? LINKS[link]() : null;
  if (!target) return { ok: false, error: "link_not_allowed" };
  shell.openPath(target);
  return { ok: true };
});

async function runSmoke() {
  console.log(`engine exe: ${engine.exe} (dev=${engine.dev})`);
  console.log(`engine status: ${JSON.stringify(engineStatus)}`);
  if (!engineStatus.ok) return app.exit(2);
  let failures = 0;
  for (const verb of ["status", "verify", "doctor", "settings-index", "config-get"]) {
    const r = await callEngine(verb, engine.exe, undefined, gamePath);
    const brief = r.json
      ? JSON.stringify(r.json).slice(0, 160)
      : (r.log[0] || "").slice(0, 160);
    console.log(`${verb}: exit=${r.exitCode} ok=${r.ok} ${brief}`);
    if (!r.ok || !r.json) failures++;
  }
  const bogus = await callEngine("rm -rf", engine.exe);
  console.log(`bogus verb: ${JSON.stringify(bogus)}`);
  if (bogus.error !== "verb_not_allowed") failures++;
  const bogusPayload = await callEngine("config-set", engine.exe, ["rm=rf", 42]);
  console.log(`bogus payload: ${JSON.stringify(bogusPayload)}`);
  if (bogusPayload.error !== "payload_not_allowed") failures++;
  app.exit(failures ? 1 : 0);
}

if (process.argv.includes("--smoke")) {
  app.whenReady().then(runSmoke);
} else {
  app.whenReady().then(() => {
    const win = new BrowserWindow({
      width: 860,
      height: 640,
      minWidth: 760,
      minHeight: 540,
      title: "DSVR Launcher",
      backgroundColor: "#11130f",
      ...(process.platform === "win32"
        ? {
            titleBarStyle: "hidden",
            titleBarOverlay: {
              color: "#11130f",
              symbolColor: "#dfd3b8",
              height: 32,
            },
          }
        : {}),
      webPreferences: {
        preload: path.join(here, "preload.cjs"),
        contextIsolation: true,
        nodeIntegration: false,
      },
    });
    win.setMenuBarVisibility(false);
    win.loadFile(path.join(here, "renderer", "index.html"));
  });
  app.on("window-all-closed", () => app.quit());
}
