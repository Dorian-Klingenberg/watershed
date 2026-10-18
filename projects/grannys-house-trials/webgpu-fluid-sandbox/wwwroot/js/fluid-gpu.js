import { renderGpu, notifyGpuFailure } from "./landscape-view.js";

const shader = `
struct FlowProposal {
    totalOutflow: f32,
    sendLeft: f32,
    sendRight: f32,
    sendUp: f32,
    sendDown: f32,
};

struct FluidParameters {
    width: u32,
    height: u32,
    maxFlow: f32,
    settleRate: f32,
    minimumWater: f32,
    drainEdges: u32,
    algorithm: u32,
    pipeArea: f32,
    retention: f32,
    terrainArea: f32,
    roughness: f32,
    damping: f32,
    advection: u32,
};

@group(0) @binding(0) var<storage, read> terrain: array<i32>;
@group(0) @binding(1) var<storage, read> waterIn: array<f32>;
@group(0) @binding(2) var<storage, read_write> proposals: array<FlowProposal>;
@group(0) @binding(3) var<storage, read_write> waterOut: array<f32>;
@group(0) @binding(4) var<uniform> params: FluidParameters;
@group(0) @binding(5) var<storage, read_write> momentum: array<vec2<f32>>;
@group(0) @binding(6) var<storage, read_write> edges: array<vec4<f32>>;
@group(0) @binding(7) var<storage, read_write> scales: array<f32>;
@group(0) @binding(8) var<storage, read> forcing: array<f32>;

fn velocity(x: i32, y: i32) -> vec2<f32> {
    if (x < 0 || y < 0 || x >= i32(params.width) || y >= i32(params.height)) { return vec2<f32>(0.0); }
    let q = momentum[u32(y) * params.width + u32(x)];
    return vec2<f32>(select(q.x, 0.0, x == i32(params.width)-1),
        select(q.y, 0.0, y == i32(params.height)-1));
}

@compute @workgroup_size(64)
fn advectVelocity(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= params.width * params.height) { return; }
    let x = i32(i % params.width); let y = i32(i / params.width);
    let c = velocity(x,y); let w = velocity(x-1,y); let e = velocity(x+1,y);
    let n = velocity(x,y-1); let s = velocity(x,y+1);
    let ne = velocity(x+1,y-1); let sw = velocity(x-1,y+1);
    let crossU = (c.x+w.x+s.x+sw.x)*0.25;
    let crossV = (c.y+e.y+n.y+ne.y)*0.25;
    let ux = select(e.x-c.x,c.x-w.x,c.x>=0.0);
    let uy = select(s.x-c.x,c.x-n.x,crossV>=0.0);
    let vx = select(e.y-c.y,c.y-w.y,crossU>=0.0);
    let vy = select(s.y-c.y,c.y-n.y,c.y>=0.0);
    edges[i] = vec4<f32>(c.x-0.016*(c.x*ux+crossV*uy),
        c.y-0.016*(crossU*vx+c.y*vy),0.0,0.0);
}

fn edgeTransfer(i: u32, axis: u32) -> f32 {
    return edges[i][axis + 2u];
}
fn scaledTransfer(i: u32, axis: u32) -> f32 {
    let transfer = edgeTransfer(i, axis);
    let neighbor = i + select(params.width, 1u, axis == 0u);
    if (transfer < 0.0) { return transfer * scales[neighbor]; }
    return transfer * scales[i];
}

@compute @workgroup_size(64)
fn proposeMomentum(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= params.width * params.height) { return; }
    var result = vec4<f32>(0.0);
    for (var axis = 0u; axis < 2u; axis++) {
        let neighbor = i + select(params.width, 1u, axis == 0u);
        if ((axis == 0u && i % params.width + 1u == params.width) ||
            neighbor >= params.width * params.height) { continue; }
        let crest = max(terrain[i], terrain[neighbor]);
        let left = max(0.0, (f32(terrain[i] - crest) + waterIn[i]) / 12.0);
        let right = max(0.0, (f32(terrain[neighbor] - crest) + waterIn[neighbor]) / 12.0);
        if (left == 0.0 && right == 0.0) { continue; }
        let head = left - right;
        let old = momentum[i][axis];
        let area = select(select(params.pipeArea,params.terrainArea,params.algorithm==2u), 1.0, params.algorithm == 3u);
        var initial = old;
        if (params.algorithm == 3u && params.advection != 0u) { initial = edges[i][axis]; }
        var next = initial + 0.016 * area * 32.174 * head;
        let donor = select(right, left, next >= 0.0);
        if (donor <= 0.0) { continue; }
        if (params.algorithm == 2u) {
            let radius = max(donor, 0.001);
            let radiusPower = pow(radius, 4.0 / 3.0);
            next /= 1.0 + 0.016 * 32.174 * params.roughness * params.roughness * abs(next) / (area * radiusPower);
        } else {
            next *= select(params.retention, 1.0 - params.damping * 0.016, params.algorithm == 3u);
        }
        let speedLimit = max(0.0, 0.45 / 0.016 - sqrt(32.174 * max(left, right)));
        let limit = speedLimit * select(area, 1.0, params.algorithm == 3u);
        next = clamp(next, -limit, limit);
        result[axis] = next;
        var transfer = (old + next) * 0.5 * 0.016 * 12.0;
        if (params.algorithm == 3u) { transfer = next * donor * 0.016 * 12.0; }
        if ((transfer > 0.0 && left > 0.0) || (transfer < 0.0 && right > 0.0)) {
            result[axis + 2u] = transfer;
        }
    }
    edges[i] = result;
}

@compute @workgroup_size(64)
fn limitMomentum(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= params.width * params.height) { return; }
    var west = 0.0;
    var north = 0.0;
    if (i % params.width > 0u) { west = edgeTransfer(i - 1u, 0u); }
    if (i >= params.width) { north = edgeTransfer(i - params.width, 1u); }
    let outgoing = max(0.0, -west) + max(0.0, edgeTransfer(i, 0u)) +
        max(0.0, -north) + max(0.0, edgeTransfer(i, 1u));
    var scale = 1.0;
    if (outgoing > 0.0) { scale = min(1.0, waterIn[i] * 0.25 / outgoing); }
    scales[i] = scale;
}

@compute @workgroup_size(64)
fn applyMomentum(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x;
    if (i >= params.width * params.height) { return; }
    let x = i % params.width;
    let y = i / params.width;
    var delta = 0.0;
    if (x > 0u) { delta += scaledTransfer(i - 1u, 0u); }
    delta -= scaledTransfer(i, 0u);
    if (y > 0u) { delta += scaledTransfer(i - params.width, 1u); }
    delta -= scaledTransfer(i, 1u);
    var nextWater = max(0.0, waterIn[i] + delta);
    if (params.drainEdges != 0u &&
        (x == 0u || x + 1u == params.width || y == 0u || y + 1u == params.height)) { nextWater = 0.0; }
    waterOut[i] = select(nextWater, forcing[i], forcing[i] >= 0.0) + forcing[params.width * params.height + i] * 0.016 * 12.0;
    for (var axis = 0u; axis < 2u; axis++) {
        let neighbor = i + select(params.width, 1u, axis == 0u);
        var scale = scales[i];
        if (edgeTransfer(i, axis) < 0.0) { scale = scales[neighbor]; }
        momentum[i][axis] = edges[i][axis] * scale;
    }
}

@compute @workgroup_size(64)
fn propose(@builtin(global_invocation_id) invocation: vec3<u32>) {
    let index = invocation.x;
    let cellCount = params.width * params.height;
    if (index >= cellCount) {
        return;
    }

    var proposal = FlowProposal(0.0, 0.0, 0.0, 0.0, 0.0);
    let availableWater = waterIn[index];
    if (availableWater <= params.minimumWater) {
        proposals[index] = proposal;
        return;
    }

    let x = index % params.width;
    let y = index / params.width;
    let currentSurface = f32(terrain[index]) + availableWater;
    var neighborIndices: array<u32, 4>;
    var neighborSurfaces: array<f32, 4>;
    var lowerCount = 0u;
    var surfaceSum = currentSurface;

    if (x > 0u) {
        let neighbor = index - 1u;
        let surface = f32(terrain[neighbor]) + waterIn[neighbor];
        if (surface < currentSurface - params.minimumWater) {
            neighborIndices[lowerCount] = 0u;
            neighborSurfaces[lowerCount] = surface;
            surfaceSum += surface;
            lowerCount++;
        }
    }
    if (x + 1u < params.width) {
        let neighbor = index + 1u;
        let surface = f32(terrain[neighbor]) + waterIn[neighbor];
        if (surface < currentSurface - params.minimumWater) {
            neighborIndices[lowerCount] = 1u;
            neighborSurfaces[lowerCount] = surface;
            surfaceSum += surface;
            lowerCount++;
        }
    }
    if (y > 0u) {
        let neighbor = index - params.width;
        let surface = f32(terrain[neighbor]) + waterIn[neighbor];
        if (surface < currentSurface - params.minimumWater) {
            neighborIndices[lowerCount] = 2u;
            neighborSurfaces[lowerCount] = surface;
            surfaceSum += surface;
            lowerCount++;
        }
    }
    if (y + 1u < params.height) {
        let neighbor = index + params.width;
        let surface = f32(terrain[neighbor]) + waterIn[neighbor];
        if (surface < currentSurface - params.minimumWater) {
            neighborIndices[lowerCount] = 3u;
            neighborSurfaces[lowerCount] = surface;
            surfaceSum += surface;
            lowerCount++;
        }
    }

    if (lowerCount == 0u) {
        proposals[index] = proposal;
        return;
    }

    let sharedSurface = surfaceSum / f32(lowerCount + 1u);
    var desiredInflows: array<f32, 4>;
    var desiredOutflow = 0.0;
    for (var i = 0u; i < lowerCount; i++) {
        desiredInflows[i] = max(0.0, sharedSurface - neighborSurfaces[i]);
        desiredOutflow += desiredInflows[i];
    }

    if (desiredOutflow <= params.minimumWater) {
        proposals[index] = proposal;
        return;
    }

    let totalFlow = min(min(availableWater, params.maxFlow), desiredOutflow * params.settleRate);
    if (totalFlow <= params.minimumWater) {
        proposals[index] = proposal;
        return;
    }

    proposal.totalOutflow = totalFlow;
    let flowScale = totalFlow / desiredOutflow;
    for (var i = 0u; i < lowerCount; i++) {
        let inflow = desiredInflows[i] * flowScale;
        switch neighborIndices[i] {
            case 0u: { proposal.sendLeft = inflow; }
            case 1u: { proposal.sendRight = inflow; }
            case 2u: { proposal.sendUp = inflow; }
            default: { proposal.sendDown = inflow; }
        }
    }

    proposals[index] = proposal;
}

@compute @workgroup_size(64)
fn apply(@builtin(global_invocation_id) invocation: vec3<u32>) {
    let index = invocation.x;
    let cellCount = params.width * params.height;
    if (index >= cellCount) {
        return;
    }

    let x = index % params.width;
    let y = index / params.width;
    var delta = 0.0;
    if (y > 0u) {
        delta += proposals[index - params.width].sendDown;
    }
    if (x > 0u) {
        delta += proposals[index - 1u].sendRight;
    }
    delta -= proposals[index].totalOutflow;
    if (x + 1u < params.width) {
        delta += proposals[index + 1u].sendLeft;
    }
    if (y + 1u < params.height) {
        delta += proposals[index + params.width].sendUp;
    }

    var nextWater = max(0.0, waterIn[index] + delta);
    if (params.drainEdges != 0u &&
        (x == 0u || x + 1u == params.width || y == 0u || y + 1u == params.height)) {
        nextWater = 0.0;
    }
    waterOut[index] = select(nextWater, forcing[index], forcing[index] >= 0.0) + forcing[params.width * params.height + index] * 0.016 * 12.0;
}
`;

