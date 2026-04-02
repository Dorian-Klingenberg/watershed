#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <CommCtrl.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <uxtheme.h>
#include <wrl/client.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

// As required by ImGui docs - WndProcHandler is intentionally behind #if 0 in the header
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include "grannys_house_trials/sim/adaptive_terrain_ownership_field.h"
#include "grannys_house_trials/gfx/orbit_camera.h"
#include "grannys_house_trials/playtest/grannys_yard_session.h"
#include "grannys_house_trials/sim/gravity_erosion_field.h"
#include "grannys_house_trials/sim/grass_field.h"
#include "grannys_house_trials/sim/sparse_refined_patch_field.h"

using namespace grannys_house_trials;

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
using namespace DirectX;

constexpr UINT frame_count = 2;
constexpr float pi = 3.1415926535f;
constexpr wchar_t main_window_class_name[] = L"GrannysHouseTrialsGrassField002";

// Forward declarations for Win32 window procedure
struct Application;
Application* g_app = nullptr;

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    // Route Win32 message to ImGui
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam))
        return true;

    if (g_app == nullptr)
        return DefWindowProcW(hwnd, message, wparam, lparam);

    switch (message)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_MOUSEMOVE:
    {
        // TODO: Ray-cast from mouse to world, select voxel
        (void)lparam;
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        // Right-click to select voxel for inspection
        return 0;
    }

    case WM_SIZE:
    {
        // Handle window resize
        return 0;
    }

    default:
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

struct VoxelInfo
{
    int x, y, z;
    float elevation;
    float moisture;
    std::string ground_type;
    bool is_soaking;
    std::string description;
};

struct Application
{
    HWND hwnd = nullptr;
    ComPtr<IDXGIFactory7> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> command_queue;
    ComPtr<ID3D12CommandAllocator> command_allocator;
    ComPtr<ID3D12GraphicsCommandList> command_list;
    ComPtr<IDXGISwapChain4> swap_chain;
    std::array<ComPtr<ID3D12Resource>, frame_count> render_targets;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12DescriptorHeap> srv_heap;
    ComPtr<ID3D12Fence> fence;
    HANDLE fence_event = nullptr;
    UINT64 fence_value = 0;
    UINT rtv_descriptor_size = 0;

    // Rendering
    gfx::OrbitCamera camera;
    
    // UI State
    std::optional<VoxelInfo> selected_voxel;
    bool show_demo_window = false;
    bool show_voxel_inspector = true;
    bool show_agent_panel = true;
    bool show_chat_log = true;

    // Simulation
    std::unique_ptr<playtest::GrannysYardSession> session;

    Application()
        : camera(0.0f, 10.0f, 15.0f)
    {
        g_app = this;
    }

