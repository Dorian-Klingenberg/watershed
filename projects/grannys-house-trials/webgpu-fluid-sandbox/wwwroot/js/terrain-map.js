const storageKey = "webgpu-fluid-sandbox.saved-terrain.v1";
let currentMap = null;

function downloadMap(json) {
    const blob = new Blob([json], {type: "application/json"});
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "fluid-sandbox-terrain-map.json";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function initializeSaveButton(id) {
    const button = document.getElementById(id);
    if (!button || button.dataset.mapSaveBound) return;
    button.dataset.mapSaveBound = "true";
    // Download synchronously inside the real click so the browser preserves user activation.
    button.addEventListener("click", () => {
        if (!currentMap) return;
        localStorage.setItem(storageKey, currentMap);
        downloadMap(currentMap);
    }, {capture: true});
}

export function setMapData(json) {
    currentMap = json;
}

export function storeMap(json) {
    localStorage.setItem(storageKey, json);
}

export function loadMap() {
    return localStorage.getItem(storageKey);
}