let state;

export async function initialize(canvas, width, height, terrain, water, maxFlow, settleRate, minimumWater, drainEdges, algorithm = 0, settings) {
    if (!navigator.gpu) {
        return { supported: false, message: "WebGPU is not available in this browser; the CPU reference remains usable." };
    }

    try {
        const adapter = await navigator.gpu.requestAdapter();
        if (!adapter) {
            return { supported: false, message: "No WebGPU adapter was found; the CPU reference remains usable." };
        }

        const device = await adapter.requestDevice();
        const cellCount = width * height;
        const bufferSize = cellCount * Float32Array.BYTES_PER_ELEMENT;
        const terrainBuffer = device.createBuffer({
            size: cellCount * Int32Array.BYTES_PER_ELEMENT,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST
        });
        const waterBuffers = [0, 1].map(() => device.createBuffer({
            size: bufferSize,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC
        }));
        const proposalBuffer = device.createBuffer({
            size: cellCount * 20,
            usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC
        });
        const parameterBuffer = device.createBuffer({
            size: 64,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });
        const readbackBuffer = device.createBuffer({
            size: bufferSize * 8,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
        });
        const momentumBuffer = device.createBuffer({
            size: bufferSize * 2, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC
        });
        const edgeBuffer = device.createBuffer({ size: bufferSize * 4, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
        const scaleBuffer = device.createBuffer({ size: bufferSize, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
        const forcingBuffer = device.createBuffer({size: bufferSize * 2, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST});
        device.queue.writeBuffer(forcingBuffer,0,new Float32Array(cellCount).fill(-1));

        const bindGroupLayout = device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
                { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
                { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } },
                ...[5, 6, 7].map(binding => ({ binding, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } })),
                {binding:8,visibility:GPUShaderStage.COMPUTE,buffer:{type:"read-only-storage"}}
            ]
        });
        const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [bindGroupLayout] });
        const module = device.createShaderModule({ code: shader });
        const proposePipeline = device.createComputePipeline({
            layout: pipelineLayout,
            compute: { module, entryPoint: "propose" }
        });
        const applyPipeline = device.createComputePipeline({
            layout: pipelineLayout,
            compute: { module, entryPoint: "apply" }
        });
        const momentumPipelines = ["proposeMomentum", "limitMomentum", "applyMomentum"].map(entryPoint =>
            device.createComputePipeline({ layout: pipelineLayout, compute: { module, entryPoint } }));
        const advectionPipeline = device.createComputePipeline({
            layout: pipelineLayout, compute: {module,entryPoint:"advectVelocity"}
        });
        const compilation = await module.getCompilationInfo();
        const errors = compilation.messages.filter(message => message.type === "error");
        if (errors.length) { device.destroy(); throw new Error(errors.map(error => error.message).join("\n")); }

        const params = new ArrayBuffer(64);
        const view = new DataView(params);
        view.setUint32(0, width, true);
        view.setUint32(4, height, true);
        view.setFloat32(8, maxFlow, true);
        view.setFloat32(12, settleRate, true);
        view.setFloat32(16, minimumWater, true);
        view.setUint32(20, drainEdges ? 1 : 0, true);
        view.setUint32(24, algorithm, true);
        writeSettings(params,settings);

        device.queue.writeBuffer(terrainBuffer, 0, new Int32Array(terrain));
        device.queue.writeBuffer(parameterBuffer, 0, params);
        const bindGroups = waterBuffers.map((input, index) => device.createBindGroup({
            layout: bindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: terrainBuffer } },
                { binding: 1, resource: { buffer: input } },
                { binding: 2, resource: { buffer: proposalBuffer } },
                { binding: 3, resource: { buffer: waterBuffers[1 - index] } },
                { binding: 4, resource: { buffer: parameterBuffer } },
                { binding: 5, resource: { buffer: momentumBuffer } },
                { binding: 6, resource: { buffer: edgeBuffer } },
                { binding: 7, resource: { buffer: scaleBuffer } },
                { binding: 8, resource: { buffer: forcingBuffer } }
            ]
        }));

        state = {
            canvas,
            device,
            width,
            height,
            cellCount,
            bufferSize,
            terrain: Array.from(terrain),
            terrainBuffer,
            waterBuffers,
            readbackBuffer,
            proposePipeline,
            applyPipeline,
            momentumPipelines, advectionPipeline, momentumBuffer, edgeBuffer, scaleBuffer, parameterBuffer, params, algorithm, settings, proposalBuffer, forcingBuffer,
            bindGroups,
            current: 0,
            cycles: 0,
            direction: 0,
            cachedWater: new Float32Array(water),
            cachedMomentum: new Float32Array(cellCount * 2),
            terrainDirty: true,
            error: null,
            workgroupCount: Math.ceil(cellCount / 64)
        };
        const initializedState = state;
        const fail = message => {
            initializedState.error = message;
            if (state === initializedState) notifyGpuFailure(message);
        };
        device.lost.then(info => { if (info.reason !== "destroyed") fail(`WebGPU device lost: ${info.message}`); });
        device.addEventListener("uncapturederror", event => {
            fail(`WebGPU validation failed: ${event.error.message}`);
        });
        await uploadWater(state, water);
        return { supported: true, message: "WebGPU compute ready" };
    } catch (error) {
        state = undefined;
        return { supported: false, message: `WebGPU setup failed: ${error.message}` };
    }
}

