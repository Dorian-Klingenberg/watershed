# D3D12 Graphics Renderer Module

## Overview

This module provides a robust, RAII-based Direct3D12 rendering abstraction extracted from `grass-field-001`. It encapsulates device management, frame synchronization, GPU buffer allocation, and pipeline state creation while maintaining Windows integration points at the module boundary.

**Module path**: `modules/gfx/d3d12_renderer/`

**Stability**: Experimental (Phase 1: Core extraction)

**Primary consumers**: `grass-field-001` (refactor), `grass-field-002` (future), new subprojects

---

## Design Philosophy

### RAII-First Approach

All GPU resources are managed through stack-based lifetime ownership, with automatic cleanup guaranteed even under exception conditions.

**Core properties**:
- **No naked pointers to GPU resources**: All COM objects managed via `WRL::ComPtr<>`
- **Constructor initialization**: All resources created in constructor; throws if any step fails
- **Destructor cleanup**: Automatic GPU synchronization and resource release
- **Move-only semantics**: Copy constructors deleted; move constructors/assignment allowed
- **Exception safety**: Strong guarantee for initialization; basic for rendering operations

**Example pattern**:
```cpp
// Stack-based ownership: resources live for scope lifetime
{
    D3D12Context ctx(hwnd, width, height); // Creates all resources or throws
    
    while (rendering) {
        GraphicsFrame frame(&ctx, frame_index);
        frame.begin();                          // Record commands
        device_resources.upload(frame.command_list());
        frame.transition_to_render_target();
        // ... render calls ...
        frame.end();                            // Auto-transitions to present
    }
    
    ctx.wait_for_gpu();                        // Explicit wait before scope exit
} // ctx and all resources automatically cleaned up here
```

### Domain Separation

**This module is pure graphics**: Device management, buffer allocation, command list recording, pipeline creation. It is **not** responsible for:
- Simulation state
- Terrain data representation
- Shader logic (only loading pre-compiled blobs)
- UI framework (only command-list recording)

**Consumers** (subprojects like grass-field-001) provide:
- Scene constant marshaling
- Terrain data binding
- Shader compilation (offline, produces `.cso` files)
- UI integration

---

## Architecture

### Class Hierarchy

```
D3D12Context (Root Device Manager)
├── Device, Factory, Swap Chain, Command Queue
├── Per-Frame Resources (for N frames in flight)
└── Fence + Synchronization

GraphicsFrame (Per-Frame State)
├── Command List (bound to frame)
├── Barrier Recording
└── Presentable State

DeviceResources (GPU Buffer Management)
├── GPUBuffer<T> (Structured Typed Buffers)
├── Staging Buffers (CPU→GPU Upload)
└── Capacity Tracking

PipelineBuilder (State Compilation)
├── Root Signature Creation
├── Compiled Shader Loading
├── PSO Creation + Caching
└── Error Reporting

RenderConstants (Shared Data Types)
├── SceneConstants (uniform buffer layout)
└── Frame Metadata
```

### Core Classes

#### 1. **D3D12Context**

**Responsibility**: Own the D3D12 device, swap chain, command infrastructure, and frame synchronization.

**Key invariants**:
- Device is valid from constructor to destructor
- Swap chain size matches window size (caller must handle WM_SIZE)
- GPU is explicitly waited via `wait_for_gpu()` before destruction
- Frame index advances predictably: 0, 1, ..., (frame_count-1), 0, 1, ...

**Public interface**:
```cpp
class D3D12Context {
public:
    // Constructor: throws on any D3D12 failure
    explicit D3D12Context(HWND hwnd, uint32_t width, uint32_t height,
        uint32_t frame_count = 2);
    
    // Lifecycle
    void wait_for_gpu();                        // Block CPU until GPU idle
    void present(uint32_t sync_interval = 1);  // Flip back-buffer + advance frame
    
    // Access (const, no-throw)
    ID3D12Device* device() const;
    ID3D12CommandQueue* command_queue() const;
    ID3D12GraphicsCommandList* command_list() const;
    ID3D12CommandAllocator* command_allocator(uint32_t frame_idx) const;
    
    // Frame State
    uint32_t current_frame_index() const;
    uint64_t total_frames_presented() const;
    ID3D12Resource* current_render_target() const;
    D3D12_CPU_DESCRIPTOR_HANDLE current_rtv_handle() const;
    
    // Resource Queries
    uint32_t rtv_descriptor_size() const;
    
    // RAII
    ~D3D12Context();
    D3D12Context(const D3D12Context&) = delete;
    D3D12Context(D3D12Context&&) = default;
    D3D12Context& operator=(D3D12Context&&) = default;
};
```

**Exceptions**:
- `std::runtime_error` on D3D12 creation failure
- `std::runtime_error` on swap chain creation failure
- `std::bad_alloc` on out-of-memory

