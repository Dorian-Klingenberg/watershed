#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <windowsx.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "../../world_definition.h"

#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
using namespace DirectX;
using namespace voxel_infrastructure_004;

constexpr UINT frame_count = 2;
constexpr float pi = 3.1415926535f;

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT4 color;
};

struct SceneConstants
{
    XMFLOAT4X4 view_projection;
};

struct CameraState
{
    float yaw = 0.75f;
    float pitch = 0.55f;
    float distance = 24.0f;
    POINT last_mouse{};
    bool dragging = false;
};

struct FrameContext
{
    ComPtr<ID3D12CommandAllocator> allocator;
    UINT64 fence_value = 0;
};

XMFLOAT4 color_for_material(MaterialKind material)
{
    switch (material)
    {
    case MaterialKind::SurfaceWater: return {0.31f, 0.58f, 0.92f, 1.0f};
    case MaterialKind::MarshSoil: return {0.42f, 0.68f, 0.35f, 1.0f};
    case MaterialKind::TerraceFill: return {0.82f, 0.67f, 0.42f, 1.0f};
    case MaterialKind::Bedrock: return {0.44f, 0.46f, 0.50f, 1.0f};
    case MaterialKind::AncientConduit: return {0.20f, 0.75f, 0.82f, 1.0f};
    case MaterialKind::DelayBasin: return {0.86f, 0.71f, 0.34f, 1.0f};
    case MaterialKind::Collector: return {0.72f, 0.55f, 0.90f, 1.0f};
    case MaterialKind::InspectionShaft: return {0.90f, 0.90f, 0.90f, 1.0f};
    case MaterialKind::Air:
    default:
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
}

bool is_visible_voxel(const WorldDefinition &world, int x, int y, int z)
{
    const auto index_of = [&](int px, int py, int pz) {
        return static_cast<std::size_t>((pz * world.height + py) * world.width + px);
    };

    const WorldVoxel &voxel = world.voxels[index_of(x, y, z)];
    if (voxel.material == MaterialKind::Air || voxel.hidden)
    {
        return false;
    }

    const std::array<std::array<int, 3>, 6> offsets = {{
        {{1, 0, 0}}, {{-1, 0, 0}}, {{0, 1, 0}}, {{0, -1, 0}}, {{0, 0, 1}}, {{0, 0, -1}},
    }};

    for (const auto &offset : offsets)
    {
        const int nx = x + offset[0];
        const int ny = y + offset[1];
        const int nz = z + offset[2];
        if (nx < 0 || nx >= world.width || ny < 0 || ny >= world.height || nz < 0 || nz >= world.depth)
        {
            return true;
        }

        const WorldVoxel &neighbor = world.voxels[index_of(nx, ny, nz)];
        if (neighbor.material == MaterialKind::Air || neighbor.hidden)
        {
            return true;
        }
    }

    return false;
}

void append_cube(std::vector<Vertex> &vertices, float x, float y, float z, XMFLOAT4 color)
{
    const XMFLOAT3 p000{x, y, z};
    const XMFLOAT3 p001{x, y, z + 1.0f};
    const XMFLOAT3 p010{x, y + 1.0f, z};
    const XMFLOAT3 p011{x, y + 1.0f, z + 1.0f};
    const XMFLOAT3 p100{x + 1.0f, y, z};
    const XMFLOAT3 p101{x + 1.0f, y, z + 1.0f};
    const XMFLOAT3 p110{x + 1.0f, y + 1.0f, z};
    const XMFLOAT3 p111{x + 1.0f, y + 1.0f, z + 1.0f};

    const auto push_tri = [&](XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 normal) {
        vertices.push_back({a, normal, color});
        vertices.push_back({b, normal, color});
        vertices.push_back({c, normal, color});
    };

    push_tri(p001, p101, p111, {0.0f, 0.0f, 1.0f}); push_tri(p001, p111, p011, {0.0f, 0.0f, 1.0f});
    push_tri(p100, p000, p010, {0.0f, 0.0f, -1.0f}); push_tri(p100, p010, p110, {0.0f, 0.0f, -1.0f});
    push_tri(p000, p001, p011, {-1.0f, 0.0f, 0.0f}); push_tri(p000, p011, p010, {-1.0f, 0.0f, 0.0f});
    push_tri(p101, p100, p110, {1.0f, 0.0f, 0.0f}); push_tri(p101, p110, p111, {1.0f, 0.0f, 0.0f});
    push_tri(p010, p011, p111, {0.0f, 1.0f, 0.0f}); push_tri(p010, p111, p110, {0.0f, 1.0f, 0.0f});
    push_tri(p000, p100, p101, {0.0f, -1.0f, 0.0f}); push_tri(p000, p101, p001, {0.0f, -1.0f, 0.0f});
}

std::vector<Vertex> build_visible_mesh()
{
    const WorldDefinition world = build_world_definition();
    std::vector<Vertex> vertices;
    vertices.reserve(4096);

    const float x_offset = -static_cast<float>(world.width) * 0.5f;
    const float y_offset = 0.0f;
    const float z_offset = -static_cast<float>(world.depth) * 0.5f;

    for (int z = 0; z < world.depth; ++z)
    {
        for (int y = 0; y < world.height; ++y)
        {
            for (int x = 0; x < world.width; ++x)
            {
                if (!is_visible_voxel(world, x, y, z))
                {
                    continue;
                }

                const auto index = static_cast<std::size_t>((z * world.height + y) * world.width + x);
                const WorldVoxel &voxel = world.voxels[index];
                append_cube(vertices, x_offset + static_cast<float>(x), y_offset + static_cast<float>(y), z_offset + static_cast<float>(z), color_for_material(voxel.material));
            }
        }
    }

    return vertices;
}

class D3D12App
{
  public:
    int run(HINSTANCE instance);

  private:
    bool initialize(HINSTANCE instance);
    void initialize_pipeline();
    void create_assets();
    void update();
    void render();
    void wait_for_gpu();
    void move_to_next_frame();
    void on_mouse_down(LPARAM lparam);
    void on_mouse_up();
    void on_mouse_move(WPARAM buttons, LPARAM lparam);
    void on_mouse_wheel(WPARAM wparam);
    LRESULT handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND hwnd_ = nullptr;
    UINT width_ = 1280;
    UINT height_ = 720;
    CameraState camera_{};

    ComPtr<IDXGIFactory4> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> command_queue_;
    ComPtr<IDXGISwapChain3> swap_chain_;
    ComPtr<ID3D12DescriptorHeap> rtv_heap_;
    ComPtr<ID3D12DescriptorHeap> cbv_heap_;
    ComPtr<ID3D12Resource> render_targets_[frame_count];
    FrameContext frames_[frame_count];
    UINT rtv_descriptor_size_ = 0;
    UINT frame_index_ = 0;

    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> pipeline_state_;
    ComPtr<ID3D12GraphicsCommandList> command_list_;

    ComPtr<ID3D12Resource> vertex_buffer_;
    D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view_{};
    UINT vertex_count_ = 0;

    ComPtr<ID3D12Resource> constant_buffer_;
    SceneConstants *constant_buffer_data_ = nullptr;

    ComPtr<ID3D12Fence> fence_;
    UINT64 fence_value_ = 1;
    HANDLE fence_event_ = nullptr;
};

int D3D12App::run(HINSTANCE instance)
{
    if (!initialize(instance))
    {
        return 1;
    }

    ShowWindow(hwnd_, SW_SHOWDEFAULT);

    MSG message{};
    while (message.message != WM_QUIT)
    {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
            continue;
        }

        update();
        render();
    }

    wait_for_gpu();
    CloseHandle(fence_event_);
    return static_cast<int>(message.wParam);
}