export async function reset(terrain, water, algorithm = 0, settings, forcing, springRates = null) {
    requireState();
    if (terrain.length !== state.cellCount) throw new Error("Terrain size does not match the field.");
    const nextTerrain = Array.from(terrain);
    // A partial reset cannot safely pair old cached water with C#'s reset terrain.
    state.recoveryInvalid = true;
    new DataView(state.params).setUint32(24, algorithm, true);
    writeSettings(state.params,settings);
    state.device.queue.writeBuffer(state.forcingBuffer,0,new Float32Array(forcing.depths));
    setSprings(springRates ?? new Float32Array(state.cellCount));
    state.device.queue.writeBuffer(state.parameterBuffer, 0, state.params);
    state.device.queue.writeBuffer(state.momentumBuffer, 0, new Float32Array(state.cellCount * 2));
    state.device.queue.writeBuffer(state.terrainBuffer, 0, new Int32Array(nextTerrain));
    const values = new Float32Array(water);
    for(let i=0;i<values.length;i++) if(forcing.depths[i]>=0) values[i]=forcing.depths[i];
    await uploadWater(state, values, { terrain: nextTerrain, cycles: 0, algorithm, settings, direction:forcing.direction });
}

export function setSprings(rates) {
    requireState();
    if (rates.length !== state.cellCount || Array.from(rates).some(rate => !Number.isFinite(rate) || rate < 0 || rate > 1000))
        throw new Error("Invalid spring rates.");
    state.device.queue.writeBuffer(state.forcingBuffer, state.bufferSize, new Float32Array(rates));
}

