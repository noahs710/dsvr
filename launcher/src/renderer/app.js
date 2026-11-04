const logEl = document.getElementById("log");
const engineState = document.getElementById("engine-state");
const launchButton = document.getElementById("launch");
const gamePathInput = document.getElementById("game-path");
const gameResult = document.getElementById("game-result");
let gamePath = "";
let engineReady = false;
let settingsLoaded = false;
let settingsControls = new Map();
let installState = null;

const enumChoices = {
  dominant_hand: ["right", "left"],
  movement_orientation: ["head", "controller"],
  movement_controller: ["non_dominant", "left", "right"],
  play_style: ["standing", "seated", "roomscale"],
  embodiment_fit: ["head", "arm"],
  aa_mode: ["0", "1"],
};
const enumLabels = { aa_mode: { "0": "Off", "1": "FXAA" } };
const settingHelp = {
  aa_mode: "FXAA processes the full eye image. Native MSAA 2×/4× and TAA need scene render inputs DSVR does not receive yet.",
  cas_enabled: "Runs after the game image and VR menu are composed, including text and room details.",
  cas_strength: "Start low to keep fine text clear and avoid bright edge halos.",
};

const commonGroups = ["Core", "Comfort", "Graphics", "Locomotion"];
const groupDescriptions = {
  Core: "Choose how DSVR starts and which hand leads.",
  Comfort: "Reduce visual strain and choose your turning style.",
  Graphics: "Tune image quality, display mode, and menu presentation.",
  Locomotion: "Set movement direction and interaction movement controls.",
  "Body and calibration": "Adjust tracked body fit and avatar presentation.",
  "Combat and haptics": "Tune weapon, guard, and controller feedback behavior.",
  "Input and bindings": "Choose controller profiles and shortcuts.",
  "Diagnostics and safety": "Review logging, offline, and developer controls.",
  "Other settings": "Additional controls supplied by the setup engine.",
};
const categoryByKey = new Map();
const addKeys = (group, keys) => keys.forEach((key) => categoryByKey.set(key, group));
addKeys("Core", ["vr_enabled", "headlook_enabled", "dominant_hand", "play_style", "xr_input_enabled"]);
addKeys("Comfort", ["comfort_vignette", "vignette_strength", "fov_scale", "snap_turn", "snap_turn_degrees", "smooth_turn", "smooth_turn_speed_dps"]);
addKeys("Graphics", ["render_scale_percent", "desktop_present", "aa_mode", "cas_enabled", "cas_strength", "force_disable_motion_blur", "reshade_host", "gameplay_theater", "mono_screen_fov_deg", "theater_override", "theater_floor_grid", "menu_distance_m", "menu_reading_fov_deg", "stereo_enabled", "afr_enabled", "hide_ui_in_vr"]);
addKeys("Locomotion", ["movement_orientation", "movement_controller", "arm_swing_sprint", "arm_swing_stride_min", "arm_swing_stride_max", "arm_swing_quiet_max", "arm_swing_alternations", "arm_swing_hold_ms", "reach_interact", "reach_forward_min_m", "reach_forward_max_m", "reach_speed_min", "reach_dominance_min", "reach_quiet_max", "reach_settle_ms", "reach_press_ms", "reach_cooldown_ms"]);
addKeys("Body and calibration", ["eye_height_override", "eye_height", "seated_height_offset_m", "world_scale", "avatar_scale", "forward_offset_m", "eye_clearance_m", "hide_player_model", "hide_head", "hide_torso_and_legs", "forearm_only", "hide_second_model", "embodiment_fit", "arm_ik_enabled", "arm_ik_log_only", "arm_ik_block_animation", "grip_roll_correction_deg"]);
addKeys("Input and bindings", ["hotkeys_enabled", "bind_profile_quest3", "bind_profile_index", "bind_profile_wmr", "bind_profile_simple", "binding_live_reattach", "log_marker_vk", "config_reload_vk"]);

