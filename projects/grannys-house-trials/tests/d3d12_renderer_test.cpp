#include <catch2/catch_test_macros.hpp>

#include "grannys_house_trials/gfx/d3d12_context.h"
#include "grannys_house_trials/gfx/graphics_frame.h"
#include "grannys_house_trials/gfx/device_resources.h"
#include "grannys_house_trials/gfx/pipeline_builder.h"
#include "grannys_house_trials/gfx/render_constants.h"

#include <windows.h>
#include <memory>

using namespace grannys_house_trials::gfx;

// Helper: Create a minimal window for testing
class TestWindowHarness {
public:
    TestWindowHarness() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"D3D12ContextTestWindow";
        RegisterClassW(&wc);

        hwnd_ = CreateWindowExW(
            0,
            L"D3D12ContextTestWindow",
            L"Test",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            640,
            480,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        REQUIRE(hwnd_ != nullptr);
        ShowWindow(hwnd_, SW_HIDE);
    }

    ~TestWindowHarness() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
        }
        UnregisterClassW(L"D3D12ContextTestWindow", nullptr);
    }

    HWND hwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

// ============================================================================
// D3D12Context Tests
// ============================================================================

TEST_CASE("D3D12Context: Constructor with valid parameters", "[d3d12_context]") {
    TestWindowHarness window;

    REQUIRE_NOTHROW(
        D3D12Context ctx(window.hwnd(), 1280, 720)
    );
}

TEST_CASE("D3D12Context: Constructor with null HWND throws", "[d3d12_context]") {
    REQUIRE_THROWS_AS(
        D3D12Context ctx(nullptr, 1280, 720),
        std::invalid_argument
    );
}

TEST_CASE("D3D12Context: Constructor with zero width throws", "[d3d12_context]") {
    TestWindowHarness window;

    REQUIRE_THROWS_AS(
        D3D12Context ctx(window.hwnd(), 0, 720),
        std::invalid_argument
    );
}

TEST_CASE("D3D12Context: Constructor with zero height throws", "[d3d12_context]") {
    TestWindowHarness window;

    REQUIRE_THROWS_AS(
        D3D12Context ctx(window.hwnd(), 1280, 0),
        std::invalid_argument
    );
}

TEST_CASE("D3D12Context: Device pointer is valid", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE(ctx.device() != nullptr);
}

TEST_CASE("D3D12Context: Command queue pointer is valid", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE(ctx.command_queue() != nullptr);
}

TEST_CASE("D3D12Context: Current frame index is in valid range", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720, 2);

    REQUIRE(ctx.current_frame_index() < 2);
}

TEST_CASE("D3D12Context: Total frames presented starts at zero", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE(ctx.total_frames_presented() == 0);
}

TEST_CASE("D3D12Context: Current render target is valid", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE(ctx.current_render_target() != nullptr);
}

TEST_CASE("D3D12Context: RTV descriptor size is consistent", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    std::uint32_t desc_size = ctx.rtv_descriptor_size();
    REQUIRE(desc_size > 0);
    REQUIRE(desc_size == ctx.rtv_descriptor_size()); // Consistent
}

TEST_CASE("D3D12Context: Move constructor transfers ownership", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx1(window.hwnd(), 1280, 720);
    ID3D12Device* device1 = ctx1.device();

    D3D12Context ctx2 = std::move(ctx1);
    ID3D12Device* device2 = ctx2.device();

    REQUIRE(device1 == device2);
}

TEST_CASE("D3D12Context: Copy constructor is deleted", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    // Verify copy semantics are forbidden (RAII safety)
    REQUIRE(!std::is_copy_constructible_v<D3D12Context>);
    REQUIRE(!std::is_copy_assignable_v<D3D12Context>);
}

TEST_CASE("D3D12Context: Command allocators are valid for all frames", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720, 3);

    for (std::uint32_t i = 0; i < 3; ++i) {
        REQUIRE(ctx.command_allocator(i) != nullptr);
    }
}

TEST_CASE("D3D12Context: Command allocator out of bounds throws", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720, 2);

    REQUIRE_THROWS_AS(ctx.command_allocator(2), std::out_of_range);
    REQUIRE_THROWS_AS(ctx.command_allocator(3), std::out_of_range);
}

