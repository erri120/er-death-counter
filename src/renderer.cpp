#include "renderer.hpp"

#include <atomic>
#include <cstring>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>

#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

#include "config.hpp"
#include "logger.hpp"

extern const unsigned char font_data[];
extern const unsigned font_data_size;

namespace {

constexpr std::size_t kDeviceVtblEntries      = 44;
constexpr std::size_t kQueueVtblEntries       = 19;
constexpr std::size_t kAllocatorVtblEntries   = 9;
constexpr std::size_t kCommandListVtblEntries = 60;
constexpr std::size_t kSwapChainVtblEntries   = 18;
constexpr std::size_t kMethodTableSize        = 150;

constexpr std::size_t kQueueVtblBase          = kDeviceVtblEntries;
constexpr std::size_t kAllocatorVtblBase      = kQueueVtblBase + kQueueVtblEntries;
constexpr std::size_t kCommandListVtblBase    = kAllocatorVtblBase + kAllocatorVtblEntries;
constexpr std::size_t kSwapChainVtblBase      = kCommandListVtblBase + kCommandListVtblEntries;

constexpr std::size_t kExecuteCommandListsIdx = kQueueVtblBase + 10;
constexpr std::size_t kPresentIdx             = kSwapChainVtblBase + 8;
constexpr std::size_t kResizeBuffersIdx       = kSwapChainVtblBase + 13;

constexpr std::size_t kMaxBuffers = 8;

void* methods_table[kMethodTableSize] = {};

using PresentFn              = HRESULT(APIENTRY*)(IDXGISwapChain*, UINT, UINT);
using ExecuteCommandListsFn  = void(APIENTRY*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResizeBuffersFn        = HRESULT(APIENTRY*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

PresentFn             o_present                = nullptr;
ExecuteCommandListsFn o_execute_command_lists  = nullptr;
ResizeBuffersFn       o_resize_buffers         = nullptr;

ID3D12CommandQueue*       game_queue          = nullptr;
ID3D12Device*             device              = nullptr;
ID3D12DescriptorHeap*     rtv_heap            = nullptr;
ID3D12DescriptorHeap*     srv_heap            = nullptr;
ID3D12Resource*           back_buffers[kMaxBuffers] = {};
D3D12_CPU_DESCRIPTOR_HANDLE render_targets[kMaxBuffers] = {};
ID3D12CommandAllocator*   command_allocators[kMaxBuffers] = {};
ID3D12GraphicsCommandList* command_list       = nullptr;
UINT                      buffer_count        = 0;
bool                      imgui_ready         = false;
bool                      init_failed_logged  = false;

std::atomic<std::uint32_t> death_count{0};
std::atomic<bool>          death_count_valid{false};
std::atomic<std::uint32_t> session_count{0};
std::atomic<std::uint32_t> boss_tries{0};
std::atomic<bool>          boss_visible{false};

config::Display display_config{};

std::atomic<bool> shutting_down{false};

struct DummyWindow {
    WNDCLASSEXW cls{};
    HWND        hwnd = nullptr;
};

DummyWindow dummy_window{};

bool init_dummy_window() {
    dummy_window.cls.cbSize        = sizeof(WNDCLASSEXW);
    dummy_window.cls.style         = CS_HREDRAW | CS_VREDRAW;
    dummy_window.cls.lpfnWndProc   = DefWindowProcW;
    dummy_window.cls.hInstance     = ::GetModuleHandleW(nullptr);
    dummy_window.cls.lpszClassName = L"death_counter_dummy";
    if (::RegisterClassExW(&dummy_window.cls) == 0) {
        return false;
    }
    dummy_window.hwnd = ::CreateWindowExW(
        0, dummy_window.cls.lpszClassName, L"death_counter_dummy",
        WS_OVERLAPPED, 0, 0, 100, 100,
        nullptr, nullptr, dummy_window.cls.hInstance, nullptr);
    return dummy_window.hwnd != nullptr;
}

void delete_dummy_window() {
    if (dummy_window.hwnd != nullptr) {
        ::DestroyWindow(dummy_window.hwnd);
        dummy_window.hwnd = nullptr;
    }
    ::UnregisterClassW(dummy_window.cls.lpszClassName, dummy_window.cls.hInstance);
    dummy_window.cls = {};
}

void reset_render_state() {
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (command_list != nullptr) {
        command_list->Release();
        command_list = nullptr;
    }
    for (UINT i = 0; i < buffer_count; ++i) {
        if (command_allocators[i] != nullptr) {
            command_allocators[i]->Release();
            command_allocators[i] = nullptr;
        }
        if (back_buffers[i] != nullptr) {
            back_buffers[i]->Release();
            back_buffers[i] = nullptr;
        }
    }
    if (rtv_heap != nullptr) {
        rtv_heap->Release();
        rtv_heap = nullptr;
    }
    if (srv_heap != nullptr) {
        srv_heap->Release();
        srv_heap = nullptr;
    }
    if (device != nullptr) {
        device->Release();
        device = nullptr;
    }
    buffer_count  = 0;
    imgui_ready   = false;
}

void srv_alloc(ImGui_ImplDX12_InitInfo*,
               D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
               D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
    static std::size_t index = 0;
    const auto increment = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    *out_cpu = srv_heap->GetCPUDescriptorHandleForHeapStart();
    out_cpu->ptr += static_cast<INT64>(increment * index);
    *out_gpu = srv_heap->GetGPUDescriptorHandleForHeapStart();
    out_gpu->ptr += static_cast<INT64>(increment * index);
    ++index;
}

void srv_free(ImGui_ImplDX12_InitInfo*,
              D3D12_CPU_DESCRIPTOR_HANDLE,
              D3D12_GPU_DESCRIPTOR_HANDLE) {
}

bool init_imgui(IDXGISwapChain3* swap_chain) {
    DXGI_SWAP_CHAIN_DESC sc_desc{};
    if (swap_chain->GetDesc(&sc_desc) < 0) {
        return false;
    }

    buffer_count = sc_desc.BufferCount;
    if (buffer_count == 0 || buffer_count > kMaxBuffers) {
        return false;
    }

    if (swap_chain->GetDevice(IID_PPV_ARGS(&device)) < 0 || device == nullptr) {
        device = nullptr;
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 16;
    srv_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&srv_heap)) < 0) {
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = buffer_count;
    rtv_desc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtv_desc.NodeMask       = 1;
    if (device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&rtv_heap)) < 0) {
        return false;
    }

