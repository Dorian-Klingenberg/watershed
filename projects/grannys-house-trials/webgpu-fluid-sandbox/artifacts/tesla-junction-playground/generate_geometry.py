"""Stamp Tesla's Fig. 1 plan view into the sandbox's 192 x 128 terrain grid.

The image source is the patent plan view reproduced as panel a in
tesla-source-figure.png (Nguyen et al., 2021, CC BY 4.0). The crop is mapped
with one uniform scale: the patent conduit occupies sixteen 1-ft cells across.
The figure is horizontally flipped so patent port 5 (the lower-resistance
direction described in the specification) is the app's left-side inlet.

Only continuous, substantial line-art components are stamped as raised
baffles. Screw holes and the dashed path/arrow annotations are not terrain.
The patent's straight passage is kept open through the central two-cell trunk.
Narrow raster gaps are minimally reopened to retain a connected fluid domain.
This is a coarse geometry imprint, not a claim that the solver reproduces the
patented resistance ratio.
"""

import json
import heapq
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.ndimage import label


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT
ARTIFACTS = APP / "artifacts"
OUT = Path(__file__).resolve().parent
SOURCE = ARTIFACTS / "tesla-source-figure.png"
WIDTH, HEIGHT = 192, 128
BANK_IN = 48
FLOOR_IN = 0
BODY_TOP, BODY_BOTTOM = 56, 71  # sixteen cells overall, including bank lips
OPEN_TOP, OPEN_BOTTOM = 57, 70
MAIN_TOP, MAIN_BOTTOM = 63, 64  # preserve the direct two-cell trunk
SOURCE_BOX = (52, 7, 1272, 120)  # patent Figure 1a, excluding the panel letter


def make_source_mask(crop: Image.Image) -> tuple[np.ndarray, dict]:
    gray = np.asarray(crop.convert("L"))
    ink = gray < 145
    h, w = ink.shape

    # Discard the housing outline, mounting-hole ring marks, and dotted path
    # arrows. The solid bucket/partition outlines remain connected and large.
    ink[:8, :] = False
    ink[-8:, :] = False
    ink[:, :5] = False
    ink[:, -5:] = False
    ink[54:60, :] = False

    labels, count = label(ink, np.ones((3, 3), dtype=np.uint8))
    baffles = np.zeros_like(ink)
    kept_components = []
    for component in range(1, count + 1):
        ys, xs = np.where(labels == component)
        if len(xs) < 250:
            continue
        if ys.min() < 10 or ys.max() > h - 10:
            continue
        baffles[labels == component] = True
        kept_components.append({
            "pixels": int(len(xs)),
            "bounds_source_px": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())],
        })

    return baffles, {
        "crop_pixels": [w, h],
        "kept_baffle_components": kept_components,
        "threshold_gray_below": 145,
        "minimum_component_pixels": 250,
        "removed_center_annotation_band_source_y": [54, 59],
    }


