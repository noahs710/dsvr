// The renderer sees exactly three verbs-in/JSON-out channels; it can never
// reach Node, Electron, or an arbitrary command line.
const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("dsvr", {
  engineStatus: () => ipcRenderer.invoke("engine:status"),
  call: (verb, payload) => ipcRenderer.invoke("engine:call", verb, payload),
  open: (link) => ipcRenderer.invoke("engine:open", link),
  gamePath: () => ipcRenderer.invoke("game:path"),
  chooseGame: () => ipcRenderer.invoke("game:choose"),
});