    const auto rtv_increment = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < buffer_count; ++i) {
        render_targets[i] = rtv_handle;
        rtv_handle.ptr += static_cast<INT64>(rtv_increment);
        if (swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i])) < 0) {
            return false;
        }
        device->CreateRenderTargetView(back_buffers[i], nullptr, render_targets[i]);
    }

    for (UINT i = 0; i < buffer_count; ++i) {
        if (device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&command_allocators[i])) < 0) {
            return false;
        }
    }

    if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  command_allocators[0], nullptr,
                                  IID_PPV_ARGS(&command_list)) < 0 ||
        command_list->Close() < 0) {
        return false;
    }

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;

    if (!ImGui_ImplWin32_Init(sc_desc.OutputWindow)) {
        return false;
    }

    ImGui_ImplDX12_InitInfo info{};
    info.Device            = device;
    info.CommandQueue      = game_queue;
    info.NumFramesInFlight = static_cast<int>(buffer_count);
    info.RTVFormat         = sc_desc.BufferDesc.Format;
    info.DSVFormat         = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = srv_heap;
    info.SrvDescriptorAllocFn = &srv_alloc;
    info.SrvDescriptorFreeFn  = &srv_free;
    if (!ImGui_ImplDX12_Init(&info)) {
        return false;
    }

    const float res     = sc_desc.BufferDesc.Height / 1080.0f;
    const float font_px = 18.0f * display_config.scale * res;

    ImFontConfig font_cfg{};
    font_cfg.FontDataOwnedByAtlas = false;
    auto* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        const_cast<unsigned char*>(font_data),
        static_cast<int>(font_data_size), font_px, &font_cfg);
    if (font == nullptr) {
        logger::log("[death-counter] failed to load embedded font, using default");
    }

    imgui_ready = true;
    logger::log("[death-counter] ImGui initialised (swapchain buffers={})",
                buffer_count);
    return true;
}

void draw_counter() {
    if (!death_count_valid.load(std::memory_order_relaxed)) {
        return;
    }

    const auto  size   = ImGui::GetMainViewport()->Size;
    const float res    = size.y / 1080.0f;
    const float margin = 20.0f * display_config.scale * res;

    ImVec2 pos;
    ImVec2 pivot;
    switch (display_config.corner) {
    case config::Corner::TopLeft:
        pos   = ImVec2{margin, margin};
        pivot = ImVec2{0.0f, 0.0f};
        break;
    case config::Corner::TopRight:
        pos   = ImVec2{size.x - margin, margin};
        pivot = ImVec2{1.0f, 0.0f};
        break;
    case config::Corner::BottomLeft:
        pos   = ImVec2{margin, size.y - margin};
        pivot = ImVec2{0.0f, 1.0f};
        break;
    case config::Corner::BottomRight:
        pos   = ImVec2{size.x - margin, size.y - margin};
        pivot = ImVec2{1.0f, 1.0f};
        break;
    }

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.35f);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_AlwaysAutoResize
        | ImGuiWindowFlags_NoSavedSettings
        | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("death-counter", nullptr, flags)) {
        ImGui::Text("Deaths: %u", death_count.load(std::memory_order_relaxed));
        ImGui::Text("Session: %u", session_count.load(std::memory_order_relaxed));
        if (boss_visible.load(std::memory_order_relaxed)) {
            ImGui::Text("Boss: %u", boss_tries.load(std::memory_order_relaxed));
        }
    }
    ImGui::End();
}