bool D3D12App::initialize(HINSTANCE instance)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = &D3D12App::window_proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"VoxelD3D12Experiment001";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"Voxel D3D12 001",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        static_cast<int>(width_),
        static_cast<int>(height_),
        nullptr,
        nullptr,
        instance,
        this);

    if (!hwnd_)
    {
        return false;
    }

    initialize_pipeline();
    create_assets();
    return true;
}

void D3D12App::initialize_pipeline()
{
    CreateDXGIFactory1(IID_PPV_ARGS(&factory_));
    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_));

    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.BufferCount = frame_count;
    swap_desc.Width = width_;
    swap_desc.Height = height_;
    swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swap_chain;
    factory_->CreateSwapChainForHwnd(command_queue_.Get(), hwnd_, &swap_desc, nullptr, nullptr, &swap_chain);
    swap_chain.As(&swap_chain_);
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();

    D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
    rtv_heap_desc.NumDescriptors = frame_count;
    rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_));
    rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC cbv_heap_desc{};
    cbv_heap_desc.NumDescriptors = 1;
    cbv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device_->CreateDescriptorHeap(&cbv_heap_desc, IID_PPV_ARGS(&cbv_heap_));

    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < frame_count; ++index)
    {
        swap_chain_->GetBuffer(index, IID_PPV_ARGS(&render_targets_[index]));
        device_->CreateRenderTargetView(render_targets_[index].Get(), nullptr, rtv_handle);
        rtv_handle.ptr += rtv_descriptor_size_;
        device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frames_[index].allocator));
    }

    device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_[0].allocator.Get(), nullptr, IID_PPV_ARGS(&command_list_));
    command_list_->Close();

    device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
    fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