    ~Application()
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        if (fence_event)
            CloseHandle(fence_event);
        g_app = nullptr;
    }

    void wait_for_gpu()
    {
        if (FAILED(command_queue->Signal(fence.Get(), fence_value)))
            throw std::runtime_error("Failed to signal D3D12 fence");

        if (FAILED(fence->SetEventOnCompletion(fence_value, fence_event)))
            throw std::runtime_error("Failed to set D3D12 fence event");

        WaitForSingleObject(fence_event, INFINITE);
        ++fence_value;
    }

    void initialize()
    {
        create_window();
        initialize_d3d12();
        initialize_imgui();
        initialize_simulation();
    }

    void create_window()
    {
        WNDCLASSEXW wnd_class = {};
        wnd_class.cbSize = sizeof(WNDCLASSEXW);
        wnd_class.style = CS_HREDRAW | CS_VREDRAW;
        wnd_class.lpfnWndProc = window_proc;
        wnd_class.hInstance = GetModuleHandleW(nullptr);
        wnd_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        wnd_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wnd_class.lpszClassName = main_window_class_name;

        RegisterClassExW(&wnd_class);

        hwnd = CreateWindowExW(
            0,
            main_window_class_name,
            L"Agent Sandbox",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1600,
            1200,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr
        );

        if (!hwnd)
            throw std::runtime_error("Failed to create window");

        ShowWindow(hwnd, SW_SHOW);
    }

    void initialize_d3d12()
    {
        // Create DXGI factory
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
            throw std::runtime_error("Failed to create DXGI factory");

        // Create device
        ComPtr<ID3D12Device> device_temp;
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_temp))))
            throw std::runtime_error("Failed to create D3D12 device");

        device = device_temp;

        // Create command queue
        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue))))
            throw std::runtime_error("Failed to create command queue");

        // Create command allocator and list
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocator))))
            throw std::runtime_error("Failed to create command allocator");

        if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocator.Get(), nullptr, IID_PPV_ARGS(&command_list))))
            throw std::runtime_error("Failed to create command list");

        command_list->Close();

        // Create fence
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
            throw std::runtime_error("Failed to create fence");

        fence_value = 1;
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event)
            throw std::runtime_error("Failed to create fence event");

        // Create swap chain
        RECT rect;
        GetClientRect(hwnd, &rect);
        UINT width = rect.right - rect.left;
        UINT height = rect.bottom - rect.top;

        DXGI_SWAP_CHAIN_DESC1 sc_desc = {};
        sc_desc.Width = width;
        sc_desc.Height = height;
        sc_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc_desc.Stereo = FALSE;
        sc_desc.SampleDesc.Count = 1;
        sc_desc.SampleDesc.Quality = 0;
        sc_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc_desc.BufferCount = frame_count;
        sc_desc.Scaling = DXGI_SCALING_STRETCH;
        sc_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

        ComPtr<IDXGISwapChain1> swap_chain_temp;
        if (FAILED(factory->CreateSwapChainForHwnd(command_queue.Get(), hwnd, &sc_desc, nullptr, nullptr, &swap_chain_temp)))
            throw std::runtime_error("Failed to create swap chain");

        swap_chain_temp.As(&swap_chain);

        // Create RTV heap
        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap_desc.NumDescriptors = frame_count;
        if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&rtv_heap))))
            throw std::runtime_error("Failed to create RTV heap");

        rtv_descriptor_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Create SRV heap for ImGui
        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc = {};
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.NumDescriptors = 1;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap))))
            throw std::runtime_error("Failed to create SRV heap");

        // Create render target views
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < frame_count; i++)
        {
            if (FAILED(swap_chain->GetBuffer(i, IID_PPV_ARGS(&render_targets[i]))))
                throw std::runtime_error("Failed to get swap chain buffer");

            device->CreateRenderTargetView(render_targets[i].Get(), nullptr, rtv_handle);
            rtv_handle.ptr += rtv_descriptor_size;
        }
    }

    void initialize_imgui()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX12_Init(device.Get(), frame_count, DXGI_FORMAT_R8G8B8A8_UNORM, srv_heap.Get(),
                            srv_heap->GetCPUDescriptorHandleForHeapStart(),
                            srv_heap->GetGPUDescriptorHandleForHeapStart());
    }

    void initialize_simulation()
    {
        session = std::make_unique<playtest::GrannysYardSession>();
        (void)session;
    }

    void render_frame()
    {
        // Start ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render UI panels
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        render_voxel_inspector();
        render_agent_panel();
        render_chat_log();

        ImGui::Render();

        const UINT frame_index = swap_chain->GetCurrentBackBufferIndex();

        if (FAILED(command_allocator->Reset()))
            throw std::runtime_error("Failed to reset command allocator");

        if (FAILED(command_list->Reset(command_allocator.Get(), nullptr)))
            throw std::runtime_error("Failed to reset command list");

        D3D12_RESOURCE_BARRIER to_render_target = {};
        to_render_target.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_render_target.Transition.pResource = render_targets[frame_index].Get();
        to_render_target.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_render_target.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        to_render_target.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_list->ResourceBarrier(1, &to_render_target);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        rtv_handle.ptr += static_cast<SIZE_T>(frame_index) * rtv_descriptor_size;

        const float clear_color[4] = {0.08f, 0.10f, 0.12f, 1.0f};
        command_list->OMSetRenderTargets(1, &rtv_handle, FALSE, nullptr);
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);

        ID3D12DescriptorHeap* descriptor_heaps[] = { srv_heap.Get() };
        command_list->SetDescriptorHeaps(1, descriptor_heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list.Get());

        D3D12_RESOURCE_BARRIER to_present = {};
        to_present.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        to_present.Transition.pResource = render_targets[frame_index].Get();
        to_present.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        command_list->ResourceBarrier(1, &to_present);

        if (FAILED(command_list->Close()))
            throw std::runtime_error("Failed to close command list");

        ID3D12CommandList* lists[] = { command_list.Get() };
        command_queue->ExecuteCommandLists(1, lists);

        if (FAILED(swap_chain->Present(1, 0)))
            throw std::runtime_error("Failed to present swap chain");

        wait_for_gpu();
    }

    void render_voxel_inspector()
    {
        if (!ImGui::Begin("Voxel Inspector", &show_voxel_inspector))
        {
            ImGui::End();
            return;
        }

        if (selected_voxel)
        {
            ImGui::Text("Position: (%d, %d, %d)", selected_voxel->x, selected_voxel->y, selected_voxel->z);
            ImGui::Text("Elevation: %.2f", selected_voxel->elevation);
            ImGui::Text("Moisture: %.2f", selected_voxel->moisture);
            ImGui::Text("Ground Type: %s", selected_voxel->ground_type.c_str());
            ImGui::Text("Soaking: %s", selected_voxel->is_soaking ? "Yes" : "No");
            ImGui::TextWrapped("Description: %s", selected_voxel->description.c_str());
        }
        else
        {
            ImGui::Text("(Right-click a voxel to inspect)");
        }

        ImGui::End();
    }

    void render_agent_panel()
    {
        if (!ImGui::Begin("Agents", &show_agent_panel))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Active Agents: TBD");
        ImGui::Separator();

        // TODO: List agents with their locations and current reasoning
        ImGui::Text("Agent reasoning will appear here");

        ImGui::End();
    }

    void render_chat_log()
    {
        if (!ImGui::Begin("Conversation Log", &show_chat_log))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Agent conversations and actions will appear here");
        
        ImGui::End();
    }

    void run()
    {
        MSG msg = {};
        while (msg.message != WM_QUIT)
        {
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            else
            {
                render_frame();
            }
        }
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        Application app;
        app.initialize();
        app.run();
    }
    catch (const std::exception& e)
    {
        MessageBoxA(nullptr, e.what(), "Error", MB_ICONERROR);
        return 1;
    }

    return 0;
}
