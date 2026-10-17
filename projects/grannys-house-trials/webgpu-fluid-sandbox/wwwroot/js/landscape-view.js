// Orthographic column presentation only; the .NET model owns every domain edit.
const heightScale = 0.35;
const clickDistance = 5;
let state = null;

const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

function snapshot(values, count) {
    if (!values || values.length !== count) {
        throw new Error(`Landscape snapshots require ${count} cells.`);
    }
    return Float32Array.from(values, value => Number.isFinite(value) ? value : 0);
}

export function initialize(cpuCanvas, gpuCanvas, scrollbar, dotnet, width, height, terrain, water) {
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
        width, height, dotnet, scrollbar, ground,
        base: (minimum - 2) * heightScale,
        camera: {
            x: width / 2, y: height / 2, z: (minimum + maximum) * heightScale / 2,
            azimuth: -0.6, elevation: 0.85, zoom: 1, anchorX: 0.5, anchorY: 0.5
        },
        span: Math.max(width, height) + (maximum - minimum) * heightScale,
        views: [], listeners: [], frame: null, queue: Promise.resolve(),
        selected: null, hover: null, gesture: null, disposed: false
    };
    for (const canvas of [cpuCanvas, gpuCanvas]) {
        const context = canvas.getContext("2d");
        if (!context) throw new Error("Landscape presentation requires a Canvas2D canvas.");
        s.views.push({
            canvas, context, water: initialWater.slice(), width: 1, height: 1,
            oldTouchAction: canvas.style.touchAction
        });
    }
    state = s;
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
    s.observer = new ResizeObserver(() => schedule(s));
    for (const view of s.views) s.observer.observe(view.canvas);
    schedule(s);
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
    const ground = snapshot(terrain, count);
    const depths = snapshot(water, count);
    s.ground = ground;
    s.views[index].water = depths;
    for (const value of ground) s.base = Math.min(s.base, (value - 2) * heightScale);
    schedule(s);
}

function listen(s, target, type, handler, options) {
    target.addEventListener(type, handler, options);
    s.listeners.push(() => target.removeEventListener(type, handler, options));
}

function schedule(s) {
    if (s.disposed || s.frame !== null) return;
    s.frame = requestAnimationFrame(() => {
        s.frame = null;
        if (s.disposed) return;
        for (const view of s.views) draw(s, view);
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
    const faces = [];
    const face = (points, depth, color, index) => {
        if (points.every(p => p[0] < -2) || points.every(p => p[0] > view.width + 2) ||
            points.every(p => p[1] < -2) || points.every(p => p[1] > view.height + 2)) return;
        faces.push({ points, color, index, depth });
    };
    const level = (x, y) => x < 0 || x >= s.width || y < 0 || y >= s.height
        ? s.base : s.ground[y * s.width + x] * heightScale;
    const surface = (x, y) => x < 0 || x >= s.width || y < 0 || y >= s.height
        ? s.base : level(x, y) + Math.max(0, view.water[y * s.width + x]) * heightScale;
    for (let y = 0; y < s.height; ++y) {
        for (let x = 0; x < s.width; ++x) {
            const index = y * s.width + x;
            // A view ray visits disjoint XY columns in ground-depth order,
            // regardless of height. Sorting face centroids in 3D instead can
            // incorrectly draw a distant tall cliff over a nearer low column.
            const depth = -b.sa * (x + 0.5) + b.ca * (y + 0.5);
            const z = level(x, y);
            const waterZ = surface(x, y);
            const tone = clamp(Math.round(110 + s.ground[index] * 1.3), 80, 185);
            const dryColor = `rgb(${tone},${Math.round(tone * 0.92)},${Math.round(tone * 0.65)})`;
            face(top(s, b, x, y, z), depth, dryColor, index);
            // Only camera-facing, exposed cliffs are submitted.
            const sides = [
                { visible: -b.sa < 0, nx: x - 1, ny: y, a: [x, y + 1], c: [x, y], color: "#71634b" },
                { visible: -b.sa >= 0, nx: x + 1, ny: y, a: [x + 1, y], c: [x + 1, y + 1], color: "#8a7855" },
                { visible: b.ca < 0, nx: x, ny: y - 1, a: [x, y], c: [x + 1, y], color: "#6b604b" },
                { visible: b.ca >= 0, nx: x, ny: y + 1, a: [x + 1, y + 1], c: [x, y + 1], color: "#827153" }
            ];
            for (const side of sides) {
                if (!side.visible) continue;
                const bottom = level(side.nx, side.ny);
                const sideFace = (low, high, color) => face([
                    project(s, b, ...side.a, low), project(s, b, ...side.c, low),
                    project(s, b, ...side.c, high), project(s, b, ...side.a, high)
                ], depth, color, null);
                if (bottom < z) sideFace(bottom, z, side.color);
                const waterBottom = Math.max(z, surface(side.nx, side.ny));
                if (waterZ > waterBottom + 0.00035) sideFace(waterBottom, waterZ, "#216a96");
            }
            if (waterZ > z + 0.00035) {
                const waterDepth = Math.max(0, view.water[index]);
                const light = Math.round(54 - Math.min(waterDepth, 18));
                face(top(s, b, x, y, waterZ), depth,
                    `hsl(201 65% ${light}%)`, index);
            }
        }
    }
    faces.sort((a, c) => a.depth - c.depth);
    const preview = s.gesture?.button === 2 && s.gesture.dragged ? s.gesture.cells : null;
    context.lineWidth = 1.5;
    for (const f of faces) {
        polygon(context, f.points, f.color);
        if (f.index === null) continue;
        const color = preview?.has(f.index) ? "#ffb454" :
            s.selected?.index === f.index ? "#fff0a8" :
            s.hover?.index === f.index ? "#baf5ff" : null;
        if (color) polygon(context, f.points, null, color);
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
}

function pointerDown(s, view, event) {
    if (s.gesture || (event.button !== 0 && event.button !== 2) || event.isPrimary === false) return;
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
    s.hover = pick(s, view, local(view, event));
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
            dispatch(s, "pour", gesture.hit, []);
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
    cancelGesture(s);
    if (s.frame !== null) cancelAnimationFrame(s.frame);
    s.observer?.disconnect();
    for (const remove of s.listeners) remove();
    for (const view of s.views) view.canvas.style.touchAction = view.oldTouchAction;
    s.dotnet = null;
}