export async function applyEdits(terrain, waterDelta) {
    requireState();
    if (terrain && terrain.length !== state.cellCount) throw new Error("Terrain size does not match the field.");
    if (waterDelta && waterDelta.length !== state.cellCount) throw new Error("Water edit size does not match the field.");
    if (!terrain && !waterDelta) return;
    // C# already owns the edited terrain. A failed upload must reset fallback,
    // not import pre-edit depths with partly updated terrain or face state.
    state.recoveryInvalid = true;
    if (terrain) uploadTerrain(terrain);
    if (waterDelta) {
        const water = state.cachedWater.slice();
        for (let i = 0; i < water.length; i++) water[i] += waterDelta[i];
        await uploadWater(state, water);
    } else {
        await state.device.queue.onSubmittedWorkDone();
    }
    requireState();
    state.recoveryInvalid = false;
    if (!waterDelta) drawGpuSnapshot(state);
}

function uploadTerrain(terrain) {
    if (terrain.length !== state.cellCount) throw new Error("Terrain size does not match the field.");
    const momentum = state.cachedMomentum.slice();
    for (let i = 0; i < terrain.length; i++) {
        if (terrain[i] === state.terrain[i]) continue;
        momentum[2 * i] = momentum[2 * i + 1] = 0;
        if (i % state.width > 0) momentum[2 * (i - 1)] = 0;
        if (i >= state.width) momentum[2 * (i - state.width) + 1] = 0;
    }
    state.device.queue.writeBuffer(state.momentumBuffer, 0, momentum);
    state.cachedMomentum = momentum;
    state.terrain = Array.from(terrain);
    state.terrainDirty = true;
    state.device.queue.writeBuffer(state.terrainBuffer, 0, new Int32Array(terrain));
}

