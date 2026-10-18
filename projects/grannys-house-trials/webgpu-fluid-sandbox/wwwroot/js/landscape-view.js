// Orthographic column presentation only; the .NET model owns every domain edit.
const heightScale = 0.35;
const clickDistance = 5;
let state = null;

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export function waterDepthColor(depth, side = false) {
    const band = Math.floor(Number.isFinite(depth) ? Math.max(0, depth) : 0);
    const light = band === 0 ? 60 : band <= 12 ? 52 - 2 * band
        : band <= 72 ? 28 - 0.3 * (band - 12) : 10 / (1 + 0.02 * (band - 72));
    return `hsl(${201 + Math.min(band * 0.35, 25)} 82% ${(light * (side ? 0.8 : 1)).toFixed(3)}%)`;
}

function snapshot(values, count) {
    if (!values || values.length !== count) {
        throw new Error(`Landscape snapshots require ${count} cells.`);
    }
    if (values instanceof Float32Array) return values;
    return Float32Array.from(values, value => Number.isFinite(value) ? value : 0);
}

export function initialize(cpuCanvas, gpuCanvas, scrollbar, verticalScrollbar, dotnet, width, height, terrain, water) {
    dispose();
    if (!Number.isInteger(width) || !Number.isInteger(height) || width < 1 || height < 1 ||
        width * height > 262144) {
        throw new Error("Invalid landscape dimensions.");
    }
    const count = width * height;
    const ground = snapshot(terrain, count);
    const initialWater = snapshot(water, count);
    let minimum = 0;
    let maximum = 0;
    for (const value of ground) {
        minimum = Math.min(minimum, value);
        maximum = Math.max(maximum, value);
    }
    const s = {
        width, height, dotnet, scrollbar, verticalScrollbar, ground,
        base: (minimum - 2) * heightScale,
        camera: {
            x: width / 2, y: height / 2, z: (minimum + maximum) * heightScale / 2,
            azimuth: -0.6, elevation: 0.85, zoom: 1, anchorX: 0.5, anchorY: 0.5
        },
        span: Math.max(width, height) + (maximum - minimum) * heightScale,
        views: [], listeners: [], frame: null, queue: Promise.resolve(),
        selected: null, hover: null, gesture: null, disposed: false, springs: new Set(),
        order: null, orderAzimuth: null, terrainVersion: 0, metricTime: performance.now(), waits: new Set()
    };
    for (const canvas of [cpuCanvas, gpuCanvas]) {
        const context = canvas.getContext("2d");
        if (!context) throw new Error("Landscape presentation requires a Canvas2D canvas.");
        s.views.push({
            canvas, context, water: initialWater.slice(), width: 1, height: 1,
            oldTouchAction: canvas.style.touchAction, enabled: true, visible: true, dirty: true,
            draws: 0, drawMs: 0, geometry: null
        });
    }
    state = s;
    for (let depth = 0; depth <= 12; depth++) {
        document.documentElement.style.setProperty(`--water-depth-${depth}`, waterDepthColor(depth));
    }
    for (const view of s.views) {
        view.canvas.style.touchAction = "none";
        listen(s, view.canvas, "pointerdown", event => pointerDown(s, view, event));
        listen(s, view.canvas, "pointermove", event => pointerMove(s, view, event));
        listen(s, view.canvas, "pointerup", event => pointerUp(s, view, event));
        listen(s, view.canvas, "pointercancel", event => cancelGesture(s, event.pointerId));
        listen(s, view.canvas, "lostpointercapture", event => cancelGesture(s, event.pointerId));
        listen(s, view.canvas, "pointerleave", () => {
            if (!s.gesture) { s.hover = null; schedule(s); }
        });
        listen(s, view.canvas, "contextmenu", event => event.preventDefault());
        listen(s, view.canvas, "wheel", event => wheel(s, view, event), { passive: false });
    }
    if (scrollbar) {
        scrollbar.min = "0";
        scrollbar.max = String(width);
        scrollbar.step = "0.1";
        syncScrollbar(s);
        listen(s, scrollbar, "input", () => {
            const x = Number(scrollbar.value);
            if (!Number.isFinite(x)) return;
            cancelGesture(s);
            s.camera.x = clamp(x, 0, width);
            s.hover = null;
            schedule(s);
        });
    }
    if (verticalScrollbar) {
        verticalScrollbar.max = String(height);
        verticalScrollbar.value = String(s.camera.y);
        listen(s, verticalScrollbar, "input", () => {
            const y = Number(verticalScrollbar.value);
            if (!Number.isFinite(y)) return;
            cancelGesture(s);
            s.camera.y = clamp(y, 0, height);
            s.hover = null;
            schedule(s);
        });
    }
    s.observer = new ResizeObserver(() => schedule(s));
    for (const view of s.views) s.observer.observe(view.canvas);
    s.visibility = new IntersectionObserver(entries => {
        for (const entry of entries) {
            const view = s.views.find(item => item.canvas === entry.target);
            view.visible = entry.isIntersecting;
        }
        schedule(s);
    });
    for (const view of s.views) s.visibility.observe(view.canvas);
    listen(s, document, "visibilitychange", () => {
        if (document.hidden) cancelWaits(s);
        else schedule(s);
    });
    s.metrics = setInterval(() => reportMetrics(s), 500);
    schedule(s);
}

