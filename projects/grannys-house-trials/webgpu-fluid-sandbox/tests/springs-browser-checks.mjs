import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
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
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const evaluate = async expression => {
    const deadline = Date.now() + 90000;
    while (true) {
        const result = await send("Runtime.evaluate", { expression, awaitPromise: true, returnByValue: true });
        if (!result.exceptionDetails) return result.result.value;
        const error = JSON.stringify(result.exceptionDetails);
        // A reset upload deliberately rejects snapshots until its complete baseline is ready.
        if (!error.includes('An incomplete GPU reset or edit') || Date.now() >= deadline) throw new Error(error);
        await sleep(200);
    }
};
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
const select = async (label, value) => {
    await evaluate(`(() => {const s=document.querySelector('[aria-label=${JSON.stringify(label)}]');
        s.value=${JSON.stringify(String(value))};s.dispatchEvent(new Event('change',{bubbles:true}));})()`);
    await sleep(350);
};
const compare = async description => {
    await click("Compare states");
    await wait(`!!document.querySelector('.comparison')`);
    assert(await evaluate(`document.querySelector('.comparison').classList.contains('match')`),
        `${description}: ${await evaluate(`document.querySelector('.comparison').innerText`)}`);
};
const advance = async ticks => {
    const start = await evaluate(`Number(document.querySelector('.readout .metric strong').textContent)`);
    await click("Run");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent)>=${start+ticks}`,180000);
    await click("Pause");await sleep(300);
};
await send("Network.enable");
await send("Network.setCacheDisabled", {cacheDisabled:true});
await send("Page.enable");
await send("Emulation.setDeviceMetricsOverride", {width:1368,height:1000,deviceScaleFactor:1,mobile:false});
await send("Page.navigate", {url:baseUrl});
await wait(`document.body?.innerText.includes('WebGPU compute ready')`);
await select("Terrain scene",1);
assert(await evaluate(`document.querySelector('.engine-status').textContent.includes('WebGPU compute ready')`), await evaluate(`document.querySelector('.alert')?.textContent`));
await evaluate(`document.querySelector('canvas').scrollIntoView({block:'center'})`);
await sleep(700);
const rect=await evaluate(`(()=>{const r=document.querySelector('canvas').getBoundingClientRect();return {x:r.x,y:r.y,w:r.width,h:r.height};})()`);
let point={x:rect.x+rect.w*.5,y:rect.y+rect.h*.5};
const mouse=async button=>{
 await send('Input.dispatchMouseEvent',{type:'mousePressed',...point,button,buttons:button==='right'?2:1,clickCount:1});
 await send('Input.dispatchMouseEvent',{type:'mouseReleased',...point,button,buttons:0,clickCount:1});
 await sleep(250);
};
let selection;
for (const [px,py] of [[.5,.5],[.5,.7],[.65,.65],[.35,.65]]) {
 point={x:rect.x+rect.w*px,y:rect.y+rect.h*py};
 await mouse('left');await sleep(500);
 selection=await evaluate(`document.querySelector('.navigation-panel p[role="status"]').textContent`);
 const hit=selection.match(/Cell \(\d+, \d+\) · terrain (\d+) in/);
 if(hit&&+hit[1]===0)break;
}
assert(selection.includes('terrain 0 in'),`select interior source cell: ${selection}`);
await mouse('right');await mouse('right');
await wait(`document.querySelector('.spring-readout').textContent.includes('0.50')`);
assert(await evaluate(`document.querySelector('.spring-readout').textContent.includes('Springs: 1')`),"same cell creates only one spring");
let snapshot=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
assert(snapshot.water.every(v=>v===0),"paused creation does not pour a column");
const sourceSelection=await evaluate(`document.querySelector('.navigation-panel p[role="status"]').textContent`);
assert(sourceSelection.includes('spring 0.50 ft³/s'),"selected source shows rate");
console.log('real right-click creation and repeated-click rate passed',sourceSelection);
await click('Save / export map');await sleep(300);
const saved=await evaluate(`JSON.parse(localStorage.getItem('webgpu-fluid-sandbox.saved-terrain.v1'))`);
assert.equal(saved.Version,2);assert.equal(saved.Springs.length,1);assert.equal(saved.Springs[0].FlowCubicFeetPerSecond,.5);
for(let algorithm=0;algorithm<4;algorithm++) {
 await select('Fluid algorithm',algorithm);
 await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.algorithm===${algorithm}&&s.cycles===0)`);
 if(algorithm===3){
  await wait(`!!document.querySelector('[aria-label="MAC velocity advection"]')`);
  await evaluate(`document.querySelector('[aria-label="MAC velocity advection"]').click()`);
  await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.settings.macAdvection===true)`);
 }
 await click('Step both');await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===1)`);
 snapshot=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
 assert.equal(snapshot.cycles,1);assert(Math.abs(snapshot.water.reduce((a,b)=>a+b,0)/12-.5*.016)<1e-6,`algorithm ${algorithm}: single-tick volume`);
 await compare('spring single tick '+algorithm);
 await select('Ticks per display',8);
 await advance(32);
 snapshot=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
 const expected=.5*.016*snapshot.cycles,actual=snapshot.water.reduce((a,b)=>a+b,0)/12;
 assert(Math.abs(actual-expected)<.002,`algorithm ${algorithm}: batched source volume ${actual}/${expected}`);
 await compare('spring batched ticks '+algorithm);
 console.log('spring single/batched GPU-CPU agreement',algorithm,snapshot.cycles,actual);
}
await click('CPU reference: on');await sleep(300);
await advance(24);
await click('CPU reference: off');await sleep(300);
await compare('spring GPU-only then resume');
await click('Clear springs');await sleep(250);
assert(await evaluate(`document.querySelector('.spring-readout').textContent.includes('Springs: 0')`));
const previousTick=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles)`);
const before=(await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`)).water.reduce((a,b)=>a+b,0);
await click('Step both');await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===${previousTick+1})`);
const after=(await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`)).water.reduce((a,b)=>a+b,0);
assert(Math.abs(after-before)<.002,'clear stops injection without deleting water');
await compare('cleared spring agreement');
await click('Restore saved map');await wait(`document.querySelector('.spring-readout').textContent.includes('0.50')`);
snapshot=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
assert(snapshot.water.every(v=>v===0),'restore clears water and restores only terrain/source configuration');
await click('Step both');await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===1)`);await compare('restored spring');
await click('Reset');await sleep(200);
assert(await evaluate(`document.querySelector('.spring-readout').textContent.includes('0.50')`),'reset retains source');
await click('Remove selected spring');await sleep(200);
assert(await evaluate(`document.querySelector('.spring-readout').textContent.includes('Springs: 0')`),'remove selected source');
console.log('spring save/restore, removal, reset and GPU-only resume passed');
await click('Restore saved map');await wait(`document.querySelector('.spring-readout').textContent.includes('0.50')`);
const legacyPath=process.env.FLUID_LEGACY_MAP_PATH??fileURLToPath(new URL('../artifacts/tesla-valve-map.json',import.meta.url));
const {root}=await send('DOM.getDocument');
const {nodeId}=await send('DOM.querySelector',{nodeId:root.nodeId,selector:'input[type="file"]'});
await send('DOM.setFileInputFiles',{nodeId,files:[legacyPath]});
await wait(`document.body.innerText.includes('Loaded tesla-blue-route-map')`);
assert(await evaluate(`document.querySelector('.spring-readout').textContent.includes('Springs: 0')`),'legacy version-one terrain import clears sources');
await compare('legacy map import');
console.log('legacy terrain JSON import with no springs passed');
ws.close();
