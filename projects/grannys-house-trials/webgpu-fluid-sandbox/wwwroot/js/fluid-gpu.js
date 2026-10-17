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
};

@group(0) @binding(0) var<storage, read> terrain: array<i32>;
@group(0) @binding(1) var<storage, read> waterIn: array<f32>;
@group(0) @binding(2) var<storage, read_write> proposals: array<FlowProposal>;
@group(0) @binding(3) var<storage, read_write> waterOut: array<f32>;
@group(0) @binding(4) var<uniform> params: FluidParameters;

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
    waterOut[index] = nextWater;
}
`;

let state;

export async function initialize(canvas, width, height, terrain, water, maxFlow, settleRate, minimumWater, drainEdges) {
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
            usage: GPUBufferUsage.STORAGE
        });
        const parameterBuffer = device.createBuffer({
            size: 32,
            usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
        });
        const readbackBuffer = device.createBuffer({
            size: bufferSize,
            usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ
        });

        const bindGroupLayout = device.createBindGroupLayout({
            entries: [
                { binding: 0, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
                { binding: 1, visibility: GPUShaderStage.COMPUTE, buffer: { type: "read-only-storage" } },
                { binding: 2, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
                { binding: 3, visibility: GPUShaderStage.COMPUTE, buffer: { type: "storage" } },
                { binding: 4, visibility: GPUShaderStage.COMPUTE, buffer: { type: "uniform" } }
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

        const params = new ArrayBuffer(32);
        const view = new DataView(params);
        view.setUint32(0, width, true);
        view.setUint32(4, height, true);
        view.setFloat32(8, maxFlow, true);
        view.setFloat32(12, settleRate, true);
        view.setFloat32(16, minimumWater, true);
        view.setUint32(20, drainEdges ? 1 : 0, true);

        device.queue.writeBuffer(terrainBuffer, 0, new Int32Array(terrain));
        device.queue.writeBuffer(parameterBuffer, 0, params);
        const bindGroups = waterBuffers.map((input, index) => device.createBindGroup({
            layout: bindGroupLayout,
            entries: [
                { binding: 0, resource: { buffer: terrainBuffer } },
                { binding: 1, resource: { buffer: input } },
                { binding: 2, resource: { buffer: proposalBuffer } },
                { binding: 3, resource: { buffer: waterBuffers[1 - index] } },
                { binding: 4, resource: { buffer: parameterBuffer } }
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
            waterBuffers,
            readbackBuffer,
            proposePipeline,
            applyPipeline,
            bindGroups,
            current: 0,
            workgroupCount: Math.ceil(cellCount / 64)
        };
        await uploadWater(state, water);
        return { supported: true, message: "WebGPU compute ready" };
    } catch (error) {
        state = undefined;
        return { supported: false, message: `WebGPU setup failed: ${error.message}` };
    }
}

export async function reset(water) {
    requireState();
    await uploadWater(state, water);
}

export async function setWater(water) {
    requireState();
    await uploadWater(state, water);
}

export async function step() {
    requireState();
    const encoder = state.device.createCommandEncoder();
    const next = 1 - state.current;
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

    encoder.copyBufferToBuffer(state.waterBuffers[next], 0, state.readbackBuffer, 0, state.bufferSize);
    state.device.queue.submit([encoder.finish()]);
    state.current = next;
    await drawGpuSnapshot(state);
}

export async function snapshot() {
    requireState();
    return await readCurrentWater(state);
}

export function renderCpu(canvas, terrain, water, width, height) {
    drawField(canvas, terrain, water, width, height);
}

export function dispose() {
    if (state) {
        state.device.destroy();
        state = undefined;
    }
}

async function uploadWater(currentState, water) {
    const values = new Float32Array(water);
    if (values.length !== currentState.cellCount) {
        throw new Error("Water snapshot size does not match the GPU field.");
    }

    currentState.device.queue.writeBuffer(currentState.waterBuffers[0], 0, values);
    currentState.device.queue.writeBuffer(currentState.waterBuffers[1], 0, values);
    currentState.current = 0;
    await drawGpuSnapshot(currentState);
}

async function drawGpuSnapshot(currentState) {
    const water = await readCurrentWater(currentState);
    drawField(currentState.canvas, currentState.terrain, water, currentState.width, currentState.height);
}

async function readCurrentWater(currentState) {
    const encoder = currentState.device.createCommandEncoder();
    encoder.copyBufferToBuffer(
        currentState.waterBuffers[currentState.current],
        0,
        currentState.readbackBuffer,
        0,
        currentState.bufferSize);
    currentState.device.queue.submit([encoder.finish()]);
    await currentState.readbackBuffer.mapAsync(GPUMapMode.READ);
    const water = Array.from(new Float32Array(currentState.readbackBuffer.getMappedRange()));
    currentState.readbackBuffer.unmap();
    return water;
}

function drawField(canvas, terrain, water, width, height) {
    const context = canvas.getContext("2d");
    if (!context) {
        return;
    }

    canvas.width = width;
    canvas.height = height;
    context.imageSmoothingEnabled = false;
    const image = context.createImageData(width, height);
    let maxDepth = 0.001;
    for (const depth of water) {
        maxDepth = Math.max(maxDepth, depth);
    }

    for (let i = 0; i < water.length; i++) {
        const depth = water[i];
        const shade = Math.min(1, Math.max(0, terrain[i] / 56));
        const offset = i * 4;
        if (depth > 0.001) {
            const intensity = Math.min(1, depth / maxDepth);
            image.data[offset] = 35 + 24 * intensity;
            image.data[offset + 1] = 107 + 93 * intensity;
            image.data[offset + 2] = 143 + 103 * intensity;
        } else {
            image.data[offset] = 52 + 44 * shade;
            image.data[offset + 1] = 70 + 44 * shade;
            image.data[offset + 2] = 51 + 20 * shade;
        }
        image.data[offset + 3] = 255;
    }

    context.putImageData(image, 0, 0);
}

function requireState() {
    if (!state) {
        throw new Error("WebGPU is not initialized.");
    }
}