export function setCpuEnabled(enabled) {
    const s = state;
    if (!s) return;
    s.views[0].enabled = enabled;
    s.views[0].draws = 0;
    s.views[0].drawMs = 0;
    if (!enabled && s.gesture?.view === s.views[0]) cancelGesture(s);
    if (enabled) schedule(s);
}

export function setGpuEnabled(enabled) {
    if (!state) return;
    state.views[1].enabled = enabled;
    if (enabled) schedule(state);
}

export function notifyGpuFailure(message) {
    const s = state;
    if (s && !s.disposed) {
        s.dotnet.invokeMethodAsync("HandleGpuFailure", message)
            .catch(error => { if (!s.disposed) console.warn("GPU recovery failed:", error); });
    }
}

export async function waitForDisplay() {
    const s = state;
    if (!s || s.disposed) return false;
    return await new Promise(resolve => {
        let frame = null, timeout = null, complete = false;
        const finish = visible => {
            if (complete) return;
            complete = true;
            if (frame !== null) cancelAnimationFrame(frame);
            if (timeout !== null) clearTimeout(timeout);
            s.waits.delete(cancel);
            resolve(visible && !s.disposed && !document.hidden);
        };
        const cancel = () => finish(false);
        s.waits.add(cancel);
        if (document.hidden) timeout = setTimeout(cancel, 100);
        else frame = requestAnimationFrame(() => finish(true));
    });
}

function cancelWaits(s) {
    for (const cancel of [...s.waits]) cancel();
}

export function cancelPendingWaits() {
    if (state) cancelWaits(state);
}

function reportMetrics(s) {
    if (s.disposed || s.reporting || document.hidden) return;
    const now = performance.now(), seconds = (now - s.metricTime) / 1000;
    const [cpu, gpu] = s.views;
    const values = [cpu.draws / seconds, gpu.draws / seconds,
        cpu.draws ? cpu.drawMs / cpu.draws : 0, gpu.draws ? gpu.drawMs / gpu.draws : 0];
    for (const view of s.views) { view.draws = 0; view.drawMs = 0; }
    s.metricTime = now;
    const depth = s.selected ? gpu.water[s.selected.index] : null;
    s.reporting = true;
    s.dotnet.invokeMethodAsync("HandlePresentationReadout", ...values, depth)
        .catch(error => { if (!s.disposed) console.warn("Performance readout failed:", error); })
        .finally(() => { s.reporting = false; });
}

export function renderCpu(terrain, water) {
    update(0, terrain, water);
}

export function renderGpu(terrain, water) {
    update(1, terrain, water);
}

function update(index, terrain, water) {
    const s = state;
    if (!s || s.disposed) return;
    const count = s.width * s.height;
    const depths = snapshot(water, count);
    const view = s.views[index];
    const waterChanged = depths.some((value, cell) => value !== view.water[cell]);
    if (terrain) {
        s.ground = snapshot(terrain, count);
        s.terrainVersion++;
        for (const value of s.ground) s.base = Math.min(s.base, (value - 2) * heightScale);
        for (const view of s.views) view.dirty = true;
    }
    view.water = depths;
    if (!terrain && !waterChanged) return;
    view.dirty = true;
    schedule(s, false);
}