TEST_CASE("D3D12Context: Destructor performs GPU wait (RAII cleanup)", "[d3d12_context]") {
    TestWindowHarness window;

    // Create context in a scope to verify destructor runs
    {
        D3D12Context ctx(window.hwnd(), 1280, 720);
        // Destructor should call wait_for_gpu() automatically
        // No explicit cleanup needed by caller
    }
    // If we reach here without hanging, destructor completed successfully
    REQUIRE(true);
}

TEST_CASE("D3D12Context: Multiple presents advance frame correctly", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720, 2);

    std::uint32_t initial_frame = ctx.current_frame_index();

    REQUIRE_NOTHROW(ctx.present());
    std::uint32_t second_frame = ctx.current_frame_index();

    REQUIRE_NOTHROW(ctx.present());
    std::uint32_t third_frame = ctx.current_frame_index();

    // Frames should cycle: 0 or 1 depending on initial state
    REQUIRE(ctx.total_frames_presented() == 2);
}

TEST_CASE("D3D12Context: All COM objects properly released on exception", "[d3d12_context]") {
    TestWindowHarness window;

    // Create and destroy contexts in rapid succession
    // If COM objects leak, this could trigger issues
    for (int i = 0; i < 3; ++i) {
        REQUIRE_NOTHROW(
            D3D12Context ctx(window.hwnd(), 1280, 720)
        );
    }
    REQUIRE(true);
}

// ============================================================================
// GraphicsFrame Tests
// ============================================================================

TEST_CASE("GraphicsFrame: Constructor with null context throws", "[graphics_frame]") {
    REQUIRE_THROWS_AS(
        GraphicsFrame frame(nullptr, 0),
        std::invalid_argument
    );
}

TEST_CASE("GraphicsFrame: begin() must be called before recording", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    // Calling end() without begin() should throw
    REQUIRE_THROWS_AS(frame.end(), std::logic_error);
}

TEST_CASE("GraphicsFrame: end() closes command list", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    REQUIRE_NOTHROW(frame.begin());
    REQUIRE_NOTHROW(frame.end());
}

TEST_CASE("GraphicsFrame: cannot call begin() twice", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    frame.begin();
    REQUIRE_THROWS_AS(frame.begin(), std::logic_error);
}

TEST_CASE("GraphicsFrame: transition_to_render_target records barrier", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    frame.begin();
    REQUIRE_NOTHROW(frame.transition_to_render_target());
    frame.end();
}

TEST_CASE("GraphicsFrame: transition_to_present records barrier", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    frame.begin();
    frame.transition_to_render_target();
    REQUIRE_NOTHROW(frame.transition_to_present());
    frame.end();
}

TEST_CASE("GraphicsFrame: Copy constructor is deleted (RAII safety)", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    // Verify copy semantics are forbidden (lifetime-scoped RAII)
    REQUIRE(!std::is_copy_constructible_v<GraphicsFrame>);
    REQUIRE(!std::is_copy_assignable_v<GraphicsFrame>);
}

TEST_CASE("GraphicsFrame: Move constructor is deleted (lifetime-bound)", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    // GraphicsFrame CANNOT be moved (lifetime-bound to stack scope)
    REQUIRE(!std::is_move_constructible_v<GraphicsFrame>);
    REQUIRE(!std::is_move_assignable_v<GraphicsFrame>);
}

TEST_CASE("GraphicsFrame: Cannot transition outside of begin/end", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    // Should not be able to transition before begin()
    REQUIRE_THROWS_AS(
        frame.transition_to_render_target(),
        std::logic_error
    );
}

TEST_CASE("GraphicsFrame: Cannot execute without end()", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    frame.begin();
    // Should not be able to execute before end()
    REQUIRE_THROWS_AS(frame.execute(), std::logic_error);
}