const actionCopy = {
  install: ["Update or reinstall DSVR", "Verifying the game and preparing an in-place update.", "DSVR setup finished."],
  "install-vr": ["Update or reinstall DSVR", "Verifying the game and preparing an in-place update with VR enabled.", "DSVR setup finished with VR enabled."],
  uninstall: ["Uninstall DSVR", "Removing DSVR files while preserving settings.", "DSVR was removed. Settings were kept."],
  "uninstall-remove-settings": ["Uninstall DSVR", "Removing DSVR files and its settings.", "DSVR and its settings were removed."],
  verify: ["Verify game build", "Checking the installed game against supported builds.", "Game build check finished."],
  doctor: ["Run doctor", "Inspecting setup, runtime, and installation health.", "Doctor check finished."],
  "fpsunlock-install": ["Install FPS Unlock", "Downloading and verifying the optional FPS Unlock mod.", "FPS Unlock setup finished."],
  "tpup-download": ["Download texture tools", "Downloading the tool package to your Downloads folder.", "Texture tools download finished."],
  "firewall-block": ["Block game network", "Adding the reversible DSVR firewall rules.", "Firewall block action finished."],
  "firewall-unblock": ["Remove network block", "Removing the DSVR firewall rules.", "Firewall unblock action finished."],
  "firewall-status": ["Check network block", "Checking the current DSVR firewall rules.", "Firewall status check finished."],
};

function logLine(message) {
  logEl.textContent += `${message}\n`;
  logEl.scrollTop = logEl.scrollHeight;
}

function actionReport(result) {
  if (Array.isArray(result.json?.log)) return result.json.log.join("\n");
  if (Array.isArray(result.json?.checks)) {
    return result.json.checks.map((check) => {
      if (typeof check === "string") return check;
      const state = check.ok ? "OK" : "CHECK";
      return `${state}  ${check.name ?? check.label ?? "Setup check"}${check.detail ? ` — ${check.detail}` : ""}`;
    }).join("\n");
  }
  if (result.json?.sha256) {
    return `Supported build: ${result.json.supported ? "yes" : "no"}\nSHA-256: ${result.json.sha256}`;
  }
  if (result.json) return JSON.stringify(result.json, null, 2);
  return (result.log ?? []).filter((line) => !line.trim().startsWith("{")).join("\n") || result.error || "No setup details were returned.";
}

async function runVerb(verb, payload) {
  logLine(`› ${verb}`);
  const result = await window.dsvr.call(verb, payload);
  for (const line of result.json?.log ?? []) logLine(line);
  if (result.json?.error) logLine(`Error: ${result.json.error}`);
  if (result.error) logLine(`Error: ${result.error}`);
  if (!result.ok && !result.json?.error) logLine(`Action failed (exit ${result.exitCode ?? "?"})`);
  return result;
}

function setOperation(verb, result, working = false) {
  const [title, detail, success] = actionCopy[verb] ?? [verb, "Running setup action.", "Setup action finished."];
  const ok = result ? result.ok && result.json?.ok !== false : false;
  const panel = document.createElement("section");
  panel.className = `operation-result ${working ? "running" : ok ? "complete" : "failed"}`;
  const heading = document.createElement("h2");
  heading.textContent = working ? `Working · ${title}` : ok ? title : `${title} needs attention`;
  const message = document.createElement("p");
  message.textContent = working
    ? `${detail} Setup may open a Windows confirmation for offline mode, a chained mod, or firewall safety.`
    : ok ? success : `Setup stopped with exit ${result?.exitCode ?? "?"}. Review the details before trying again.`;
  panel.append(heading, message);
  if (!working) {
    const details = document.createElement("details");
    const summary = document.createElement("summary");
    summary.textContent = "Show setup details";
    const output = document.createElement("pre");
    output.textContent = actionReport(result);
    details.append(summary, output);
    panel.appendChild(details);
  }
  gameResult.replaceChildren(panel);
}

