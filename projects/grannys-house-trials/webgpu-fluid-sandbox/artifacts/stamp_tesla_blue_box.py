"""Build the playable Tesla terrain from the reviewed 96x64 bitmap preview."""
import json
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
PREVIEW = ROOT / "wwwroot/artifacts/tesla-blue-box-stamp-96x64.png"
MAP_JSON = ROOT / "artifacts/tesla-valve-map.json"
MAP_PNG = ROOT / "wwwroot/tesla-valve-layout.png"
SOURCE_W, SOURCE_H = 96, 64
WIDTH, HEIGHT = 192, 128

# The review bitmap is a plotted 96x64 cell map. Sample the center of each cell;
# black is channel and white is ground. These bounds describe its plotted grid.
image = Image.open(PREVIEW).convert("L")
left, top, right, bottom = 65, 60, 1518, 848
cell_w, cell_h = (right - left) / SOURCE_W, (bottom - top) / SOURCE_H
mask = np.zeros((SOURCE_H, SOURCE_W), dtype=bool)
for y in range(SOURCE_H):
    for x in range(SOURCE_W):
        px = round(left + (x + 0.5) * cell_w)
        py = round(top + (y + 0.5) * cell_h)
        mask[y, x] = image.getpixel((px, py)) < 80

# The reviewed drawing has tiny one- and two-cell raster gaps at adjoining
# passage seams. Join those seams and the two ends of each return branch only;
# leave the clear spaces between neighboring legs open.
for x, y in [
    (14, 33),                         # first trunk seam
    (44, 36), (45, 36), (47, 36),    # middle trunk seam
    (75, 33), (76, 33), (77, 33),    # final trunk seam
    (19, 39), (29, 39),              # first return branch joins
    (47, 39), (58, 39),              # second return branch joins
    (78, 39), (86, 39), (87, 39),    # final return branch joins
]:
    mask[y, x] = True
# A lone raster speck adjacent to the middle seam is not part of the passage.
mask[34, 46] = False
mask[40, 74] = False

# Upsample every approved source cell uniformly to the app's native 192x128
# grid. This keeps the bitmap topology and aligns ports with the app controls.
channel = np.repeat(np.repeat(mask, 2, axis=0), 2, axis=1)
if channel.shape != (HEIGHT, WIDTH):
    raise RuntimeError("The stamped channel has the wrong application dimensions.")

# Ensure the channel graph is connected before grading it.
seen = set()
components = 0
for y, x in zip(*np.nonzero(channel)):
    start = (int(y), int(x))
    if start in seen:
        continue
    components += 1
    seen.add(start)
    queue = deque([start])
    while queue:
        cy, cx = queue.popleft()
        for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
            if 0 <= ny < HEIGHT and 0 <= nx < WIDTH and channel[ny, nx] and (ny, nx) not in seen:
                seen.add((ny, nx))
                queue.append((ny, nx))
if components != 1:
    raise RuntimeError(f"The stamped channel has {components} disconnected components.")

rows, columns = np.indices((HEIGHT, WIDTH))
left_port = channel & (columns <= 9)
right_port = channel & (columns >= WIDTH - 10)
if not left_port.any() or not right_port.any():
    raise RuntimeError("The channel must reach both left and right ports.")
fixed = left_port | right_port
interior = channel & ~fixed
bed = np.full((HEIGHT, WIDTH), 48.0, dtype=np.float64)
bed[left_port] = 24.0
bed[right_port] = 0.0
bed[interior] = 24.0 * np.clip((WIDTH - 10 - columns[interior]) / (WIDTH - 19), 0, 1)
parity = (rows + columns) & 1