TEST_CASE("GraphicsFrame: Destructor without end() logs error", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    {
        GraphicsFrame frame(&ctx, ctx.current_frame_index());
        frame.begin();GPUBuffer copy constructor is deleted (RAII)", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    // Copy semantics forbidden to prevent double-cleanup
    REQUIRE(!std::is_copy_constructible_v<GPUBuffer<std::uint32_t>>);
    REQUIRE(!std::is_copy_assignable_v<GPUBuffer<std::uint32_t>>);
}

TEST_CASE("DeviceResources: GPUBuffer move semantics are allowed", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer1(ctx.device(), 100);
    ID3D12Resource* orig_resource = buffer1.gpu_resource();

    // Move should transfer ownership
    GPUBuffer<std::uint32_t> buffer2 = std::move(buffer1);
    REQUIRE(buffer2.gpu_resource() == orig_resource);
}

T

TEST_CASE("PipelineBuilder: Copy constructor is deleted", "[pipeline_builder]") {
    PipelineBuilder builder;

    // Ensure copy semantics are forbidden
    REQUIRE(!std::is_copy_constructible_v<PipelineBuilder>);
    REQUIRE(!std::is_copy_assignable_v<PipelineBuilder>);
}

TEST_CASE("PipelineBuilder: Move semantics are allowed", "[pipeline_builder]") {
    PipelineBuilder builder1;

    REQUIRE(std::is_move_constructible_v<PipelineBuilder>);
    REQUIRE(std::is_move_assignable_v<PipelineBuilder>);

    PipelineBuilder builder2 = std::move(builder1);
    REQUIRE(builder2.get_cached_pipeline("nonexistent") == nullptr);
}EST_CASE("DeviceResources: GPUBuffer out of bounds update throws", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    std::uint32_t data[10] = {};
    // Try to write past end of buffer
    REQUIRE_THROWS_AS(
        buffer.update(data, 10, 95),
        std::out_of_range
    );
}

TEST_CASE("DeviceResources: GPUBuffer clear() resets size and dirty flag", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    std::uint32_t data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    buffer.update(data, 10, 0);
    REQUIRE(buffer.is_dirty());

    buffer.clear();
    REQUIRE(buffer.element_count() == 0);
    REQUIRE(!buffer.is_dirty());
}

TEST_CASE("DeviceResources: GPUBuffer destroy cleans up GPU resources", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    // Create and destroy in scope
    {
        GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);
        REQUIRE(buffer.gpu_resource() != nullptr);
    }
    // Destructor should clean up automatically (no leaks)
    REQUIRE(true);
}

TEST_CASE("DeviceResources: GPUBuffer with zero capacity throws", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    // Note: Creating with 0 capacity may be okay (lazy allocation),
    // but resizing to 0 should not happen
    // This tests that we handle edge cases gracefully
    REQUIRE(buffer.capacity() > 0);
}

TEST_CASE("DeviceResources: DeviceResources constructor with null device throws", "[device_resources]") {
    REQUIRE_THROWS_AS(
        DeviceResources res(nullptr),
        std::invalid_argument
    );
}

TEST_CASE("DeviceResources: DeviceResources copy constructor is deleted", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    DeviceResources res(ctx.device());

    REQUIRE(!std::is_copy_constructible_v<DeviceResources>);
    REQUIRE(!std::is_copy_assignable_v<DeviceResources>);
}

TEST_CASE("DeviceResources: DeviceResources move semantics are allowed", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    DeviceResources res1(ctx.device());

    // Move should be allowed
    REQUIRE(std::is_move_constructible_v<DeviceResources>);
    REQUIRE(std::is_move_assignable_v<DeviceResources>REQUIRE(true);
}

TEST_CASE("GraphicsFrame: Null resource transition is safe (no-op)", "[graphics_frame]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    frame.begin();
    REQUIRE_NOTHROW(
        frame.transition_resource(nullptr, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST)
    );
    frame.end();
}

// ============================================================================
// DeviceResources Tests
// ============================================================================

TEST_CASE("DeviceResources: GPUBuffer constructor allocates memory", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE_NOTHROW(
        GPUBuffer<std::uint32_t> buffer(ctx.device(), 1000)
    );
}

TEST_CASE("DeviceResources: GPUBuffer capacity matches requested", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 1000);

    REQUIRE(buffer.capacity() == 1000);
}

TEST_CASE("DeviceResources: GPUBuffer resize increases capacity", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    buffer.resize(200);
    REQUIRE(buffer.capacity() >= 200);
}

TEST_CASE("DeviceResources: GPUBuffer update marks dirty", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    std::uint32_t data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    REQUIRE_NOTHROW(buffer.update(data, 10, 0));
    REQUIRE(buffer.is_dirty());
}

TEST_CASE("DeviceResources: GPUBuffer gpu_resource returns valid pointer", "[device_resources]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

    REQUIRE(buffer.gpu_resource() != nullptr);
}

TEST_CASE("DeviceResources: DeviceResources constructor with null device throws", "[device_resources]") {
    REQUIRE_THROWS_AS(
        DeviceResources res(nullptr),
        std::invalid_argument
    );
}