function installVerb() {
  return document.getElementById("enable-vr-after").checked ? "install-vr" : "install";
}

async function runGameAction(requestedVerb) {
  if (!engineReady) return;
  let verb = requestedVerb;
  if (requestedVerb === "install") verb = installVerb();
  if (requestedVerb === "uninstall") {
    const removesSettings = document.getElementById("remove-settings").checked;
    const settingsCopy = removesSettings
      ? "DSVR files and DSVR.toml settings will be removed."
      : "DSVR files will be removed; DSVR.toml settings will stay.";
    if (!window.confirm(`Uninstall DSVR?\n\n${settingsCopy}\n\nThe game must be closed. Any preserved chained mod can be restored by the setup prompt.`)) return;
    if (removesSettings) verb = "uninstall-remove-settings";
  }
  const buttons = [...document.querySelectorAll("#page-game button")];
  for (const button of buttons) button.disabled = true;
  setOperation(verb, null, true);
  const result = await runVerb(verb);
  setOperation(verb, result);
  for (const button of buttons) button.disabled = false;
  await refreshStatus();
}

for (const button of document.querySelectorAll("#page-game button[data-verb]"))
  button.addEventListener("click", () => runGameAction(button.dataset.verb));
document.getElementById("install-action").addEventListener("click", () => runGameAction("install"));
document.getElementById("uninstall-action").addEventListener("click", () => runGameAction("uninstall"));

async function refreshStatus() {
  const card = document.getElementById("status-card");
  const result = await window.dsvr.call("status");
  if (!result.ok || !result.json?.install_state) {
    card.replaceChildren();
    const error = document.createElement("div");
    error.className = "card fail";
    error.textContent = `Game status unavailable: ${result.json?.error ?? result.error ?? "unknown"}`;
    card.appendChild(error);
    launchButton.disabled = true;
    document.getElementById("install-action").disabled = true;
    document.getElementById("uninstall-action").disabled = true;
    return;
  }
  const status = result.json;
  installState = status;
  const body = document.createElement("div");
  body.className = "card status-card";
  const heading = document.createElement("h2");
  heading.append(document.createTextNode("Dark Souls Remastered"));
  const version = document.createElement("span");
  version.className = "status-version";
  version.textContent = status.version ?? "DSVR";
  heading.appendChild(version);
  body.appendChild(heading);
  const pathText = document.createElement("p");
  pathText.className = "hint path-text";
  pathText.textContent = status.exe ?? "Game folder not found";
  if (!gamePath && typeof status.exe === "string") {
    gamePathInput.value = status.exe.replace(/[\\/][^\\/]+$/, "");
  }
  body.appendChild(pathText);
  const installed = status.install_state.installed;
  const partial = status.install_state.partial;
  const values = [
    ["DSVR", installed ? "Installed" : partial ? "Needs repair" : "Not installed"],
    ["FPS Unlock", status.fps_unlock_state.installed ? "Installed" : "Optional"],
    ["Game", status.game_running ? "Running" : "Closed"],
    ["Headset refresh", status.headset_hz ? `${status.headset_hz} Hz` : "Not reported"],
  ];
  const grid = document.createElement("div");
  grid.className = "grid";
  for (const [label, value] of values) {
    const cell = document.createElement("div");
    cell.className = "status-value";
    const key = document.createElement("span");
    key.textContent = label;
    const val = document.createElement("strong");
    val.textContent = value;
    cell.append(key, val);
    grid.appendChild(cell);
  }
  body.appendChild(grid);
  card.replaceChildren(body);

  const installAction = document.getElementById("install-action");
  installAction.textContent = partial ? "Repair DSVR install" : installed ? "Update / reinstall DSVR" : "Install DSVR";
  installAction.disabled = !engineReady || status.game_running;
  document.getElementById("uninstall-action").disabled = !engineReady || status.game_running || (!installed && !partial);
  launchButton.disabled = !engineReady || status.game_running || !installed;
  document.querySelector('[data-verb="fpsunlock-install"]').disabled =
    !engineReady || status.game_running || status.fps_unlock_state.installed;
  for (const button of document.querySelectorAll('#page-game button[data-verb="verify"], #page-game button[data-verb="doctor"], #page-game button[data-verb="firewall-status"]'))
    button.disabled = !engineReady;
  for (const button of document.querySelectorAll('#page-game button[data-verb="tpup-download"], #page-game button[data-verb="firewall-block"], #page-game button[data-verb="firewall-unblock"]'))
    button.disabled = !engineReady;
  document.getElementById("install-hint").textContent = installed
    ? "The same action updates or reinstalls the files in place. Existing settings stay in DSVR.toml."
    : partial ? "Setup found an incomplete install. Repair replaces only the DSVR-owned files."
      : "Choose whether this installation should start with VR enabled.";
}

