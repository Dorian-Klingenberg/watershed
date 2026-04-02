# D3D12 Renderer Module

> RAII-based Direct3D12 graphics abstraction for `Granny's House Trials`.

## Status

**Phase 1: Architecture & Documentation**

✓ Architecture document complete  
✓ CMake integration planned  
⏳ Header declarations (Phase 2)  
⏳ Implementations (Phase 2)  
⏳ Unit tests (Phase 2)  
⏳ grass-field-001 refactor (Phase 3)  

## Quick Start

See [ARCHITECTURE.md](./ARCHITECTURE.md) for full design and API reference.

### High-Level Overview

```cpp
// Create device context
D3D12Context graphics(hwnd, 1280, 720);

// Load shaders and create pipeline
PipelineBuilder builder;
auto pso = builder.build_pipeline(graphics.device(), "terrain", {...});

// Allocate GPU buffers
GPUBuffer<TerrainCell> terrain(graphics.device(), cell_count);

// Main render loop
while (running) {
    GraphicsFrame frame(&graphics, graphics.current_frame_index());
    frame.begin();
    
    terrain.upload(frame.command_list());
    // ... render commands ...
    
    frame.end();
    frame.execute();
    graphics.present();
}
```

## Design Principles

**RAII-First**: Stack-based resource ownership. No manual cleanup.

**Move-Only**: Copy constructors deleted. Move semantics supported.

**Exception-Safe**: Strong guarantee on initialization. Throws on failure.

**Windows-Native**: COM integration via `WRL::ComPtr<>`. Direct3D12 features only.

## Key Classes

| Class | Purpose |
|-------|---------|
| `D3D12Context` | Device, swap chain, frame synchronization |
| `GraphicsFrame` | Per-frame command list and barriers |
| `DeviceResources` | GPU buffer allocation and upload |
| `GPUBuffer<T>` | Typed structured buffers with staging |
| `PipelineBuilder` | Shader loading and pipeline caching |

## Integration Points

Currently integrated into: `modules/gfx/`  
Build target: `grannys_house_trials::core` (Phase 1)  
Consumers: `grass-field-001` (to refactor), `grass-field-002` (future)  

## Next Steps

**Phase 2**: Implement core classes and unit tests.  
**Phase 3**: Refactor grass-field-001 to use module.  
**Phase 4**: grass-field-002 adoption and performance profiling.

---

See [ARCHITECTURE.md](./ARCHITECTURE.md) for complete documentation.