**Windows assumptions**:
- HWND must be valid at construction time
- HWND must remain valid for all render calls
- Caller is responsible for handling WM_SIZE (future: resize support)

---

#### 2. **GraphicsFrame**

**Responsibility**: Encapsulate per-frame command list recording and resource state transitions.

**Key invariants**:
- Frame can only be used during its scope (lifetime-bound)
- `begin()` must be called before recording commands
- `end()` must be called before frame destruction (or throws)
- Transitions are recorded to the command list

**Public interface**:
```cpp
class GraphicsFrame {
public:
    // Constructor
    explicit GraphicsFrame(D3D12Context* context, uint32_t frame_index);
    
    // Recording Lifecycle
    void begin();                               // Reset allocator, open command list
    void end();                                 // Close command list
    void execute();                             // Submit to command queue
    
    // State Transitions
    void transition_to_render_target();         // PRESENT -> RENDER_TARGET
    void transition_to_present();               // RENDER_TARGET -> PRESENT
    void transition_resource(ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after);
    
    // Access
    ID3D12GraphicsCommandList* command_list() const;
    ID3D12RenderTargetView* render_target_view() const;
    
    // RAII
    ~GraphicsFrame();
    GraphicsFrame(const GraphicsFrame&) = delete;
    GraphicsFrame(GraphicsFrame&&) = delete;    // Not movable (lifetime-bound)
};
```

**Exception safety**:
- Destructor is no-throw if `end()` was called; otherwise logs error
- Partial `begin()` → exception → cleanup guaranteed

---

#### 3. **DeviceResources** and **GPUBuffer<T>**

**Responsibility**: Manage structured GPU buffers with automatic upload synchronization and capacity tracking.

**Public interface**:
```cpp
template<typename T>
class GPUBuffer {
public:
    // Constructor: allocates initial capacity
    explicit GPUBuffer(ID3D12Device* device, uint32_t initial_capacity);
    
    // Data Management
    void resize(uint32_t element_count);       // Can trigger GPU allocation
    void update(const T* data, uint32_t count, uint32_t offset = 0);
    void upload(ID3D12GraphicsCommandList* command_list);
    void clear();
    
    // Access (const, no-throw)
    ID3D12Resource* gpu_resource() const;
    uint32_t capacity() const;
    uint32_t element_count() const;
    bool is_dirty() const;
    
    // RAII
    ~GPUBuffer() = default;
    GPUBuffer(const GPUBuffer&) = delete;
    GPUBuffer(GPUBuffer&&) = default;
};

class DeviceResources {
public:
    // Higher-Level Interface
    uint32_t allocate_cbv_srv_descriptor();    // From heap
    void release_descriptor(uint32_t handle);
    
private:
    std::vector<std::unique_ptr<GPUBuffer<std::byte>>> buffers_;
};
```

**Staging buffer strategy**:
- Each buffer maintains optional staging copy
- `update()` writes to staging copy
- `upload()` records copy command to GPU
- CPU-visible pointer managed internally

---

#### 4. **PipelineBuilder**

**Responsibility**: Load compiled shaders, create root signatures, and cache pipeline state objects.

**Key invariants**:
- Shaders are pre-compiled (`.cso` files); no runtime compilation
- PSOs are cached by name; queries are O(log n)
- Missing shaders throw at load time

**Public interface**:
```cpp
class PipelineBuilder {
public:
    // Shader Loading
    ComPtr<ID3DBlob> load_compiled_shader(std::string_view relative_path);
    
    // Root Signature Creation
    ComPtr<ID3D12RootSignature> create_root_signature(
        ID3D12Device* device,
        const D3D12_ROOT_SIGNATURE_DESC& desc);
    
    // PSO Creation + Caching
    ComPtr<ID3D12PipelineState> build_pipeline(
        ID3D12Device* device,
        std::string_view debug_name,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);
    
    ID3D12PipelineState* get_cached_pipeline(std::string_view name) const;
    
private:
    std::map<std::string, ComPtr<ID3D12PipelineState>> pso_cache_;
};
```

**Exception handling**:
- `std::runtime_error` on missing shader file
- `std::runtime_error` on D3D12 pipeline creation failure

---

### Supporting Types

#### RenderConstants

Defines standard uniform buffer layouts for shaders.

```cpp
struct SceneConstants {
    DirectX::XMFLOAT4X4 inverse_view_projection;
    DirectX::XMFLOAT4 camera_world_position;
    DirectX::XMFLOAT4 field_origin_and_voxel_size;
    // ... more as needed
};

struct FrameMetadata {
    uint32_t frame_index;
    uint64_t frame_count;
    float delta_time;
};
```

---

## Module Interface Summary

### Public Headers