document.getElementById("choose-game").addEventListener("click", async () => {
  const result = await window.dsvr.chooseGame();
  if (!result.ok) return;
  gamePath = result.path;
  gamePathInput.value = gamePath;
  logLine(gamePath ? `Game folder selected: ${gamePath}` : "Game folder unchanged");
  await refreshStatus();
  settingsLoaded = false;
  await loadSettings();
});

launchButton.addEventListener("click", async () => {
  launchButton.disabled = true;
  const result = await runVerb("launch");
  if (result.json?.ok) logLine(`Game started through Steam (process ${result.json.pid}).`);
  else logLine("Game did not start. Review the status and setup details.");
  await refreshStatus();
});

function categoryFor(row) {
  const explicit = categoryByKey.get(row.key);
  if (explicit) return explicit;
  if (row.group === "Combat and haptics") return "Combat and haptics";
  if (row.group === "Diagnostics and safety") return "Diagnostics and safety";
  if (row.group === "Camera and body") return "Body and calibration";
  if (row.group === "Controls and movement") return "Input and bindings";
  return "Other settings";
}

function makeSettingGroup(name, rows, initiallyOpen) {
  const group = document.createElement("details");
  group.className = "setting-group";
  group.open = initiallyOpen;
  group.dataset.group = name;
  const summary = document.createElement("summary");
  const title = document.createElement("strong");
  title.textContent = name;
  const description = document.createElement("small");
  description.textContent = groupDescriptions[name] ?? "Additional controls.";
  const count = document.createElement("span");
  count.className = "count";
  count.textContent = `${rows.length} settings`;
  summary.append(title, description, count);
  const list = document.createElement("div");
  list.className = "setting-rows";
  for (const row of rows) {
    const label = document.createElement("label");
    label.className = "setting";
    label.dataset.search = `${row.label} ${row.key} ${row.group}`.toLowerCase();
    const nameBox = document.createElement("span");
    nameBox.className = "setting-name";
    const nameText = document.createElement("strong");
    nameText.textContent = row.label;
    const source = document.createElement("small");
    source.textContent = `${row.key} · ${row.group}`;
    nameBox.append(nameText, source);
    if (settingHelp[row.key]) {
      const help = document.createElement("small");
      help.textContent = settingHelp[row.key];
      nameBox.appendChild(help);
    }
    const value = settingsControls.get(row.key).baseline;
    let input;
    let kind;
    if (typeof value === "boolean") {
      input = document.createElement("input");
      input.type = "checkbox";
      input.checked = value;
      kind = "bool";
    } else if (enumChoices[row.key]) {
      input = document.createElement("select");
      for (const choice of enumChoices[row.key]) {
        const option = document.createElement("option");
        option.value = choice;
        option.textContent = enumLabels[row.key]?.[choice] ?? choice.replaceAll("_", " ");
        input.appendChild(option);
      }
      input.value = String(value);
      kind = "text";
    } else {
      input = document.createElement("input");
      input.type = typeof value === "number" ? "number" : "text";
      if (input.type === "number") input.step = "any";
      input.value = String(value);
      kind = typeof value === "number" ? "number" : "text";
    }
    input.className = "setting-control";
    input.id = `setting-${row.key}`;
    if (row.key === "dev_mode") {
      input.checked = Boolean(value);
      input.disabled = true;
      input.title = "Use the developer mode control below or press Ctrl+D";
    }
    label.append(nameBox, input);
    list.appendChild(label);
    settingsControls.set(row.key, { kind, input, saved: value, baseline: value });
  }
  group.append(summary, list);
  return group;
}