# Discrete Laplace relaxation shares one bed height at every branch/merge.
# Longer routes distribute the same head drop across more channel cells.
for iteration in range(50_000):
    largest_change = 0.0
    for color in (0, 1):
        total = np.zeros_like(bed)
        count = np.zeros_like(bed)
        total[1:, :] += bed[:-1, :] * channel[:-1, :]
        count[1:, :] += channel[:-1, :]
        total[:-1, :] += bed[1:, :] * channel[1:, :]
        count[:-1, :] += channel[1:, :]
        total[:, 1:] += bed[:, :-1] * channel[:, :-1]
        count[:, 1:] += channel[:, :-1]
        total[:, :-1] += bed[:, 1:] * channel[:, 1:]
        count[:, :-1] += channel[:, 1:]
        active = interior & (parity == color) & (count > 0)
        target = np.zeros_like(bed)
        target[active] = total[active] / count[active]
        updated = bed[active] + 1.7 * (target[active] - bed[active])
        if updated.size:
            largest_change = max(largest_change, float(np.max(np.abs(updated - bed[active]))))
            bed[active] = updated
    if largest_change < 1e-5:
        break
else:
    raise RuntimeError("The channel bed grade did not converge.")

rounded = np.where(channel, np.rint(bed), 48).astype(np.int32)
map_data = {
    "Version": 1,
    "Name": "tesla-blue-box-stamp-graded",
    "Width": WIDTH,
    "Height": HEIGHT,
    "SourceBitmap": "wwwroot/artifacts/tesla-blue-box-stamp-96x64.png",
    "SourceGrid": "96x64, uniformly upsampled 2x to the app grid",
    "ElevationMethod": "24in left port, 0in right port, shared discrete-Laplace channel solve, banks 48in",
    "ElevationInches": rounded.reshape(-1).tolist(),
    "ChannelMask": channel.reshape(-1).tolist(),
}
MAP_JSON.write_text(json.dumps(map_data, separators=(",", ":")) + "\n")

# Render the actual app grid in grayscale: darker channel beds are lower.
scale, margin_x, margin_y = 7, 90, 76
map_w, map_h = WIDTH * scale, HEIGHT * scale
canvas = Image.new("RGB", (map_w + 2 * margin_x, map_h + margin_y + 92), "white")
draw = ImageDraw.Draw(canvas)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 17)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
except OSError:
    font = small = ImageFont.load_default()
draw.text((margin_x, 25), "Tesla channel terrain | 192 x 128 cells | 1 ft per cell", fill="black", font=font)
plot = Image.fromarray(np.repeat(np.repeat(np.uint8(35 + rounded * (210 / 48)), scale, axis=0), scale, axis=1), "L").convert("RGB")
canvas.paste(plot, (margin_x, margin_y))
for x in range(WIDTH + 1):
    px = margin_x + x * scale
    color = (70, 70, 70) if x % 16 == 0 else (175, 175, 175)
    draw.line((px, margin_y, px, margin_y + map_h), fill=color, width=1)
for y in range(HEIGHT + 1):
    py = margin_y + y * scale
    color = (70, 70, 70) if y % 16 == 0 else (175, 175, 175)
    draw.line((margin_x, py, margin_x + map_w, py), fill=color, width=1)
for x in range(0, WIDTH, 16):
    draw.text((margin_x + x * scale, margin_y - 22), str(x), fill="black", font=small)
for y in range(0, HEIGHT, 16):
    draw.text((margin_x - 35, margin_y + y * scale - 7), str(y), fill="black", font=small)
legend_y = margin_y + map_h + 24
draw.text((margin_x, legend_y), "Dark = low bed (0 in outlet)     Mid gray = inlet (24 in)     Light = raised banks (48 in)", fill="black", font=small)
canvas.save(MAP_PNG)

edges = []
for y, x in zip(*np.nonzero(channel)):
    if x + 1 < WIDTH and channel[y, x + 1]:
        edges.append(abs(int(rounded[y, x]) - int(rounded[y, x + 1])))
    if y + 1 < HEIGHT and channel[y + 1, x]:
        edges.append(abs(int(rounded[y, x]) - int(rounded[y + 1, x])))
print(f"Generated {int(channel.sum())} connected channel cells; {iteration + 1} relaxation sweeps; max neighboring bed change {max(edges)} in.")