function listen(s, target, type, handler, options) {
    target.addEventListener(type, handler, options);
    s.listeners.push(() => target.removeEventListener(type, handler, options));
}

function schedule(s, all = true) {
    if (all) for (const view of s.views) view.dirty = true;
    if (s.disposed || s.frame !== null) return;
    s.frame = requestAnimationFrame(() => {
        s.frame = null;
        if (s.disposed) return;
        if (document.hidden) return;
        for (const view of s.views) {
            if (!view.enabled || !view.visible || !view.dirty || view.canvas.closest("[hidden]")) continue;
            const start = performance.now();
            draw(s, view);
            view.drawMs += performance.now() - start;
            view.draws++;
            view.dirty = false;
        }
    });
}

function basis(s, view) {
    const c = s.camera;
    const ca = Math.cos(c.azimuth), sa = Math.sin(c.azimuth);
    const se = Math.sin(c.elevation), ce = Math.cos(c.elevation);
    const scale = Math.min(view.width, view.height * 1.45) * 0.88 / s.span * c.zoom;
    return { ca, sa, se, ce, scale, ox: view.width * c.anchorX, oy: view.height * c.anchorY };
}

function project(s, b, x, y, z) {
    const dx = x - s.camera.x, dy = y - s.camera.y, dz = z - s.camera.z;
    const depth = -b.sa * dx + b.ca * dy;
    return [
        b.ox + (b.ca * dx + b.sa * dy) * b.scale,
        b.oy + (b.se * depth - b.ce * dz) * b.scale
    ];
}

function polygon(context, points, fill, stroke) {
    context.beginPath();
    context.moveTo(points[0][0], points[0][1]);
    for (let i = 1; i < points.length; ++i) context.lineTo(points[i][0], points[i][1]);
    context.closePath();
    if (fill) { context.fillStyle = fill; context.fill(); }
    if (stroke) { context.strokeStyle = stroke; context.stroke(); }
}

function top(s, b, x, y, z) {
    return [
        project(s, b, x, y, z), project(s, b, x + 1, y, z),
        project(s, b, x + 1, y + 1, z), project(s, b, x, y + 1, z)
    ];
}

export function setSprings(sources) {
    if (!state || state.disposed) return;
    state.springs = new Set(sources.map(source => source.y * state.width + source.x));
    schedule(state);
}