async function loadSettings() {
  const list = document.getElementById("settings-list");
  list.replaceChildren();
  settingsControls = new Map();
  const [index, config] = await Promise.all([
    window.dsvr.call("settings-index"),
    window.dsvr.call("config-get"),
  ]);
  if (!index.ok || !index.json?.rows || !config.ok || !config.json?.settings) {
    const message = document.createElement("p");
    message.className = "hint";
    message.textContent = "Choose a valid game folder to load settings.";
    list.appendChild(message);
    settingsLoaded = false;
    return;
  }
  const baseline = config.json.settings;
  const rows = index.json.rows.filter((row) => row.key in baseline);
  for (const row of rows) settingsControls.set(row.key, { baseline: baseline[row.key] });
  const grouped = new Map();
  for (const row of rows) {
    const category = categoryFor(row);
    if (!grouped.has(category)) grouped.set(category, []);
    grouped.get(category).push(row);
  }
  for (const category of commonGroups) {
    const categoryRows = grouped.get(category);
    if (categoryRows?.length) list.appendChild(makeSettingGroup(category, categoryRows, category === "Core" || category === "Comfort"));
  }
  const otherCategories = [...grouped.keys()].filter((name) => !commonGroups.includes(name));
  if (otherCategories.length) {
    const more = document.createElement("details");
    more.id = "more-controls";
    const summary = document.createElement("summary");
    const title = document.createElement("strong");
    title.textContent = "More controls";
    const count = document.createElement("span");
    count.className = "count";
    count.textContent = `${otherCategories.reduce((sum, name) => sum + grouped.get(name).length, 0)} settings`;
    summary.append(title, count);
    more.appendChild(summary);
    const order = ["Body and calibration", "Combat and haptics", "Input and bindings", "Diagnostics and safety", "Other settings"];
    for (const category of order) {
      const categoryRows = grouped.get(category);
      if (categoryRows?.length) more.appendChild(makeSettingGroup(category, categoryRows, false));
    }
    list.appendChild(more);
  }
  const actions = document.createElement("div");
  actions.className = "setting-save";
  const save = document.createElement("button");
  save.textContent = "Save settings";
  save.disabled = true;
  const status = document.createElement("span");
  status.className = "hint";
  actions.append(save, status);
  list.appendChild(actions);
  const current = (control) => control.kind === "bool" ? control.input.checked : control.input.value.trim();
  const dirty = () => [...settingsControls.entries()].filter(([, control]) =>
    control.kind && String(current(control)) !== String(control.saved));
  const update = () => {
    const count = dirty().length;
    save.disabled = count === 0;
    status.textContent = count ? `${count} unsaved change${count === 1 ? "" : "s"}` : "";
  };
  for (const control of settingsControls.values())
    if (control.kind) control.input.addEventListener("input", update);
  save.addEventListener("click", async () => {
    const changed = dirty();
    if (!changed.length) return;
    save.disabled = true;
    const payload = changed.map(([key, control]) => `${key}=${current(control)}`);
    const result = await runVerb("config-set", payload);
    if (result.ok && result.json?.ok !== false) {
      for (const control of settingsControls.values())
        if (control.kind) control.saved = current(control);
      status.textContent = "Saved. Press F7 in game to reload settings.";
      logLine(`Saved ${payload.length} setting${payload.length === 1 ? "" : "s"}.`);
    } else {
      status.textContent = `Settings were not saved: ${result.json?.error ?? result.error ?? "check the values"}`;
    }
    update();
  });
  document.getElementById("settings-count").textContent = `${rows.length} settings`;
  settingsLoaded = true;
}