void D3D12App::create_assets()
{
    const char *shader_source =
        "cbuffer SceneConstants : register(b0) { float4x4 viewProjection; };"
        "struct VSInput { float3 position : POSITION; float3 normal : NORMAL; float4 color : COLOR; };"
        "struct PSInput { float4 position : SV_POSITION; float3 normal : NORMAL; float4 color : COLOR; };"
        "PSInput VSMain(VSInput input) {"
        "  PSInput output;"
        "  output.position = mul(float4(input.position, 1.0), viewProjection);"
        "  output.normal = input.normal;"
        "  output.color = input.color;"
        "  return output;"
        "}"
        "float4 PSMain(PSInput input) : SV_TARGET {"
        "  const float3 lightDir = normalize(float3(-0.5, 1.0, -0.35));"
        "  const float3 normal = normalize(input.normal);"
        "  const float diffuse = saturate(dot(normal, lightDir));"
        "  const float lighting = 0.28 + diffuse * 0.72;"
        "  return float4(input.color.rgb * lighting, input.color.a);"
        "}";

    ComPtr<ID3DBlob> vertex_shader;
    ComPtr<ID3DBlob> pixel_shader;
    ComPtr<ID3DBlob> error_blob;
    D3DCompile(shader_source, std::strlen(shader_source), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vertex_shader, &error_blob);
    D3DCompile(shader_source, std::strlen(shader_source), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &pixel_shader, &error_blob);

    D3D12_ROOT_PARAMETER root_parameter{};
    root_parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_parameter.Descriptor.ShaderRegister = 0;
    root_parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC root_signature_desc{};
    root_signature_desc.NumParameters = 1;
    root_signature_desc.pParameters = &root_parameter;
    root_signature_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error_blob);
    device_->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root_signature_));

    const D3D12_INPUT_ELEMENT_DESC input_layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    rasterizer.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc{};
    pso_desc.InputLayout = {input_layout, static_cast<UINT>(std::size(input_layout))};
    pso_desc.pRootSignature = root_signature_.Get();
    pso_desc.VS = {vertex_shader->GetBufferPointer(), vertex_shader->GetBufferSize()};
    pso_desc.PS = {pixel_shader->GetBufferPointer(), pixel_shader->GetBufferSize()};
    pso_desc.RasterizerState = rasterizer;
    pso_desc.BlendState = blend;
    pso_desc.DepthStencilState.DepthEnable = FALSE;
    pso_desc.DepthStencilState.StencilEnable = FALSE;
    pso_desc.SampleMask = UINT_MAX;
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso_desc.NumRenderTargets = 1;
    pso_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso_desc.SampleDesc.Count = 1;
    device_->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state_));

    const std::vector<Vertex> vertices = build_visible_mesh();
    vertex_count_ = static_cast<UINT>(vertices.size());
    const UINT vertex_buffer_size = static_cast<UINT>(vertices.size() * sizeof(Vertex));

    D3D12_HEAP_PROPERTIES upload_heap{};
    upload_heap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = vertex_buffer_size;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device_->CreateCommittedResource(
        &upload_heap,
        D3D12_HEAP_FLAG_NONE,
        &buffer_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertex_buffer_));

    void *mapped_data = nullptr;
    D3D12_RANGE read_range{0, 0};
    vertex_buffer_->Map(0, &read_range, &mapped_data);
    std::memcpy(mapped_data, vertices.data(), vertex_buffer_size);
    vertex_buffer_->Unmap(0, nullptr);

    vertex_buffer_view_.BufferLocation = vertex_buffer_->GetGPUVirtualAddress();
    vertex_buffer_view_.StrideInBytes = sizeof(Vertex);
    vertex_buffer_view_.SizeInBytes = vertex_buffer_size;

    buffer_desc.Width = (sizeof(SceneConstants) + 255) & ~255;
    device_->CreateCommittedResource(
        &upload_heap,
        D3D12_HEAP_FLAG_NONE,
        &buffer_desc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&constant_buffer_));

    constant_buffer_->Map(0, &read_range, reinterpret_cast<void **>(&constant_buffer_data_));

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{};
    cbv_desc.BufferLocation = constant_buffer_->GetGPUVirtualAddress();
    cbv_desc.SizeInBytes = (sizeof(SceneConstants) + 255) & ~255;
    device_->CreateConstantBufferView(&cbv_desc, cbv_heap_->GetCPUDescriptorHandleForHeapStart());
}