function draw(s, view) {
    const rect = view.canvas.getBoundingClientRect();
    view.width = Math.max(1, rect.width);
    view.height = Math.max(1, rect.height);
    // Cap backing-store resolution; simulation snapshots never drive an animation loop.
    const ratio = Math.min(globalThis.devicePixelRatio || 1, 2);
    const pixelWidth = Math.max(1, Math.round(view.width * ratio));
    const pixelHeight = Math.max(1, Math.round(view.height * ratio));
    if (view.canvas.width !== pixelWidth) view.canvas.width = pixelWidth;
    if (view.canvas.height !== pixelHeight) view.canvas.height = pixelHeight;
    const context = view.context;
    context.setTransform(ratio, 0, 0, ratio, 0, 0);
    context.fillStyle = "#111e29";
    context.fillRect(0, 0, view.width, view.height);
    const b = basis(s, view);
    const geometryKey = JSON.stringify([view.width, view.height, s.camera, s.terrainVersion]);
    if (view.geometryKey !== geometryKey) {
        view.geometryKey = geometryKey;
        view.geometry = new Array(s.width * s.height);
    }
    if (s.orderAzimuth !== s.camera.azimuth) {
        s.orderAzimuth = s.camera.azimuth;
        s.order = Array.from({ length: s.width * s.height }, (_, index) => index);
        const depth = index => -b.sa * (index % s.width) + b.ca * Math.floor(index / s.width);
        s.order.sort((a, c) => depth(a) - depth(c));
    }
    const preview = s.gesture?.button === 2 && s.gesture.dragged ? s.gesture.cells : null;
    context.lineWidth = 1.5;
    const face = (points, color, index) => {
        if (points.every(p => p[0] < -2) || points.every(p => p[0] > view.width + 2) ||
            points.every(p => p[1] < -2) || points.every(p => p[1] > view.height + 2)) return;
        polygon(context, points, color);
        if (index === null) return;
        const outline = preview?.has(index) ? "#ffb454" :
            s.selected?.index === index ? "#fff0a8" :
            s.hover?.index === index ? "#baf5ff" : null;
        if (outline) polygon(context, points, null, outline);
        if (s.springs.has(index)) {
            polygon(context, points, null, "#60ffc5");
            const center = points.reduce((sum, p) => [sum[0] + p[0] / points.length, sum[1] + p[1] / points.length], [0, 0]);
            context.beginPath(); context.arc(center[0], center[1], 3, 0, Math.PI * 2);
            context.fillStyle = "#60ffc5"; context.fill();
        }
    };
    const level = (x, y) => x < 0 || x >= s.width || y < 0 || y >= s.height
        ? s.base : s.ground[y * s.width + x] * heightScale;
    const surface = (x, y) => x < 0 || x >= s.width || y < 0 || y >= s.height
        ? s.base : level(x, y) + Math.max(0, view.water[y * s.width + x]) * heightScale;
    for (const index of s.order) {
            const x = index % s.width, y = Math.floor(index / s.width);
            // A view ray visits disjoint XY columns in ground-depth order,
            // regardless of height. Sorting face centroids in 3D instead can
            // incorrectly draw a distant tall cliff over a nearer low column.
            const z = level(x, y);
            const waterZ = surface(x, y);
            let geometry = view.geometry[index];
            if (!geometry) {
                const tone = clamp(Math.round(110 + s.ground[index] * 1.3), 80, 185);
                const sides = [
                { visible: -b.sa < 0, nx: x - 1, ny: y, a: [x, y + 1], c: [x, y], color: "#71634b" },
                { visible: -b.sa >= 0, nx: x + 1, ny: y, a: [x + 1, y], c: [x + 1, y + 1], color: "#8a7855" },
                { visible: b.ca < 0, nx: x, ny: y - 1, a: [x, y], c: [x + 1, y], color: "#6b604b" },
                { visible: b.ca >= 0, nx: x, ny: y + 1, a: [x + 1, y + 1], c: [x, y + 1], color: "#827153" }
                ].filter(side => side.visible);
                for (const side of sides) {
                    side.bottom = level(side.nx, side.ny);
                    if (side.bottom < z) side.points = [
                        project(s, b, ...side.a, side.bottom), project(s, b, ...side.c, side.bottom),
                        project(s, b, ...side.c, z), project(s, b, ...side.a, z)
                    ];
                }
                geometry = view.geometry[index] = {
                    top: top(s, b, x, y, z), sides,
                    color: `rgb(${tone},${Math.round(tone * 0.92)},${Math.round(tone * 0.65)})`
                };
            }
            face(geometry.top, geometry.color, index);
            for (const side of geometry.sides) {
                const sideFace = (low, high, color) => face([
                    project(s, b, ...side.a, low), project(s, b, ...side.c, low),
                    project(s, b, ...side.c, high), project(s, b, ...side.a, high)
                ], color, null);
                if (side.points) face(side.points, side.color, null);
                const waterBottom = Math.max(z, surface(side.nx, side.ny));
                if (waterZ > waterBottom + 0.00035) {
                    sideFace(waterBottom, waterZ, waterDepthColor(view.water[index], true));
                }
            }
            if (waterZ > z + 0.00035) {
                const waterDepth = Math.max(0, view.water[index]);
                const points = top(s, b, x, y, waterZ);
                face(points, waterDepthColor(waterDepth), index);
                // Depth contours remain readable when adjacent deep blues look similar.
                const band = Math.floor(waterDepth);
                const neighbors = [y > 0 ? index - s.width : -1,
                    x + 1 < s.width ? index + 1 : -1,
                    y + 1 < s.height ? index + s.width : -1,
                    x > 0 ? index - 1 : -1];
                let contour = false;
                for (let edge = 0; edge < 4; edge++) {
                    const neighbor = neighbors[edge];
                    if (neighbor < 0 || view.water[neighbor] <= 0.001 ||
                        Math.floor(view.water[neighbor]) === band) continue;
                    if (!contour) { context.beginPath(); contour = true; }
                    context.moveTo(...points[edge]);
                    context.lineTo(...points[(edge + 1) % 4]);
                }
                if (contour) {
                    context.strokeStyle = "rgba(135, 205, 255, 0.55)";
                    context.lineWidth = 0.7;
                    context.stroke();
                    context.lineWidth = 1.5;
                }
                if (preview?.has(index) || s.selected?.index === index || s.hover?.index === index) {
                    const outline = preview?.has(index) ? "#ffb454" :
                        s.selected?.index === index ? "#fff0a8" : "#baf5ff";
                    polygon(context, points, null, outline);
                }
            }
    }
}