export async function step(ticks = 1) {
    requireState();
    if (!Number.isInteger(ticks) || ticks < 1 || ticks > 8) throw new Error("Invalid tick batch.");
    const encoder = state.device.createCommandEncoder();
    for (let tick = 0; tick < ticks; ++tick) {
        if (state.algorithm !== 0) {
            if(state.algorithm===3 && state.settings.macAdvection) {
                const pass=encoder.beginComputePass();
                pass.setPipeline(state.advectionPipeline);
                pass.setBindGroup(0,state.bindGroups[state.current]);
                pass.dispatchWorkgroups(state.workgroupCount);
                pass.end();
            }
            for (const pipeline of state.momentumPipelines) {
                const pass = encoder.beginComputePass();
                pass.setPipeline(pipeline);
                pass.setBindGroup(0, state.bindGroups[state.current]);
                pass.dispatchWorkgroups(state.workgroupCount);
                pass.end();
            }
            state.current = 1 - state.current;
            continue;
        }
        const proposePass = encoder.beginComputePass();
        proposePass.setPipeline(state.proposePipeline);
        proposePass.setBindGroup(0, state.bindGroups[state.current]);
        proposePass.dispatchWorkgroups(state.workgroupCount);
        proposePass.end();

        const applyPass = encoder.beginComputePass();
        applyPass.setPipeline(state.applyPipeline);
        applyPass.setBindGroup(0, state.bindGroups[state.current]);
        applyPass.dispatchWorkgroups(state.workgroupCount);
        applyPass.end();
        state.current = 1 - state.current;
    }
    state.device.queue.submit([encoder.finish()]);
    const complete = await readCurrentWater(state);
    requireState();
    state.cachedWater = complete.water;
    state.cachedMomentum = complete.momentum;
    state.cachedTransfers = complete.transfers;
    state.cycles += ticks;
    drawGpuSnapshot(state);
}