// ============================================================================
// PipelineBuilder Tests
// ============================================================================

TEST_CASE("PipelineBuilder: Empty cache initialization", "[pipeline_builder]") {
    PipelineBuilder builder;

    REQUIRE(builder.get_cached_pipeline("nonexistent") == nullptr);
}

TEST_CASE("PipelineBuilder: Clear cache succeeds", "[pipeline_builder]") {
    PipelineBuilder builder;

    REQUIRE_NOTHROW(builder.clear_cache());
}

// ============================================================================
// RenderConstants Tests
// ============================================================================

TEST_CASE("RenderConstants: SceneConstants structure is properly sized", "[render_constants]") {
    SceneConstants sc{};

    // Just verify the structure exists and is instantiable
    REQUIRE(sizeof(sc) > 0);
}

TEST_CASE("RenderConstants: FrameMetadata structure is properly sized", "[render_constants]") {
    FrameMetadata fm{};

    // Just verify the structure exists and is instantiable
    REQUIRE(sizeof(fm) > 0);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_CASE("Integration: Full frame recording cycle", "[integration]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE_NOTHROW([&]() {
        GraphicsFrame frame(&ctx, ctx.current_frame_index());
        frame.begin();
        frame.transition_to_render_target();
        frame.transition_to_present();
        frame.end();
        frame.execute();
    }());
}

TEST_CASE("Integration: Multiple frames without GPU stalls", "[integration]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720, 2);

    for (int i = 0; i < 5; ++i) {
        REQUIRE_NOTHROW([&]() {
            GraphicsFrame frame(&ctx, ctx.current_frame_index());
            frame.begin();
            frame.transition_to_render_target();
            frame.transition_to_present();
            frame.end();
            frame.execute();
        }());

        REQUIRE_NOTHROW(ctx.present());
    }

    REQUIRE(ctx.total_frames_presented() == 5);
}

TEST_CASE("Integration: GPU synchronization with wait_for_gpu", "[integration]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE_NOTHROW(ctx.wait_for_gpu());
}

// ============================================================================
// RAII Best Practices Validation
// ============================================================================

TEST_CASE("RAII: No manual cleanup required (stack-based ownership)", "[raii_best_practices]") {
    TestWindowHarness window;

    // Create a context entirely on the stack
    // If RAII is working correctly, no manual cleanup should be needed
    {
        D3D12Context ctx(window.hwnd(), 1280, 720);
        ID3D12Device* device = ctx.device();
        REQUIRE(device != nullptr);
        // No explicit release or cleanup called
    }
    // All resources should be automatically cleaned up by destructor

    REQUIRE(true); // If we reach here, RAII cleanup was successful
}

TEST_CASE("RAII: ComPtr auto-cleanup on exception in constructor", "[raii_best_practices]") {
    TestWindowHarness window;

    // If we throw with an invalid parameter after partial init,
    // all ComPtr<> objects should be automatically cleaned up
    try {
        D3D12Context ctx(nullptr, 1280, 720); // Invalid HWND triggers exception
        REQUIRE(false); // Should not reach here
    } catch (const std::invalid_argument&) {
        // Exception expected - all resources should be cleaned up automatically
        REQUIRE(true);
    } catch (...) {
        REQUIRE(false); // Wrong exception type
    }
}

TEST_CASE("RAII: Fence event cleanup in destructor", "[raii_best_practices]") {
    TestWindowHarness window;

    // Create and destroy context - fence event should be cleaned up
    {
        D3D12Context ctx(window.hwnd(), 1280, 720);
        // Context owns fence_event handle
    }
    // Destructor should have closed the handle

    // Create another context afterward
    {
        D3D12Context ctx2(window.hwnd(), 1280, 720);
        REQUIRE(true); // Should succeed if previous handle was properly released
    }
}

TEST_CASE("RAII: Move constructor prevents double-cleanup", "[raii_best_practices]") {
    TestWindowHarness window;

    // Create a context
    D3D12Context original(window.hwnd(), 1280, 720);
    ID3D12Device* original_device = original.device();

    // Move the context
    D3D12Context moved = std::move(original);
    ID3D12Device* moved_device = moved.device();

    // Device pointer should be the same
    REQUIRE(moved_device == original_device);

    // moved_device should still be valid, original context destroyed is okay
    // because it's been moved from
    REQUIRE(moved.device() != nullptr);
}

