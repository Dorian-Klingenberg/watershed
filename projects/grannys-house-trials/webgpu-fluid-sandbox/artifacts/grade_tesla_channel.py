"""Apply a smooth 24-to-0 inch bed grade to the stamped Tesla channel network."""
import json
from pathlib import Path

import numpy as np

path = Path(__file__).with_name("tesla-valve-map.json")
map_data = json.loads(path.read_text())
width, height = map_data["Width"], map_data["Height"]
if "ChannelMask" in map_data:
    channel = np.asarray(map_data["ChannelMask"], dtype=bool).reshape(height, width)
else:
    channel = (np.asarray(map_data["ElevationInches"]).reshape(height, width) == 0)

rows, columns = np.indices((height, width))
fixed = channel & ((columns <= 9) | (columns >= width - 10))
bed = np.zeros((height, width), dtype=np.float64)
bed[channel & (columns <= 9)] = 24.0
bed[channel & (columns >= width - 10)] = 0.0
interior = channel & ~fixed
bed[interior] = 24.0 * np.clip((width - 10 - columns[interior]) / (width - 19), 0, 1)
parity = (rows + columns) & 1

# Solve the discrete Laplace equation on the channel cells. At a branch and
# merge, the shared junction gets one elevation; the longer route distributes
# that same head drop over more cells and therefore has a gentler grade.
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

mask = channel.reshape(-1).tolist()
elevations = np.where(channel, np.rint(bed), 48).astype(np.int32).reshape(-1).tolist()
map_data["Name"] = "tesla-blue-route-graded"
map_data["ChannelMask"] = mask
map_data["ElevationInches"] = elevations
path.write_text(json.dumps(map_data, separators=(",", ":")) + "\n")
print(f"Graded {len(mask) - mask.count(False)} channel cells in {iteration + 1} sweeps.")