function local(view, event) {
    const rect = view.canvas.getBoundingClientRect();
    return { x: event.clientX - rect.left, y: event.clientY - rect.top };
}

function pick(s, view, point) {
    const b = basis(s, view);
    const u = (point.x - b.ox) / b.scale;
    const v = (point.y - b.oy) / b.scale;
    const forward = [-b.sa * b.ce, b.ca * b.ce, b.se];
    const origin = [
        s.camera.x + b.ca * u - b.sa * b.se * v,
        s.camera.y + b.sa * u + b.ca * b.se * v,
        s.camera.z - b.ce * v
    ];
    // Intersect the real terrain columns, not the screen's ground-plane footprint.
    // A line parameter increasing toward the camera makes the largest exit the visible hit.
    let nearest = -Infinity;
    let hit = null;
    for (let index = 0; index < s.ground.length; ++index) {
        const x = index % s.width, y = Math.floor(index / s.width);
        const lower = [x, y, s.base];
        const upper = [x + 1, y + 1, s.ground[index] * heightScale];
        let enter = -Infinity, exit = Infinity;
        for (let axis = 0; axis < 3 && enter <= exit; ++axis) {
            if (Math.abs(forward[axis]) < 1e-10) {
                if (origin[axis] < lower[axis] || origin[axis] > upper[axis]) enter = Infinity;
            } else {
                const a = (lower[axis] - origin[axis]) / forward[axis];
                const c = (upper[axis] - origin[axis]) / forward[axis];
                enter = Math.max(enter, Math.min(a, c));
                exit = Math.min(exit, Math.max(a, c));
            }
        }
        if (enter <= exit && exit > nearest) {
            nearest = exit;
            hit = {
                index, x, y,
                point: origin.map((coordinate, axis) => coordinate + forward[axis] * exit)
            };
        }
    }
    return hit;
}

function anchor(s, view, point, hit) {
    [s.camera.x, s.camera.y, s.camera.z] = hit.point;
    s.camera.anchorX = point.x / view.width;
    s.camera.anchorY = point.y / view.height;
    syncScrollbar(s);
}

function syncScrollbar(s) {
    if (s.scrollbar) s.scrollbar.value = String(clamp(s.camera.x, 0, s.width));
    if (s.verticalScrollbar) s.verticalScrollbar.value = String(clamp(s.camera.y, 0, s.height));
}

function pointerDown(s, view, event) {
    if (!view.enabled || s.gesture || (event.button !== 0 && event.button !== 2) || event.isPrimary === false) return;
    const point = local(view, event);
    const hit = pick(s, view, point);
    if (!hit) return;
    event.preventDefault();
    s.gesture = {
        view, pointerId: event.pointerId, button: event.button, start: point, last: point,
        hit, dragged: false, azimuth: s.camera.azimuth, elevation: s.camera.elevation,
        cells: new Set([hit.index]), lastHit: hit
    };
    view.canvas.setPointerCapture(event.pointerId);
    s.hover = hit;
    schedule(s);
}