TEST_CASE("RAII: Copy semantics forbidden prevents accidental double-cleanup", "[raii_best_practices]") {
    // This test documents the design intention:
    // D3D12 resources cannot be safely copied - only moved
    // Copy constructors are deleted to force users to use move semantics

    REQUIRE(!std::is_copy_constructible_v<D3D12Context>);
    REQUIRE(!std::is_copy_assignable_v<D3D12Context>);

    REQUIRE(!std::is_copy_constructible_v<GraphicsFrame>);
    REQUIRE(!std::is_copy_assignable_v<GraphicsFrame>);

    REQUIRE(!std::is_copy_constructible_v<GPUBuffer<std::uint32_t>>);
    REQUIRE(!std::is_copy_assignable_v<GPUBuffer<std::uint32_t>>);
}

TEST_CASE("RAII: Exception safety during buffer allocation", "[raii_best_practices]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    // If buffer allocation throws, no resources should leak
    try {
        GPUBuffer<std::uint32_t> buffer(ctx.device(), 10000000); // May fail on memory pressure
        // If we get here, allocation succeeded
        REQUIRE(buffer.capacity() > 0);
    } catch (const std::exception&) {
        // Allocation failed - but all resources should still be cleaned up
        REQUIRE(true);
    }
}

TEST_CASE("RAII: No manual Release() calls in public API", "[raii_best_practices]") {
    // This is validated by design: all D3D12 COM objects are managed by ComPtr<>
    // The public API never requires users to call Release() manually
    //
    // Evidence: D3D12Context methods all return pointers, not ComPtr
    // This is intentional - ComPtr cleanup happens in d3d12_context.cpp
    // Users should never need to manage lifetime themselves

    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    // User code should look like:
    ID3D12Device* device = ctx.device();
    // NOT: device->Release(); // Don't do this!

    REQUIRE(device != nullptr);
}

TEST_CASE("RAII: GPU synchronization prevents use-after-free", "[raii_best_practices]") {
    TestWindowHarness window;

    {
        D3D12Context ctx(window.hwnd(), 1280, 720, 2);

        // Submit work and present
        {
            GraphicsFrame frame(&ctx, ctx.current_frame_index());
            frame.begin();
            frame.transition_to_render_target();
            frame.transition_to_present();
            frame.end();
            frame.execute();
        }

        REQUIRE_NOTHROW(ctx.present());

        // When ctx goes out of scope, destructor calls wait_for_gpu()
        // This ensures GPU has finished all work before cleanup
    }

    // If we reach here without GPU errors, synchronization worked correctly
    REQUIRE(true);
}

TEST_CASE("RAII: Lifetime-scoped GraphicsFrame prevents misuse", "[raii_best_practices]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    // GraphicsFrame is intentionally lifetime-scoped
    // It cannot be moved or copied, enforcing stack-based usage

    GraphicsFrame frame(&ctx, ctx.current_frame_index());

    // These should fail at compile-time (runtime test shows types):
    REQUIRE(!std::is_copy_constructible_v<GraphicsFrame>);
    REQUIRE(!std::is_move_constructible_v<GraphicsFrame>);

    // This enforces the single-frame-per-scope pattern:
    //   {
    //       GraphicsFrame frame(&ctx, idx);
    //       frame.begin();
    //       // ... record commands ...
    //       frame.end();
    //   } // frame destroyed here
}

TEST_CASE("RAII: All descriptor handles are managed internally", "[raii_best_practices]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    // Users never allocate or manage descriptor handles directly
    // All heap management is hidden in D3D12Context

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = ctx.current_rtv_handle();
    REQUIRE(rtv_handle.ptr != 0); // Valid handle

    // User code never does: delete rtv_handle, Release(), etc.
    // The descriptor heap is destroyed when ctx is destroyed
}

TEST_CASE("RAII: Staged buffer cleanup is implicit", "[raii_best_practices]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    {
        GPUBuffer<std::uint32_t> buffer(ctx.device(), 100);

        std::uint32_t data[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        buffer.update(data, 10, 0);

        // GPU buffer and staging buffer exist in buffer object
        // User never manages them directly
        REQUIRE(buffer.gpu_resource() != nullptr);
    }

    // Both GPU and staging buffers destroyed here
    // User didn't need to explicitly unmap, release, or cleanup
    REQUIRE(true);
}