def stamp() -> tuple[np.ndarray, dict, Image.Image]:
    if not SOURCE.exists():
        raise FileNotFoundError(f"Missing patent figure source: {SOURCE}")
    source_image = Image.open(SOURCE).convert("RGB")
    crop = source_image.crop(SOURCE_BOX)
    source_w, source_h = crop.size
    baffle_mask, source_metrics = make_source_mask(crop)

    # Uniform transform: the patent body spans eight 1-ft cells vertically.
    scale = (BODY_BOTTOM - BODY_TOP + 1) / source_h
    body_width = source_w * scale
    x_offset = (WIDTH - body_width) / 2
    y_offset = BODY_TOP

    z = np.full(WIDTH * HEIGHT, BANK_IN, dtype=np.int32).reshape(HEIGHT, WIDTH)
    # The open conduit is lower than the banks. The top and bottom cell rows
    # are the raised side lips; its six clear rows contain the patent geometry.
    z[OPEN_TOP:OPEN_BOTTOM + 1, :] = FLOOR_IN
    # Straight 4-ft central path, extended from each port to the map edge.
    z[MAIN_TOP:MAIN_BOTTOM + 1, :] = FLOOR_IN

    # Resample by source-cell area overlap, so a traced baffle does not vanish
    # merely because a thin scan line falls between target cell centers.
    baffle_cells = np.zeros((HEIGHT, WIDTH), dtype=bool)
    for y in range(OPEN_TOP, OPEN_BOTTOM + 1):
        sy0 = (y - y_offset) / scale
        sy1 = (y + 1 - y_offset) / scale
        iy0 = max(0, math.floor(sy0))
        iy1 = min(source_h, math.ceil(sy1))
        for x in range(WIDTH):
            sx0 = (x - x_offset) / scale
            sx1 = (x + 1 - x_offset) / scale
            ix0 = max(0, math.floor(sx0))
            ix1 = min(source_w, math.ceil(sx1))
            if ix1 <= ix0 or iy1 <= iy0:
                continue
            if baffle_mask[iy0:iy1, ix0:ix1].any():
                baffle_cells[y, x] = True

    # Keep a one-cell direct trunk for the favorable direction. The remaining
    # patent partitions form the alternating hooked returns around it.
    baffle_cells[MAIN_TOP:MAIN_BOTTOM + 1, :] = False
    z[baffle_cells] = BANK_IN

    # Patent port 5 is the favorable-flow inlet; mirror so the app's forward
    # experiment and user's left-to-right pours follow port 5 -> port 4.
    z = np.fliplr(z).copy()
    baffle_cells = np.fliplr(baffle_cells).copy()

    # A one-foot raster can close a patent throat that is visible in the scan.
    # Restore the two end openings of each such pocket to the through-trunk;
    # these are the smallest grid edits that preserve its bypass connection.
    open_components, component_count = label(
        z == FLOOR_IN, np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]], dtype=np.uint8))
    trunk_component = int(open_components[MAIN_TOP, 0])
    opened_gates = []
    for component in range(1, component_count + 1):
        if component == trunk_component:
            continue
        ys, xs = np.where(open_components == component)
        candidates = []
        for y in range(max(OPEN_TOP, int(ys.min()) - 1), min(OPEN_BOTTOM, int(ys.max()) + 1) + 1):
            for x in range(max(0, int(xs.min()) - 1), min(WIDTH - 1, int(xs.max()) + 1) + 1):
                if z[y, x] != BANK_IN:
                    continue
                neighbors = ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))
                adjacent = [open_components[ny, nx] for nx, ny in neighbors
                            if 0 <= nx < WIDTH and 0 <= ny < HEIGHT]
                if component in adjacent and trunk_component in adjacent:
                    candidates.append((x, y))
        candidates = sorted(set(candidates))
        selected = []
        if candidates:
            selected = [candidates[0]]
            farthest = max(candidates, key=lambda cell: abs(cell[0] - selected[0][0]))
            if abs(farthest[0] - selected[0][0]) >= 2:
                selected.append(farthest)
        else:
            # Some very narrow openings need more than one wall cell after
            # coarse sampling. Find the least-destructive 4-neighbor throat.
            start_cells = list(zip(xs.tolist(), ys.tolist()))
            best = {}
            previous = {}
            pending = []
            for sx, sy in start_cells:
                best[(sx, sy)] = (0, 0)
                heapq.heappush(pending, (0, 0, sx, sy))
            target = None
            while pending:
                walls, steps, x, y = heapq.heappop(pending)
                if best[(x, y)] != (walls, steps):
                    continue
                if open_components[y, x] == trunk_component:
                    target = (x, y)
                    break
                for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                    if not (0 <= nx < WIDTH and OPEN_TOP <= ny <= OPEN_BOTTOM):
                        continue
                    cost = (walls + int(z[ny, nx] == BANK_IN), steps + 1)
                    if cost < best.get((nx, ny), (10**9, 10**9)):
                        best[(nx, ny)] = cost
                        previous[(nx, ny)] = (x, y)
                        heapq.heappush(pending, (*cost, nx, ny))
            if target is None:
                raise ValueError(f"Patent pocket component {component} cannot connect to the trunk")
            path = []
            while target in previous:
                target = previous[target]
                path.append(target)
            selected = [(x, y) for x, y in path if z[y, x] == BANK_IN]
        for x, y in selected:
            z[y, x] = FLOOR_IN
            baffle_cells[y, x] = False
            opened_gates.append([int(x), int(y)])

    open_mask = z == FLOOR_IN
    cc, component_count = label(open_mask, np.array([[0, 1, 0], [1, 1, 1], [0, 1, 0]], dtype=np.uint8))
    through_connected = bool(cc[MAIN_TOP, 0] and cc[MAIN_TOP, WIDTH - 1] and cc[MAIN_TOP, 0] == cc[MAIN_TOP, WIDTH - 1])
    if component_count != 1 or not through_connected:
        raise ValueError(f"Stamped open cells must form one connected channel; found {component_count} components")
    widths = [int(open_mask[OPEN_TOP:OPEN_BOTTOM + 1, x].sum()) for x in range(WIDTH)]
    checks = {
        "map": {"width_cells": WIDTH, "height_cells": HEIGHT, "cell_size_ft": 1},
        "source": {
            "image": SOURCE.name,
            "figure": "US1329559A Fig. 1 plan view, reproduced as panel a in Nguyen et al. (2021)",
            "citation": "https://www.nature.com/articles/s41467-021-23009-y",
            "patent": "https://patents.google.com/patent/US1329559A/en",
            "source_crop_xyxy": list(SOURCE_BOX),
            "source_crop_pixels": [source_w, source_h],
            "uniform_scale_cells_per_source_pixel": scale,
            "body_x_cells_unflipped": [x_offset, x_offset + body_width],
            "body_y_cells": [BODY_TOP, BODY_BOTTOM + 1],
            "mirrored_horizontally_for_app_forward": True,
            **source_metrics,
        },
        "terrain": {
            "channel_floor_inches": FLOOR_IN,
            "banks_and_imprinted_baffles_inches": BANK_IN,
        "direct_trunk_rows": [MAIN_TOP, MAIN_BOTTOM],
        "nominal_direct_trunk_width_cells": MAIN_BOTTOM - MAIN_TOP + 1,
            "raster_throats_reopened_xy": opened_gates,
            "cell_widths_open_by_column": widths,
            "mean_open_cells_per_column_within_body": float(np.mean(widths)),
            "maximum_open_cells_in_a_column": max(widths),
            "baffle_cells": int(baffle_cells.sum()),
            "open_cells": int(open_mask.sum()),
            "open_components_4_neighbor": int(component_count),
            "port5_to_port4_connected": through_connected,
        },
    }
    return z, checks, crop.transpose(Image.Transpose.FLIP_LEFT_RIGHT)