function moveGesture(s, view, event) {
    const gesture = s.gesture;
    if (!gesture || gesture.view !== view || gesture.pointerId !== event.pointerId) return;
    const point = local(view, event);
    if (!gesture.dragged && Math.hypot(point.x - gesture.start.x, point.y - gesture.start.y) > clickDistance) {
        gesture.dragged = true;
        if (gesture.button === 0) anchor(s, view, gesture.start, gesture.hit);
    }
    if (gesture.dragged && gesture.button === 0) {
        s.camera.azimuth = gesture.azimuth + (point.x - gesture.start.x) * 0.008;
        s.camera.elevation = clamp(gesture.elevation + (point.y - gesture.start.y) * 0.006, 0.25, 1.4);
        s.hover = gesture.hit;
    } else if (gesture.dragged) {
        // Sample between pointer events, then connect neighboring picks into a cell stroke.
        const distance = Math.hypot(point.x - gesture.last.x, point.y - gesture.last.y);
        const steps = clamp(Math.ceil(distance / Math.max(2, basis(s, view).scale / 2)), 1, 32);
        for (let i = 1; i <= steps; ++i) {
            const t = i / steps;
            const hit = pick(s, view, {
                x: gesture.last.x + (point.x - gesture.last.x) * t,
                y: gesture.last.y + (point.y - gesture.last.y) * t
            });
            if (!hit) { gesture.lastHit = null; continue; }
            if (gesture.lastHit) addStroke(s, gesture.cells, gesture.lastHit, hit);
            else gesture.cells.add(hit.index);
            gesture.lastHit = hit;
            s.hover = hit;
        }
    }
    gesture.last = point;
    schedule(s);
}

function addStroke(s, cells, from, to) {
    const steps = Math.max(Math.abs(to.x - from.x), Math.abs(to.y - from.y), 1);
    for (let i = 0; i <= steps; ++i) {
        const x = Math.round(from.x + (to.x - from.x) * i / steps);
        const y = Math.round(from.y + (to.y - from.y) * i / steps);
        cells.add(y * s.width + x);
    }
}

function pointerMove(s, view, event) {
    if (s.gesture) {
        if (s.gesture.pointerId === event.pointerId) moveGesture(s, view, event);
        return;
    }
    const hit = pick(s, view, local(view, event));
    if (s.hover?.index === hit?.index) return;
    s.hover = hit;
    schedule(s);
}

function pointerUp(s, view, event) {
    const gesture = s.gesture;
    if (!gesture || gesture.view !== view || gesture.pointerId !== event.pointerId) return;
    moveGesture(s, view, event);
    s.gesture = null;
    if (view.canvas.hasPointerCapture(event.pointerId)) view.canvas.releasePointerCapture(event.pointerId);
    if (gesture.dragged && gesture.button === 2) {
        dispatch(s, "barrier", gesture.hit, Array.from(gesture.cells));
    } else if (!gesture.dragged) {
        if (gesture.button === 0) {
            dispatch(s, "select", gesture.hit, []);
        } else {
            dispatch(s, "spring", gesture.hit, []);
        }
    }
    s.hover = pick(s, view, local(view, event));
    schedule(s);
}

function cancelGesture(s, pointerId) {
    const gesture = s.gesture;
    if (!gesture || (pointerId !== undefined && gesture.pointerId !== pointerId)) return;
    s.gesture = null;
    if (gesture.view.canvas.hasPointerCapture(gesture.pointerId)) {
        gesture.view.canvas.releasePointerCapture(gesture.pointerId);
    }
    s.hover = null;
    schedule(s);
}

function wheel(s, view, event) {
    if (s.gesture) return;
    const point = local(view, event);
    const hit = pick(s, view, point);
    if (!hit) return;
    event.preventDefault();
    anchor(s, view, point, hit);
    const pixels = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? view.height : 1);
    s.camera.zoom = clamp(s.camera.zoom * Math.exp(-clamp(pixels, -500, 500) * 0.0015), 0.4, 12);
    s.hover = hit;
    schedule(s);
}

function dispatch(s, action, hit, cells) {
    s.selected = hit;
    s.queue = s.queue.then(async () => {
        if (!s.disposed) await s.dotnet.invokeMethodAsync("HandleLandscapeAction", action, hit.x, hit.y, cells);
    }).catch(error => {
        if (!s.disposed) console.warn(`Landscape ${action} callback failed:`, error);
    });
}

export function dispose() {
    const s = state;
    if (!s) return;
    state = null;
    s.disposed = true;
    cancelWaits(s);
    cancelGesture(s);
    if (s.frame !== null) cancelAnimationFrame(s.frame);
    s.observer?.disconnect();
    s.visibility?.disconnect();
    clearInterval(s.metrics);
    for (const remove of s.listeners) remove();
    for (const view of s.views) view.canvas.style.touchAction = view.oldTouchAction;
    s.dotnet = null;
}
