import assert from "node:assert/strict";
// Uses an already-running Chromium CDP endpoint; no browser profile or dependencies are created.
const baseUrl = process.env.FLUID_TEST_URL ?? "http://127.0.0.1:5119";
const cdpUrl = process.env.FLUID_CDP_URL ?? "http://127.0.0.1:9222";
const tabs = await (await fetch(`${cdpUrl}/json/list`)).json();
const ws = new WebSocket(tabs[0].webSocketDebuggerUrl);
await new Promise(resolve => ws.addEventListener("open", resolve, { once: true }));
let serial = 0;
const pending = new Map();
ws.addEventListener("message", ({data}) => {
    const result = JSON.parse(data);
    if (result.id) {
        const {resolve, reject} = pending.get(result.id);
        pending.delete(result.id);
        result.error ? reject(result.error) : resolve(result.result);
    }
});
const send = (method, params = {}) => new Promise((resolve, reject) => {
    const id = ++serial;
    pending.set(id, {resolve, reject});
    ws.send(JSON.stringify({id, method, params}));
});
const evaluate = async expression => {
    const result = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
    if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
};
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const wait = async (expression, timeout = 90000) => {
    const start = Date.now();
    while (!await evaluate(expression)) {
        if (Date.now() - start > timeout) throw new Error(`Timeout: ${expression}\n${await evaluate("document.body.innerText")}`);
        await sleep(200);
    }
};
const click = async text => evaluate(`(() => {
    const b = [...document.querySelectorAll('fluent-button,button')].find(b => b.textContent.trim() === ${JSON.stringify(text)});
    if (!b) throw Error('Missing button ' + ${JSON.stringify(text)});
    b.click();
})()`);
await send("Network.enable");
await send("Network.setCacheDisabled",{cacheDisabled:true});
await send("Page.enable");
await send("Page.addScriptToEvaluateOnNewDocument", {source:`(() => {
    if (!globalThis.GPUAdapter) return;
    const requestDevice=GPUAdapter.prototype.requestDevice;
    GPUAdapter.prototype.requestDevice=async function(...args) {
        const device=await requestDevice.apply(this,args);
        window.validationGpuDevice=device;
        return device;
    };
    const writeBuffer=GPUQueue.prototype.writeBuffer;
    GPUQueue.prototype.writeBuffer=function(...args) {
        const result=writeBuffer.apply(this,args);
        if(window.validationFailTerrainUpload && args[2] instanceof Int32Array) {
            window.validationFailTerrainUpload=false;
            window.validationEditFailureTriggered=true;
            window.validationGpuDevice.destroy();
            throw Error('Injected device failure during barrier upload');
        }
        return result;
    };
})()`});
await send("Page.navigate", { url: baseUrl });
await send("Emulation.setDeviceMetricsOverride", {width:1368,height:912,deviceScaleFactor:1,mobile:false});
await wait(`document.body?.innerText.includes('WebGPU compute ready')`);
console.log(await evaluate("document.body.innerText.slice(0,550)"));
await evaluate(`(() => {
    window.validationCpuDraws=0;
    const original=CanvasRenderingContext2D.prototype.fillRect;
    CanvasRenderingContext2D.prototype.fillRect=function(...args) {
        if(this.canvas===document.querySelector('canvas')) window.validationCpuDraws++;
        return original.apply(this,args);
    };
})()`);
for (let algorithm = 0; algorithm < 4; algorithm++) {
    await evaluate(`(() => {
        const select = document.querySelector('[aria-label="Fluid algorithm"]');
        select.value = '${algorithm}'; select.dispatchEvent(new Event('change', {bubbles:true}));
    })()`);
    await sleep(300);
    await click("Reset");
    await sleep(200);
    await click("Pour 12 in");
    await evaluate(`(() => {
        const s = document.querySelector('[aria-label="Ticks per display"]');
        s.value='8'; s.dispatchEvent(new Event('change',{bubbles:true}));
    })()`);
    await click("Run");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent) >= 128`, 180000);
    await click("Pause");
    await sleep(400);
    await click("Compare states");
    await wait(`!!document.querySelector('.comparison')`);
    assert(await evaluate(`document.querySelector('.comparison').classList.contains('match')`),
        await evaluate(`document.querySelector('.comparison').innerText`));
    console.log("model", algorithm, await evaluate(`document.querySelector('.comparison').innerText`));
    await evaluate(`document.querySelector('canvas').scrollIntoView({block:'center'})`);
    const point = await evaluate(`(() => { const r=document.querySelector('canvas').getBoundingClientRect();
        return {x:r.x+r.width*.5,y:r.y+r.height*.5}; })()`);
    await send("Input.dispatchMouseEvent", {type:"mousePressed", ...point, button:"left",clickCount:1});
    await send("Input.dispatchMouseEvent", {type:"mouseReleased", ...point, button:"left",clickCount:1});
    await wait(`document.querySelector('.navigation-panel p[role="status"]').textContent.includes('terrain')`);
    const initialTerrain = await evaluate(`Number(document.querySelector('.navigation-panel p[role="status"]').textContent.match(/terrain (\\d+)/)[1])`);
    await send("Input.dispatchMouseEvent", {type:"mousePressed", ...point, button:"right",buttons:2,clickCount:1});
    await send("Input.dispatchMouseEvent", {type:"mouseMoved", x:point.x+35,y:point.y,button:"right",buttons:2});
    await send("Input.dispatchMouseEvent", {type:"mouseReleased", x:point.x+35,y:point.y,button:"right",buttons:0,clickCount:1});
    await wait(`Number(document.querySelector('.navigation-panel p[role="status"]').textContent.match(/terrain (\\d+)/)[1]) === ${initialTerrain + 12}`);
    await click("Pour 12 in");
    const editTick = await evaluate(`Number(document.querySelector('.readout .metric strong').textContent)`);
    await click("Run");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent) >= ${editTick + 128}`,180000);
    await click("Pause");
    await sleep(300);
    await click("Compare states");
    await wait(`!!document.querySelector('.comparison')`);
    assert(await evaluate(`document.querySelector('.comparison').classList.contains('match')`),
        await evaluate(`document.querySelector('.comparison').innerText`));
    console.log("after real barrier and pour", algorithm, await evaluate(`document.querySelector('.comparison').innerText`));
    await click("CPU reference: on");
    await wait(`document.body.innerText.includes('CPU reference: off')`);
    await sleep(300);
    await evaluate(`window.validationCpuDraws=0`);
    const tick = await evaluate(`Number(document.querySelector('.readout .metric strong').textContent)`);
    await click("Run");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent) >= ${tick + 64}`, 180000);
    await click("Pause");
    await sleep(400);
    assert.equal(await evaluate(`window.validationCpuDraws`),0,"CPU-off performs no CPU canvas draws");
    await click("CPU reference: off");
    await wait(`document.body.innerText.includes('CPU re-synchronized')`);
    const resumeTick = await evaluate(`Number(document.querySelector('.readout .metric strong').textContent)`);
    await click("Run");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent) >= ${resumeTick + 128}`,180000);
    await click("Pause");
    await sleep(300);
    await click("Compare states");
    await wait(`!!document.querySelector('.comparison')`);
    assert(await evaluate(`document.querySelector('.comparison').classList.contains('match')`),
        await evaluate(`document.querySelector('.comparison').innerText`));
    console.log("resumed", algorithm, await evaluate(`document.querySelector('.comparison').innerText`));
    const camera = await evaluate(`JSON.stringify([...document.querySelectorAll('input[type="range"]')].map(s=>s.value))`);
    await evaluate(`(() => { const s=document.querySelector('[aria-label="Fluid algorithm"]');
        s.value='${(algorithm+1)%4}';s.dispatchEvent(new Event('change',{bubbles:true})); })()`);
    await sleep(500);
    const switched = await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
    assert(switched.cycles === 0 && switched.water.every(v=>v===0) && switched.momentum.every(v=>v===0), "switch clears complete GPU state");
    assert.equal(await evaluate(`JSON.stringify([...document.querySelectorAll('input[type="range"]')].map(s=>s.value))`),camera,"switch preserves camera");
    assert.equal(await evaluate(`Number(document.querySelector('.navigation-panel p[role="status"]').textContent.match(/terrain (\\d+)/)[1])`), initialTerrain+12,"switch preserves edited terrain");
    await click("Reset");
    await wait(`Number(document.querySelector('.navigation-panel p[role="status"]').textContent.match(/terrain (\\d+)/)[1]) === ${initialTerrain}`);
}
for (const [width,height] of [[390,844],[320,720],[912,1368]]) {
    await send("Emulation.setDeviceMetricsOverride",{width,height,deviceScaleFactor:1,mobile:false});
    await sleep(300);
    assert(await evaluate(`document.documentElement.scrollWidth <= window.innerWidth`),`no horizontal overflow at ${width}`);
}
await click("Pour 12 in");
await click("Step both");
await wait(`Number(document.querySelector('.readout .metric strong').textContent) === 1`);
const recovery = await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
await click("CPU reference: on");
await wait(`document.body.innerText.includes('CPU reference: off')`);
await evaluate(`import('./js/landscape-view.js').then(m=>m.notifyGpuFailure('Validation-injected GPU failure'))`);
await wait(`document.body.innerText.includes('CPU fallback recovered')`);
assert(await evaluate(`document.body.innerText.includes('tick ${recovery.cycles}')`),"fallback recovers completed tick");
await click("Step CPU");
await sleep(300);
assert.equal(await evaluate(`Number(document.querySelector('.readout .metric strong').textContent)`),recovery.cycles+1,"recovered CPU advances");
// Destroy the actual GPU device after a barrier's face/terrain writes begin.
// An unconfirmed edit must force explicit reset, never a mixed recovery baseline.
await send("Page.reload");
await wait(`document.body?.innerText.includes('WebGPU compute ready')`);
await evaluate(`(() => { const s=document.querySelector('[aria-label="Fluid algorithm"]');
    s.value='3';s.dispatchEvent(new Event('change',{bubbles:true})); })()`);