export async function snapshot(fresh = false) {
    if (!state) throw new Error("No GPU baseline is available.");
    if (state.recoveryInvalid) throw new Error("An incomplete GPU reset or edit invalidated the recovery baseline.");
    if (fresh) {
        requireState();
        const complete = await readCurrentWater(state);
        state.cachedWater = complete.water;
        state.cachedMomentum = complete.momentum;
        requireState();
    }
    return { water: Array.from(state.cachedWater), momentum: Array.from(state.cachedMomentum),
        cycles: state.cycles, algorithm: state.algorithm, settings:state.settings, direction:state.direction };
}

export function dispose() {
    if (state) {
        state.device.destroy();
        state = undefined;
    }
}

async function uploadWater(currentState, water, baseline = null) {
    const values = water instanceof Float32Array ? water : new Float32Array(water);
    if (values.length !== currentState.cellCount) {
        throw new Error("Water snapshot size does not match the GPU field.");
    }

    currentState.device.queue.writeBuffer(currentState.waterBuffers[0], 0, values);
    currentState.device.queue.writeBuffer(currentState.waterBuffers[1], 0, values);
    currentState.current = 0;
    await currentState.device.queue.onSubmittedWorkDone();
    requireState();
    currentState.cachedWater = values;
    if (baseline) {
        currentState.terrain = baseline.terrain;
        currentState.terrainDirty = true;
        currentState.cycles = baseline.cycles;
        currentState.algorithm = baseline.algorithm;
        currentState.settings = baseline.settings;
        currentState.direction = baseline.direction;
        currentState.cachedTransfers = new Float32Array(currentState.cellCount * 2);
        currentState.cachedMomentum = new Float32Array(currentState.cellCount * 2);
        currentState.recoveryInvalid = false;
    }
    drawGpuSnapshot(currentState);
}