- `d3d12_context.h` - Device and frame management
- `graphics_frame.h` - Per-frame recording
- `device_resources.h` - GPU buffer management
- `pipeline_builder.h` - Shader and PSO creation
- `render_constants.h` - Shared uniform buffer layouts

### Usage Pattern (High-Level)

```cpp
// 1. Create context at startup
D3D12Context graphics(hwnd, 1280, 720);

// 2. Create pipeline
PipelineBuilder builder;
auto vs = builder.load_compiled_shader("shaders/terrain.vs.cso");
auto ps = builder.load_compiled_shader("shaders/terrain.ps.cso");
auto pso = builder.build_pipeline(graphics.device(), "terrain_pipeline", {
    // ... D3D12_GRAPHICS_PIPELINE_STATE_DESC ...
});

// 3. Create buffers
GPUBuffer<TerrainCell> terrain_buffer(graphics.device(), 10000);
terrain_buffer.update(terrain_data.data(), terrain_data.size());

// 4. Main loop
while (running) {
    // Frame recording
    {
        GraphicsFrame frame(&graphics, graphics.current_frame_index());
        frame.begin();
        
        terrain_buffer.upload(frame.command_list());
        frame.transition_to_render_target();
        
        // ... record render calls ...
        
        frame.transition_to_present();
        frame.end();
        frame.execute();
    }
    
    // Present and advance frame
    graphics.present(1);
}

// 5. Cleanup (automatic via RAII)
graphics.wait_for_gpu();
// ~D3D12Context() called here
```

---

## Windows Integration

### Environment Assumptions

- Windows 10+ (DXGI 1.6 required)
- Modern x64 CPU with D3D12 support
- COM runtime initialized (caller's responsibility)

### Error Model

- **Fatal errors** (device creation, etc.) → throw `std::runtime_error`
- **Resource errors** (allocation failure) → throw `std::bad_alloc`
- **State errors** (calling methods in wrong order) → throw `std::logic_error`

All errors propagate to caller; no silent failures or console output (use external logging).

### Future Windows Features (Not Phase 1)

- Fullscreen/windowed mode toggle
- DPI awareness / monitor-scale handling
- Window resize support
- Variable refresh rate (G-Sync, FreeSync)
- Shader hot-reloading

---

## Testing Strategy

### Unit Tests (Catch2)

**Test file**: `tests/d3d12_renderer_test.cpp`

**Coverage**:
1. Context lifetime and cleanup
2. Frame synchronization correctness
3. Buffer resize and capacity
4. Pipeline state caching
5. Exception safety guarantees
6. Move semantics

**Key test scenarios**:
```
✓ Create context, verify device valid, destroy
✓ Render 10 frames, verify no GPU stalls
✓ Move context to new location, old destroyed
✓ Resize buffer 3x, verify capacity >= requested
✓ Partial init failure → full cleanup
✓ Multiple pipelines cached correctly
```

### Integration Tests

**Target**: grass-field-001 refactor

1. Create context with grass-field params (1280x720)
2. Load 3 pipelines (coarse, refined, hybrid)
3. Render 1000 frames
4. Verify pixel output matches original implementation (delta < 1 ULP)
5. Verify no GPU memory leaks

---

## Extraction Phases

### Phase 1: Core Module & Documentation ✓
- [x] Architecture document (this file)
- [ ] CMakeLists.txt for module
- [ ] Header stubs (declarations only)
- [ ] Tests stubs

### Phase 2: Implementation
- [ ] `d3d12_context.cpp` (400 LOC)
- [ ] `graphics_frame.cpp` (200 LOC)
- [ ] `device_resources.cpp` (300 LOC)
- [ ] `pipeline_builder.cpp` (150 LOC)
- [ ] Unit tests (500 LOC)

### Phase 3: grass-field-001 Refactor
- [ ] Replace inline D3D12 code with module usage
- [ ] Validate pixel output equivalence
- [ ] Remove inline boilerplate

### Phase 4: Future Expansion
- [ ] grass-field-002 adoption
- [ ] New subproject usage
- [ ] Performance profiling hooks

---

## Maintenance Notes

### For Future Developers

**If adding new features**:
- Keep public interface minimal
- Prefer composition over inheritance
- All exceptions must be documented in header comments
- Add tests before implementation

**If considering breaking changes**:
- Check all subproject consumers first
- Consider versioning or deprecation strategy
- Update this document before merging

**The golden rule**: RAII guarantees must never be weakened. If a change requires hand-crafted cleanup, it does not belong in public API.

---

## References

- [DEVELOPMENT_GUIDE.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/DEVELOPMENT_GUIDE.md) - Project principles
- [grass-field-001/README.md](/D:/Repos/Games/TheGame/projects/grannys-house-trials/subprojects/grass-field-001/README.md) - Extraction source
- Direct3D 12 API Documentation (Microsoft Docs)
- C++ Windows Runtime (WRL) Documentation
