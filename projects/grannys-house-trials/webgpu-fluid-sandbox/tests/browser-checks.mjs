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
const palette = await evaluate(`import('./js/landscape-view.js').then(({waterDepthColor: color}) => {
    const top=Array.from({length:73},(_,i)=>color(i));
    const side=Array.from({length:73},(_,i)=>color(i,true));
    const light=c=>Number(c.match(/([\\d.]+)%\\)$/)[1]);
    const normalize=c=>{
        const span=document.createElement('span');span.style.color=c;document.body.append(span);
        const result=getComputedStyle(span).color;span.remove();return result;
    };
    return {
        sameBand:color(0.1)===color(0.9),
        distinct:new Set(top.slice(1)).size===72,
        monotone:top.every((c,i)=>i===0||light(c)<light(top[i-1])),
        deeper18:color(18)!==color(19),
        safe:[-1,NaN,Infinity,-Infinity].every(v=>color(v)===color(0)),
        darkerSides:side.every((c,i)=>Math.abs(light(c)-light(top[i])*0.8)<0.0011),
        legend:Array.from({length:13},(_,n)=>n).every(n=>{
            const variable=getComputedStyle(document.documentElement).getPropertyValue('--water-depth-'+n).trim();
            const swatch=document.querySelector('.depth-swatch.depth-'+n);
            return variable===color(n)&&swatch&&getComputedStyle(swatch).backgroundColor===normalize(color(n));
        }),
        labelled:!!document.querySelector('[aria-label="Water depth in inches"]')
    };
})`);
for(const [check,passed] of Object.entries(palette)) assert(passed,`shared renderer depth palette: ${check}`);
console.log("Shared inch-band palette and exact legend colors passed.");
if(process.env.FLUID_PALETTE_ONLY==="1"){
    ws.close();
    process.exit(0);
}
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
    const knobs = [
        [["Maximum flow (in/tick)",1.3],["Settle rate",0.37]],
        [["Pipe area (ft²)",0.33],["Momentum retention / tick",0.89]],
        [["Terrain-head area (ft²)",0.31],["Manning roughness",0.055]],
        [["MAC damping (1/s)",0.08]]
    ][algorithm];
    assert.equal(await evaluate(`document.querySelectorAll('[aria-label="Algorithm settings"] input[type="number"]').length`),knobs.length,"only algorithm-relevant knobs visible");
    assert(await evaluate(`document.querySelector('.algorithm-help').textContent.includes(${JSON.stringify(["settle rate","retention","roughness","advection"][algorithm])})`),
        "per-algorithm help explains knob effects");
    for(const [label,value] of knobs) await select(label,value);
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
    await wait(`document.querySelectorAll('.navigation-panel p[role="status"]')[1].textContent.includes('selected · paused')`);
    const switched = await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
    assert(switched.cycles === 0 && switched.water.every(v=>v===0) && switched.momentum.every(v=>v===0), "switch clears complete GPU state");
    assert.equal(await evaluate(`JSON.stringify([...document.querySelectorAll('input[type="range"]')].map(s=>s.value))`),camera,"switch preserves camera");
    assert.equal(await evaluate(`Number(document.querySelector('.navigation-panel p[role="status"]').textContent.match(/terrain (\\d+)/)[1])`), initialTerrain+12,"switch preserves edited terrain");
    await click("Reset");
    await wait(`Number(document.querySelector('.navigation-panel p[role="status"]').textContent.match(/terrain (\\d+)/)[1]) === ${initialTerrain}`);
}
// Terrain is independent of solver and settings; every scene is resettable on each backend.
for(let scene=0;scene<5;scene++) for(let algorithm=0;algorithm<4;algorithm++){
    await select("Terrain scene",scene);
    await select("Fluid algorithm",algorithm);
    await click("Restore algorithm defaults");await sleep(300);
    if(scene===3) {
        for(const direction of ["Forward","Reverse"]){
            await click(`${direction} experiment`);
            await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===0&&s.direction===${direction==="Forward"?1:2}).catch(()=>false)`);
            await click("Advance 128 experiment ticks");
            await wait(`Number(document.querySelector('.readout .metric strong').textContent)===128`,180000);
            await compare(`Tesla ${algorithm} ${direction}`);
            const baseline=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
            assert.equal(baseline.water[32*96+8],direction==="Forward"?18:12,"fixed left reservoir head");
            assert.equal(baseline.water[32*96+88],direction==="Forward"?12:18,"fixed right reservoir head");
            console.log("Tesla measurement",algorithm,direction,"tick",baseline.cycles,
                await evaluate(`document.querySelector('.experiment-readout').innerText`));
            assert(await evaluate(`document.querySelector('.experiment-readout').innerText.includes(${JSON.stringify(algorithm===0?"ft³/tick":"ft³/s")})`),
                "experiment reports correct physical or algorithmic-time units");
        }
        assert(await evaluate(`document.body.innerText.includes('Measured |forward| − |reverse|:')`),"equal-tick trial reports measured directional difference");
        await click("CPU reference: on");await sleep(300);
        await click("Forward experiment");
        await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===0&&s.direction===1).catch(()=>false)`);
        await advance(64);
        await click("CPU reference: off");
        await wait(`document.body.innerText.includes('CPU re-synchronized')`);
        await advance(64);
        await compare(`Tesla ${algorithm} GPU-only forcing/resume`);
    } else {
        await click("Pour 12 in");
        await advance(64);
        await compare(`scene ${scene}, model ${algorithm}`);
    }
    await click("Reset");
    await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===0).catch(()=>false)`);
    const reset=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
    assert.equal(reset.cycles,0);
    assert(reset.water.every(v=>v===0)&&reset.momentum.every(v=>v===0),"scene reset clears all fluid and forcing");
}
await select("Terrain scene",1);
await select("Fluid algorithm",3);
await evaluate(`document.querySelector('[aria-label="MAC velocity advection"]').click()`);
await sleep(300);
await click("Pour 12 in");
await advance(128);
await compare("real MAC velocity advection enabled");
assert(await evaluate(`import('./js/fluid-gpu.js').then(async m=>(await m.snapshot()).settings.macAdvection)`),"advection setting included in complete state");
await click("CPU reference: on");await sleep(300);
await advance(64);
await click("CPU reference: off");
await wait(`document.body.innerText.includes('CPU re-synchronized')`);
await advance(64);
await compare("advective MAC complete-state resume");
await select("Terrain scene",3);
for(const direction of ["Forward","Reverse"]){
    await click(`${direction} experiment`);
    await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===0&&s.direction===${direction==="Forward"?1:2}).catch(()=>false)`);
    await click("Advance 128 experiment ticks");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent)===128`,180000);
    await compare(`advective Tesla ${direction}`);
    console.log("Tesla advective MAC",direction,await evaluate(`document.querySelector('.experiment-readout').innerText`));
}
const savedCamera=await evaluate(`JSON.stringify([...document.querySelectorAll('input[type="range"]')].map(s=>s.value))`);
await select("MAC damping (1/s)",0.12);
await wait(`import('./js/fluid-gpu.js').then(m=>m.snapshot()).then(s=>s.cycles===0&&s.settings.macDamping===0.12).catch(()=>false)`);
const changed=await evaluate(`import('./js/fluid-gpu.js').then(m=>m.snapshot())`);
assert(changed.cycles===0&&changed.water.every(v=>v===0)&&changed.momentum.every(v=>v===0),"settings reset all fluid");
assert.equal(await evaluate(`JSON.stringify([...document.querySelectorAll('input[type="range"]')].map(s=>s.value))`),savedCamera,"settings preserve camera");
await click("Restore algorithm defaults");await sleep(300);
assert(!await evaluate(`document.querySelector('[aria-label="MAC velocity advection"]').checked`),"restore defaults disables actual advection");
await select("Terrain scene",0);
await click("Reset");await sleep(300);
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
for(let scene=0;scene<4;scene++) for(let algorithm=0;algorithm<4;algorithm++){
    await select("Terrain scene",scene);
    await evaluate(`(() => { const s=document.querySelector('[aria-label="Fluid algorithm"]');
        s.value='${algorithm}';s.dispatchEvent(new Event('change',{bubbles:true})); })()`);
    await sleep(200);
    await click("Pour 12 in");
    await click("Step CPU");
    await wait(`Number(document.querySelector('.readout .metric strong').textContent)===1`);
    assert(await evaluate(`[...document.querySelectorAll('fluent-button,button')].find(b=>b.textContent.trim()==='CPU reference: on').disabled`),"fallback cannot disable CPU");
}
console.log("All actual WebGPU/C# comparisons, nondefault settings, advection, five terrains, controlled Tesla forcing, edits, full-state resume, switch/reset, responsive layout and fallback checks passed.");
ws.close();