void D3D12App::update()
{
    const XMVECTOR target = XMVectorSet(0.0f, 3.5f, 0.0f, 1.0f);
    const float cx = std::cosf(camera_.yaw);
    const float sx = std::sinf(camera_.yaw);
    const float cy = std::cosf(camera_.pitch);
    const float sy = std::sinf(camera_.pitch);

    XMFLOAT3 target_position{};
    XMStoreFloat3(&target_position, target);
    const XMVECTOR eye = XMVectorSet(
        target_position.x + camera_.distance * cx * cy,
        target_position.y + camera_.distance * sy,
        target_position.z + camera_.distance * sx * cy,
        1.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, target, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const XMMATRIX projection = XMMatrixPerspectiveFovLH(pi / 4.0f, static_cast<float>(width_) / static_cast<float>(height_), 0.1f, 100.0f);
    const XMMATRIX view_projection = XMMatrixTranspose(view * projection);
    XMStoreFloat4x4(&constant_buffer_data_->view_projection, view_projection);
}

void D3D12App::render()
{
    frames_[frame_index_].allocator->Reset();
    command_list_->Reset(frames_[frame_index_].allocator.Get(), pipeline_state_.Get());

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = render_targets_[frame_index_].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    command_list_->ResourceBarrier(1, &barrier);

    D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_), 0.0f, 1.0f};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    command_list_->RSSetViewports(1, &viewport);
    command_list_->RSSetScissorRects(1, &scissor);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += frame_index_ * rtv_descriptor_size_;
    command_list_->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    const float clear_color[] = {0.08f, 0.09f, 0.12f, 1.0f};
    command_list_->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
    command_list_->SetGraphicsRootSignature(root_signature_.Get());
    ID3D12DescriptorHeap *heaps[] = {cbv_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->SetGraphicsRootConstantBufferView(0, constant_buffer_->GetGPUVirtualAddress());
    command_list_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    command_list_->IASetVertexBuffers(0, 1, &vertex_buffer_view_);
    command_list_->DrawInstanced(vertex_count_, 1, 0, 0);

    D3D12_RESOURCE_BARRIER barrier_back{};
    barrier_back.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier_back.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier_back.Transition.pResource = render_targets_[frame_index_].Get();
    barrier_back.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier_back.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier_back.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    command_list_->ResourceBarrier(1, &barrier_back);

    command_list_->Close();
    ID3D12CommandList *command_lists[] = {command_list_.Get()};
    command_queue_->ExecuteCommandLists(1, command_lists);
    swap_chain_->Present(1, 0);
    move_to_next_frame();
}

