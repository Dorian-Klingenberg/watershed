#pragma once

#include <cstdint>
#include <stdexcept>
#include <d3d12.h>

namespace grannys_house_trials::gfx {

// Forward declare to avoid circular includes
class D3D12Context;

/// Encapsulates per-frame GPU command recording and resource state transitions.
///
/// This class manages the lifetime of a single frame's command list recording.
/// All resource transitions are recorded to the command list and executed at frame boundaries.
///
/// RAII behavior:
/// - Constructor: Binds to a specific frame in the D3D12Context
/// - begin(): Resets the command allocator and opens the command list
/// - Record commands: User calls GPU functions via command_list()
/// - end(): Closes the command list (must be called before destruction)
/// - Destructor: Verifies frame was properly closed
///
/// Lifetime safety:
/// - GraphicsFrame objects are NOT movable or copyable (lifetime-bound to stack)
/// - Frame must outlive all command recording operations
/// - Frame must outlive all calls to command_list()
///
/// Exception safety:
/// - Basic guarantee: partial frame state must be cleaned up
/// - If end() is not called before destruction, destructor logs error
///
/// Typical usage:
/// @code
///     D3D12Context graphics(hwnd, 1280, 720);
///     {
///         GraphicsFrame frame(&graphics, graphics.current_frame_index());
///         frame.begin();
///         // ... record GPU commands to frame.command_list() ...
///         frame.transition_to_render_target();
///         // ... render commands ...
///         frame.transition_to_present();
///         frame.end();
///         frame.execute();
///     }
///     graphics.present();
/// @endcode
class GraphicsFrame {
public:
    /// Constructor: allocates frame resources and binds to context.
    /// @param context Valid D3D12Context pointer (must outlive this frame)
    /// @param frame_index Frame index from context.current_frame_index()
    explicit GraphicsFrame(D3D12Context* context, std::uint32_t frame_index);

    /// Destructor: verifies frame was properly ended.
    /// Logs error if end() was not called.
    ~GraphicsFrame();

    // RAII: prevent copying and moving (lifetime-bound to stack scope)
    GraphicsFrame(const GraphicsFrame&) = delete;
    GraphicsFrame& operator=(const GraphicsFrame&) = delete;
    GraphicsFrame(GraphicsFrame&&) = delete;
    GraphicsFrame& operator=(GraphicsFrame&&) = delete;

    // ========== Recording Lifecycle ==========

    /// Opens the frame for GPU command recording.
    /// Resets the command allocator and opens the command list.
    /// Must be called before recording any GPU commands.
    /// @throws std::runtime_error if reset fails
    void begin();

    /// Closes the command list (marks frame recording complete).
    /// Must be called before execute() or frame destruction.
    /// @throws std::runtime_error if close fails
    void end();

    /// Submits the recorded command list to the GPU command queue.
    /// Must be called after end().
    /// After execute(), the GPU begins processing queued commands.
    /// @throws std::runtime_error if execution fails
    void execute();

    // ========== State Transitions ==========

    /// Records a transition from PRESENT to RENDER_TARGET state.
    /// Call before recording render commands.
    /// Transitions are recorded to the command list.
    void transition_to_render_target();

    /// Records a transition from RENDER_TARGET to PRESENT state.
    /// Call after recording render commands, before execute().
    /// Prepares the resource for swap chain presentation.
    void transition_to_present();

    /// Records an arbitrary resource state transition.
    /// Useful for transitioning UAVs, SRVs, or other resources during rendering.
    /// @param resource Resource to transition (may be null, in which case no-op)
    /// @param before Current resource state
    /// @param after Desired resource state
    void transition_resource(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after);

    // ========== Access ==========

    /// Returns the command list for recording GPU commands.
    /// Valid after begin(), must be closed by end().
    /// No-throw, const-qualified.
    ID3D12GraphicsCommandList* command_list() const noexcept;

    /// Returns the render target view descriptor for OMSetRenderTargets().
    /// No-throw, const-qualified.
    D3D12_CPU_DESCRIPTOR_HANDLE render_target_view() const noexcept;

private:
    D3D12Context* context_ = nullptr;
    std::uint32_t frame_index_ = 0;
    bool is_open_ = false;
    bool is_closed_ = false;
};

} // namespace grannys_house_trials::gfx