void overlay(IDXGISwapChain3* swap_chain) {
    if (!imgui_ready && !init_imgui(swap_chain)) {
        if (!init_failed_logged) {
            init_failed_logged = true;
            logger::log("[death-counter] ImGui init failed, overlay disabled");
        }
        return;
    }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_counter();
    ImGui::Render();

    const UINT index = swap_chain->GetCurrentBackBufferIndex();
    if (index >= buffer_count) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type                         = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags                        = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource         = back_buffers[index];
    barrier.Transition.Subresource       = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore       = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter        = D3D12_RESOURCE_STATE_RENDER_TARGET;

    command_allocators[index]->Reset();
    command_list->Reset(command_allocators[index], nullptr);
    command_list->ResourceBarrier(1, &barrier);
    command_list->OMSetRenderTargets(1, &render_targets[index], FALSE, nullptr);
    command_list->SetDescriptorHeaps(1, &srv_heap);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    command_list->ResourceBarrier(1, &barrier);
    command_list->Close();

    game_queue->ExecuteCommandLists(1,
        reinterpret_cast<ID3D12CommandList* const*>(&command_list));
}

HRESULT APIENTRY hook_present(IDXGISwapChain* swap_chain, UINT sync_interval,
                              UINT flags) {
    if (!shutting_down.load(std::memory_order_relaxed) && game_queue != nullptr) {
        IDXGISwapChain3* swap_chain3 = nullptr;
        if (swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3)) == S_OK) {
            overlay(swap_chain3);
            swap_chain3->Release();
        }
    }
    return o_present(swap_chain, sync_interval, flags);
}

void APIENTRY hook_execute_command_lists(ID3D12CommandQueue* queue,
                                         UINT num_command_lists,
                                         ID3D12CommandList* const* command_lists) {
    if (game_queue == nullptr && queue != nullptr &&
        queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        game_queue = queue;
        logger::log("[death-counter] captured game command queue");
    }
    o_execute_command_lists(queue, num_command_lists, command_lists);
}

HRESULT APIENTRY hook_resize_buffers(IDXGISwapChain* swap_chain, UINT buffer_count,
                                     UINT width, UINT height, DXGI_FORMAT new_format,
                                     UINT flags) {
    if (imgui_ready) {
        logger::log("[death-counter] swapchain resized, resetting render state");
        reset_render_state();
    }
    return o_resize_buffers(swap_chain, buffer_count, width, height, new_format, flags);
}

bool create_hook(std::size_t index, void* detour, void** original) {
    const auto target = methods_table[index];
    return MH_CreateHook(target, detour, original) == MH_OK &&
           MH_EnableHook(target) == MH_OK;
}

}

namespace renderer {

void set_death_count(std::optional<std::uint32_t> total, std::uint32_t session,
                     std::optional<std::uint32_t> boss) {
    if (total) {
        death_count.store(*total, std::memory_order_relaxed);
        death_count_valid.store(true, std::memory_order_relaxed);
    } else {
        death_count_valid.store(false, std::memory_order_relaxed);
    }
    session_count.store(session, std::memory_order_relaxed);
    if (boss) {
        boss_tries.store(*boss, std::memory_order_relaxed);
        boss_visible.store(true, std::memory_order_relaxed);
    } else {
        boss_visible.store(false, std::memory_order_relaxed);
    }
}

bool init(HINSTANCE dll) {
    display_config = config::load(dll);
    logger::log("[death-counter] config: corner={}, scale={}",
                display_config.corner == config::Corner::TopLeft    ? "top_left"
                : display_config.corner == config::Corner::TopRight ? "top_right"
                : display_config.corner == config::Corner::BottomLeft
                    ? "bottom_left"
                    : "bottom_right",
                display_config.scale);

    if (!init_dummy_window()) {
        logger::log("[death-counter] failed to create dummy window");
        return false;
    }

    IDXGIFactory4* factory = nullptr;
    ID3D12Device*  dummy_device = nullptr;
    ID3D12CommandQueue* dummy_queue = nullptr;
    ID3D12CommandAllocator* dummy_allocator = nullptr;
    ID3D12GraphicsCommandList* dummy_list = nullptr;
    IDXGISwapChain* dummy_swap_chain = nullptr;

    bool ok = false;
    do {
        if (::CreateDXGIFactory1(IID_PPV_ARGS(&factory)) < 0) {
            logger::log("[death-counter] CreateDXGIFactory1 failed");
            break;
        }

        IDXGIAdapter* adapter = nullptr;
        if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND) {
            logger::log("[death-counter] no DXGI adapter found");
            break;
        }

        if (::D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0,
                                IID_PPV_ARGS(&dummy_device)) < 0) {
            logger::log("[death-counter] D3D12CreateDevice failed");
            break;
        }

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (dummy_device->CreateCommandQueue(&queue_desc,
                                             IID_PPV_ARGS(&dummy_queue)) < 0) {
            logger::log("[death-counter] CreateCommandQueue failed");
            break;
        }

