// GPU-resident fluid raycast view.
// Reuse the established raycast implementation, replacing its CPU-composed
// field-cell SRV with separate compute-owned terrain and water SRVs.

#define GPU_RESIDENT_FLUID
#include "grass_field_renderer.hlsl"