def render_map(z: np.ndarray, out_path: Path) -> None:
    cell = 8
    left, top, right, bottom = 74, 62, 28, 84
    image = Image.new("RGB", (left + WIDTH * cell + right, top + HEIGHT * cell + bottom), "white")
    draw = ImageDraw.Draw(image)

    for y in range(HEIGHT):
        for x in range(WIDTH):
            # Darker cells are lower, matching the user's requested raster map.
            value = z[y, x]
            shade = round(28 + 224 * value / BANK_IN)
            x0, y0 = left + x * cell, top + y * cell
            draw.rectangle((x0, y0, x0 + cell - 1, y0 + cell - 1), fill=(shade, shade, shade))

    grid_color = (182, 190, 196)
    for x in range(WIDTH + 1):
        xx = left + x * cell
        draw.line((xx, top, xx, top + HEIGHT * cell), fill=grid_color if x % 8 == 0 else (225, 228, 230), width=1)
    for y in range(HEIGHT + 1):
        yy = top + y * cell
        draw.line((left, yy, left + WIDTH * cell, yy), fill=grid_color if y % 8 == 0 else (225, 228, 230), width=1)

    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 16)
        small = ImageFont.truetype("DejaVuSans.ttf", 13)
    except OSError:
        font = small = ImageFont.load_default()
    draw.text((left, 18), "Tesla patent Fig. 1 imprinted on the 192 x 128 terrain grid", fill=(25, 35, 42), font=font)
    for x in range(0, WIDTH, 8):
        draw.text((left + x * cell - 3, top - 22), str(x), fill=(45, 54, 60), font=small)
    for y in range(0, HEIGHT, 8):
        draw.text((28, top + y * cell - 8), str(y), fill=(45, 54, 60), font=small)
    draw.text((left, top + HEIGHT * cell + 14),
              "Dark = low channel floor (0 in)  |  Light = raised bank / patent baffle (48 in)  |  1 cell = 1 ft",
              fill=(35, 45, 50), font=small)
    draw.text((left, top + HEIGHT * cell + 38),
              "Source plan mirrored: patent port 5 -> port 4 is left-to-right in the app. No arrows are stamped as terrain.",
              fill=(35, 45, 50), font=small)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(out_path)


def main() -> None:
    z, checks, source_plan = stamp()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "terrain.json").write_text(json.dumps({
        "Version": 1,
        "Name": "tesla-patent-planform",
        "Width": WIDTH,
        "Height": HEIGHT,
        "ElevationInches": z.ravel().tolist(),
    }, separators=(",", ":")) + "\n")
    (OUT / "geometry-checks.json").write_text(json.dumps(checks, indent=2) + "\n")
    source_plan.save(OUT / "patent-plan-view.png")
    source_plan.save(APP / "wwwroot" / "tesla-patent-plan-view.png")
    render_map(z, OUT / "map.png")
    render_map(z, APP / "wwwroot" / "tesla-valve-layout.png")
    print(json.dumps(checks["terrain"], indent=2))


if __name__ == "__main__":
    main()