void D3D12App::wait_for_gpu()
{
    command_queue_->Signal(fence_.Get(), fence_value_);
    fence_->SetEventOnCompletion(fence_value_, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
    ++fence_value_;
}

void D3D12App::move_to_next_frame()
{
    const UINT64 current_fence = fence_value_;
    command_queue_->Signal(fence_.Get(), current_fence);
    frames_[frame_index_].fence_value = current_fence;
    ++fence_value_;

    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
    if (fence_->GetCompletedValue() < frames_[frame_index_].fence_value)
    {
        fence_->SetEventOnCompletion(frames_[frame_index_].fence_value, fence_event_);
        WaitForSingleObject(fence_event_, INFINITE);
    }
}

void D3D12App::on_mouse_down(LPARAM lparam)
{
    camera_.dragging = true;
    camera_.last_mouse.x = GET_X_LPARAM(lparam);
    camera_.last_mouse.y = GET_Y_LPARAM(lparam);
    SetCapture(hwnd_);
}

void D3D12App::on_mouse_up()
{
    camera_.dragging = false;
    ReleaseCapture();
}

void D3D12App::on_mouse_move(WPARAM buttons, LPARAM lparam)
{
    if (!camera_.dragging || (buttons & MK_LBUTTON) == 0)
    {
        return;
    }

    const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    const int dx = point.x - camera_.last_mouse.x;
    const int dy = point.y - camera_.last_mouse.y;
    camera_.last_mouse = point;

    camera_.yaw += static_cast<float>(dx) * 0.01f;
    camera_.pitch = std::clamp(camera_.pitch - static_cast<float>(dy) * 0.01f, -1.2f, 1.2f);
}

void D3D12App::on_mouse_wheel(WPARAM wparam)
{
    const short wheel_delta = GET_WHEEL_DELTA_WPARAM(wparam);
    camera_.distance = std::clamp(camera_.distance - static_cast<float>(wheel_delta) * 0.005f, 8.0f, 48.0f);
}

LRESULT D3D12App::handle_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_LBUTTONDOWN:
        on_mouse_down(lparam);
        return 0;
    case WM_LBUTTONUP:
        on_mouse_up();
        return 0;
    case WM_MOUSEMOVE:
        on_mouse_move(wparam, lparam);
        return 0;
    case WM_MOUSEWHEEL:
        on_mouse_wheel(wparam);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

LRESULT CALLBACK D3D12App::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_NCCREATE)
    {
        const auto *create_struct = reinterpret_cast<CREATESTRUCT *>(lparam);
        auto *app = static_cast<D3D12App *>(create_struct->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    auto *app = reinterpret_cast<D3D12App *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    return app ? app->handle_message(hwnd, msg, wparam, lparam) : DefWindowProc(hwnd, msg, wparam, lparam);
}
} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    try
    {
        D3D12App app;
        return app.run(instance);
    }
    catch (const std::exception &exception)
    {
        MessageBoxA(nullptr, exception.what(), "Voxel D3D12 001 failed", MB_OK | MB_ICONERROR);
        return 1;
    }
}