await sleep(300);
await click("Pour 12 in");
await click("Step both");
await wait(`Number(document.querySelector('.readout .metric strong').textContent)===1`);
await evaluate(`document.querySelector('canvas').scrollIntoView({block:'center'})`);
const failingPoint = await evaluate(`(() => {const r=document.querySelector('canvas').getBoundingClientRect();
    window.validationFailTerrainUpload=true;return {x:r.x+r.width*.5,y:r.y+r.height*.5};})()`);
await send("Input.dispatchMouseEvent",{type:"mousePressed",...failingPoint,button:"right",buttons:2,clickCount:1});
await send("Input.dispatchMouseEvent",{type:"mouseMoved",x:failingPoint.x+35,y:failingPoint.y,button:"right",buttons:2});
await send("Input.dispatchMouseEvent",{type:"mouseReleased",x:failingPoint.x+35,y:failingPoint.y,button:"right",buttons:0,clickCount:1});
await wait(`document.body.innerText.includes('GPU recovery unavailable')`);
assert(await evaluate(`window.validationEditFailureTriggered`),"device failure happened during barrier upload");
assert.equal(await evaluate(`Number(document.querySelector('.readout .metric strong').textContent)`),0,"partial barrier upload explicitly resets tick");
await click("Step CPU");
await wait(`Number(document.querySelector('.readout .metric strong').textContent)===1`);
// A browser without WebGPU must still offer every model and disallow reference-off.
await send("Page.addScriptToEvaluateOnNewDocument", {source:`Object.defineProperty(Navigator.prototype,'gpu',{get:()=>undefined,configurable:true})`});
await send("Page.reload");
await wait(`document.body?.innerText.includes('WebGPU is not available')`);
for(let algorithm=0;algorithm<4;algorithm++){
    await evaluate(`(() => { const s=document.querySelector('[aria-label="Fluid algorithm"]');
        s.value='${algorithm}';s.dispatchEvent(new Event('change',{bubbles:true})); })()`);
    await sleep(200);
    await click("Pour 12 in");
    await click("Step CPU");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent)===1`);
    assert(await evaluate(`[...document.querySelectorAll('fluent-button,button')].find(b=>b.textContent.trim()==='CPU reference: on').disabled`),"fallback cannot disable CPU");
}
console.log("All four actual WebGPU/C# comparisons, real edits, full-state resume, switch/reset, responsive layout and fallback checks passed.");
ws.close();