        if (dummy_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&dummy_allocator)) < 0) {
            logger::log("[death-counter] CreateCommandAllocator failed");
            break;
        }

        if (dummy_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                            dummy_allocator, nullptr,
                                            IID_PPV_ARGS(&dummy_list)) < 0) {
            logger::log("[death-counter] CreateCommandList failed");
            break;
        }

        DXGI_SWAP_CHAIN_DESC sc_desc{};
        sc_desc.BufferDesc.Width                   = 100;
        sc_desc.BufferDesc.Height                  = 100;
        sc_desc.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
        sc_desc.SampleDesc.Count                   = 1;
        sc_desc.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc_desc.BufferCount                        = 2;
        sc_desc.OutputWindow                       = dummy_window.hwnd;
        sc_desc.Windowed                           = TRUE;
        sc_desc.SwapEffect                         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc_desc.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

        if (factory->CreateSwapChain(dummy_queue, &sc_desc,
                                     &dummy_swap_chain) < 0) {
            logger::log("[death-counter] CreateSwapChain failed");
            break;
        }

        std::memcpy(methods_table, *reinterpret_cast<void***>(dummy_device),
                    kDeviceVtblEntries * sizeof(void*));
        std::memcpy(methods_table + kQueueVtblBase,
                    *reinterpret_cast<void***>(dummy_queue),
                    kQueueVtblEntries * sizeof(void*));
        std::memcpy(methods_table + kAllocatorVtblBase,
                    *reinterpret_cast<void***>(dummy_allocator),
                    kAllocatorVtblEntries * sizeof(void*));
        std::memcpy(methods_table + kCommandListVtblBase,
                    *reinterpret_cast<void***>(dummy_list),
                    kCommandListVtblEntries * sizeof(void*));
        std::memcpy(methods_table + kSwapChainVtblBase,
                    *reinterpret_cast<void***>(dummy_swap_chain),
                    kSwapChainVtblEntries * sizeof(void*));

        ok = true;
    } while (false);

    if (dummy_list != nullptr) {
        dummy_list->Release();
    }
    if (dummy_allocator != nullptr) {
        dummy_allocator->Release();
    }
    if (dummy_queue != nullptr) {
        dummy_queue->Release();
    }
    if (dummy_swap_chain != nullptr) {
        dummy_swap_chain->Release();
    }
    if (dummy_device != nullptr) {
        dummy_device->Release();
    }
    if (factory != nullptr) {
        factory->Release();
    }
    delete_dummy_window();

    if (!ok || MH_Initialize() != MH_OK) {
        logger::log("[death-counter] MinHook init failed");
        return false;
    }

    if (!create_hook(kExecuteCommandListsIdx,
                     reinterpret_cast<void*>(&hook_execute_command_lists),
                     reinterpret_cast<void**>(&o_execute_command_lists)) ||
        !create_hook(kPresentIdx,
                     reinterpret_cast<void*>(&hook_present),
                     reinterpret_cast<void**>(&o_present)) ||
        !create_hook(kResizeBuffersIdx,
                     reinterpret_cast<void*>(&hook_resize_buffers),
                     reinterpret_cast<void**>(&o_resize_buffers))) {
        logger::log("[death-counter] failed to create D3D12 hooks");
        return false;
    }

    logger::log("[death-counter] D3D12 hooks installed");
    return true;
}

void shutdown() {
    shutting_down.store(true, std::memory_order_relaxed);

    MH_DisableHook(methods_table[kExecuteCommandListsIdx]);
    MH_DisableHook(methods_table[kPresentIdx]);
    MH_DisableHook(methods_table[kResizeBuffersIdx]);
    MH_Uninitialize();

    reset_render_state();
}

}
