const storageKey = "webgpu-fluid-sandbox.saved-terrain.v1";

export function saveMap(json) {
    // Keep a browser-local copy for returning to the map and download a shareable review file.
    localStorage.setItem(storageKey, json);
    const blob = new Blob([json], {type: "application/json"});
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "fluid-sandbox-terrain-map.json";
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function loadMap() {
    return localStorage.getItem(storageKey);
}