function drawGpuSnapshot(currentState) {
    renderGpu(currentState.terrainDirty ? currentState.terrain : null, currentState.cachedWater);
    currentState.terrainDirty = false;
}

async function readCurrentWater(currentState) {
    const encoder = currentState.device.createCommandEncoder();
    encoder.copyBufferToBuffer(
        currentState.waterBuffers[currentState.current],
        0,
        currentState.readbackBuffer,
        0,
        currentState.bufferSize);
    encoder.copyBufferToBuffer(currentState.momentumBuffer, 0, currentState.readbackBuffer,
        currentState.bufferSize, currentState.bufferSize * 2);
    if(currentState.direction!==0) {
        encoder.copyBufferToBuffer(currentState.algorithm===0 ? currentState.proposalBuffer : currentState.edgeBuffer,0,
            currentState.readbackBuffer,currentState.bufferSize*3,currentState.bufferSize*(currentState.algorithm===0?5:4));
        if(currentState.algorithm!==0) encoder.copyBufferToBuffer(currentState.scaleBuffer,0,currentState.readbackBuffer,currentState.bufferSize*7,currentState.bufferSize);
    }
    currentState.device.queue.submit([encoder.finish()]);
    await currentState.readbackBuffer.mapAsync(GPUMapMode.READ);
    const values = new Float32Array(currentState.readbackBuffer.getMappedRange(0,currentState.bufferSize*(currentState.direction!==0?8:3))).slice();
    currentState.readbackBuffer.unmap();
    const n=currentState.cellCount;
    const transfers=currentState.direction!==0 ? new Float32Array(n*2) : null;
    if(transfers) for(let i=0;i<n;i++) for(let axis=0;axis<2;axis++){
        const neighbor=i+(axis===0?1:currentState.width);
        if(neighbor>=n || (axis===0 && i%currentState.width===currentState.width-1)) continue;
        if(currentState.algorithm===0) transfers[2*i+axis]=values[3*n+5*i+(axis===0?2:4)]-values[3*n+5*neighbor+(axis===0?1:3)];
        else {
            const t=values[3*n+4*i+axis+2];
            transfers[2*i+axis]=t*values[7*n+(t>=0?i:neighbor)];
        }
    }
    return { water: values.slice(0, n), momentum: values.slice(n,3*n),transfers };
}

function writeSettings(params,s) {
    const v=new DataView(params);
    v.setFloat32(8,s.cellularMaxFlow,true);v.setFloat32(12,s.cellularSettleRate,true);
    [s.pipeArea,s.pipeRetention,s.terrainHeadArea,s.manningRoughness,s.macDamping].forEach((x,i)=>v.setFloat32(28+4*i,x,true));
    v.setUint32(48,s.macAdvection?1:0,true);
}

export function measure(discharge,bypass) {
    requireState();
    const read=p=>p.faces.reduce((sum,face,i)=>sum+(state.cachedTransfers?.[face]??0)*p.weights[i],0);
    return {discharge:read(discharge),bypassFlow:read(bypass)};
}

function requireState() {
    if (!state) {
        throw new Error("WebGPU is not initialized.");
    }
    if (state.error) throw new Error(state.error);
}
