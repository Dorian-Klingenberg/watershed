#include <catch2/catch_test_macros.hpp>

#include "grannys_house_trials/gfx/d3d12_context.h"
#include "grannys_house_trials/gfx/ui_frame_renderer.h"

#include <windows.h>
#include <stdexcept>

using namespace grannys_house_trials::gfx;

namespace {

class TestWindowHarness {
public:
    TestWindowHarness() {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"UIFrameRendererTestsWindow";
        RegisterClassW(&wc);

        hwnd_ = CreateWindowExW(
            0,
            L"UIFrameRendererTestsWindow",
            L"UIFrameRendererTests",
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
        UnregisterClassW(L"UIFrameRendererTestsWindow", nullptr);
    }

    HWND hwnd() const { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

} // namespace

TEST_CASE("UIFrameRenderer: calls record callback", "[ui_frame_renderer]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    UIFrameRenderer renderer;

    bool callback_called = false;
    const float clear_color[] = {0.1f, 0.2f, 0.3f, 1.0f};

    REQUIRE_NOTHROW(renderer.render(
        ctx,
        nullptr,
        clear_color,
        [&](ID3D12GraphicsCommandList* command_list) {
            REQUIRE(command_list != nullptr);
            callback_called = true;
        }));

    REQUIRE(callback_called);
}

TEST_CASE("UIFrameRenderer: null callback is allowed", "[ui_frame_renderer]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);
    UIFrameRenderer renderer;
    const float clear_color[] = {0.0f, 0.0f, 0.0f, 1.0f};

    UIFrameRenderer::RecordCallback callback{};

    REQUIRE_NOTHROW(renderer.render(ctx, nullptr, clear_color, callback));
}

TEST_CASE("D3D12Context: resize boundary is explicit", "[d3d12_context]") {
    TestWindowHarness window;
    D3D12Context ctx(window.hwnd(), 1280, 720);

    REQUIRE_FALSE(ctx.supports_resize());
    REQUIRE_THROWS_AS(ctx.resize(1920, 1080), std::logic_error);
}