document.getElementById("settings-search").addEventListener("input", (event) => {
  const query = event.currentTarget.value.trim().toLowerCase();
  for (const group of document.querySelectorAll(".setting-group")) {
    let matches = 0;
    for (const row of group.querySelectorAll(".setting")) {
      const visible = !query || row.dataset.search.includes(query);
      row.hidden = !visible;
      if (visible) matches += 1;
    }
    group.hidden = matches === 0;
    if (query && matches) {
      group.open = true;
      const parent = group.closest("#more-controls");
      if (parent) parent.open = true;
    }
  }
});

document.getElementById("dev-toggle").addEventListener("click", async () => {
  if (!engineReady) return;
  const config = await window.dsvr.call("config-get");
  if (!config.ok || typeof config.json?.settings?.dev_mode !== "boolean") return;
  const saved = await runVerb("config-set", [`dev_mode=${!config.json.settings.dev_mode}`]);
  if (!saved.ok || saved.json?.ok === false) logLine(saved.json?.error ?? "Could not save developer mode.");
  settingsLoaded = false;
  await loadSettings();
});

document.addEventListener("keydown", async (event) => {
  if (event.ctrlKey && event.key.toLowerCase() === "d" && engineReady) {
    event.preventDefault();
    document.getElementById("dev-toggle").click();
  }
});

for (const button of document.querySelectorAll("[data-preset]"))
  button.addEventListener("click", async () => {
    if (!engineReady || !window.confirm(`Apply the ${button.textContent} preset and save it to DSVR.toml?`)) return;
    const result = await runVerb(`preset-${button.dataset.preset}`);
    if (result.ok && result.json?.ok !== false) {
      logLine("Preset saved. Press F7 in game to reload settings.");
      settingsLoaded = false;
      await loadSettings();
    }
  });

document.getElementById("safe-defaults").addEventListener("click", async () => {
  if (!engineReady || !window.confirm("Replace all DSVR settings with factory safe defaults?")) return;
  const result = await runVerb("safe-defaults");
  if (result.ok && result.json?.ok !== false) {
    logLine("Safe defaults saved. Press F7 in game to reload settings.");
    settingsLoaded = false;
    await loadSettings();
  }
});

document.getElementById("clear-log").addEventListener("click", () => { logEl.textContent = ""; });
document.getElementById("nav").addEventListener("click", async (event) => {
  const button = event.target.closest("button[data-page]");
  if (!button) return;
  for (const item of document.querySelectorAll("#nav button")) item.classList.toggle("active", item === button);
  for (const page of document.querySelectorAll(".page")) page.classList.toggle("active", page.id === `page-${button.dataset.page}`);
  if (button.dataset.page === "settings" && !settingsLoaded) await loadSettings();
});

document.getElementById("choose-game").disabled = !engineReady;
for (const button of document.querySelectorAll("#page-game button")) button.disabled = !engineReady;
for (const button of document.querySelectorAll("[data-preset]")) button.disabled = !engineReady;
document.getElementById("safe-defaults").disabled = !engineReady;
document.getElementById("dev-toggle").hidden = !engineReady;
const startup = await window.dsvr.engineStatus();
engineReady = startup.ok;
engineState.textContent = startup.ok
  ? startup.dev ? "Development engine" : "Engine verified"
  : `Engine unavailable: ${startup.error}`;
engineState.classList.toggle("error", !startup.ok);
gamePath = (await window.dsvr.gamePath()) || "";
gamePathInput.value = gamePath;
document.getElementById("choose-game").disabled = !engineReady;
for (const button of document.querySelectorAll("#page-game button")) button.disabled = !engineReady;
for (const button of document.querySelectorAll("[data-preset]")) button.disabled = !engineReady;
document.getElementById("safe-defaults").disabled = !engineReady;
document.getElementById("dev-toggle").hidden = !engineReady;
await refreshStatus();
