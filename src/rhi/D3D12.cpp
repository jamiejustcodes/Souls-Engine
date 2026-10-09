#include "Capture.hpp"
#include "rhi/NativeAccess.hpp"
#include <SDL3/SDL.h>
#include <bitset>
#include <cstdio>
#include <cstring>
#include <new>
#include <wrl/client.h>

namespace souls::rhi {
using Microsoft::WRL::ComPtr;
namespace {
constexpr std::uint32_t texture_capacity = 32, srv_capacity = 1024;
constexpr std::uint32_t constant_stride =
    (sizeof(FrameConstants) + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) /
    D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT * D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
Error gpu_error(const char *message, HRESULT result) noexcept {
    return {ErrorCode::gpu, message, result};
}
D3D12_BARRIER_LAYOUT layout(Access access) noexcept {
    switch (access) {
    case Access::render_target:
        return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    case Access::shader_read:
        return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    case Access::present:
        return D3D12_BARRIER_LAYOUT_PRESENT;
    default:
        return D3D12_BARRIER_LAYOUT_UNDEFINED;
    }
}
D3D12_BARRIER_ACCESS access_mask(Access access) noexcept {
    switch (access) {
    case Access::render_target:
        return D3D12_BARRIER_ACCESS_RENDER_TARGET;
    case Access::shader_read:
        return D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    default:
        return D3D12_BARRIER_ACCESS_NO_ACCESS;
    }
}
D3D12_BARRIER_SYNC sync(Stage stage) noexcept {
    switch (stage) {
    case Stage::color_output:
        return D3D12_BARRIER_SYNC_RENDER_TARGET;
    case Stage::fragment:
        return D3D12_BARRIER_SYNC_PIXEL_SHADING;
    case Stage::all:
        return D3D12_BARRIER_SYNC_ALL;
    default:
        return D3D12_BARRIER_SYNC_NONE;
    }
}
void transition(ID3D12GraphicsCommandList7 *cmd, ID3D12Resource *resource, Stage before, Stage after,
                Access from, Access to) noexcept {
    D3D12_TEXTURE_BARRIER b{};
    b.SyncBefore = sync(before);
    b.SyncAfter = sync(after);
    b.AccessBefore = access_mask(from);
    b.AccessAfter = access_mask(to);
    b.LayoutBefore = layout(from);
    b.LayoutAfter = layout(to);
    b.pResource = resource;
    b.Subresources = {0, 1, 0, 1, 0, 1};
    b.Flags =
        from == Access::undefined ? D3D12_TEXTURE_BARRIER_FLAG_DISCARD : D3D12_TEXTURE_BARRIER_FLAG_NONE;
    D3D12_BARRIER_GROUP group{};
    group.Type = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers = 1;
    group.pTextureBarriers = &b;
    cmd->Barrier(1, &group);
}
} // namespace
struct Device::Impl {
    struct Texture {
        ComPtr<ID3D12Resource> resource, depth;
        ComPtr<ID3D12DescriptorHeap> dsv;
        Extent extent{};
        bool depth_ready = false;
        detail::TextureView srv{};
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        Access state = Access::undefined;
    };
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList7> command;
        ComPtr<ID3D12Resource> constants;
        void *mapped = nullptr;
        std::uint32_t constant_count = 0;
        std::uint64_t fence = 0;
        std::array<std::byte, 512 * 1024> storage{};
        LinearArena arena{storage};
    };
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter3> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue> debug_messages;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> chain;
    ComPtr<ID3D12DescriptorHeap> rtvs, srvs;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12QueryHeap> queries;
    ComPtr<ID3D12Resource> readback;
    std::array<ComPtr<ID3D12Resource>, frames_in_flight> backbuffers;
    std::array<Frame, frames_in_flight> frames;
    Pool<Texture, TextureTag, texture_capacity> textures;
    struct Buffer {
        ComPtr<ID3D12Resource> resource;
        std::uint32_t size = 0, max_index = 0;
        BufferUsage usage = BufferUsage::vertex;
    };
    struct Pipeline {
        ComPtr<ID3D12PipelineState> state;
        bool grid = false;
    };
    Pool<Buffer, BufferTag, 128> buffers;
    Pool<Pipeline, PipelineTag, 16> pipelines;
    ComPtr<ID3D12RootSignature> root;
    TextureHandle geometry_target{};
    std::bitset<srv_capacity> descriptors;
    HANDLE event = nullptr;
    std::uint32_t rtv_stride = 0, srv_stride = 0, slot = 0;
    std::uint64_t timeline = 0, serial = 0, frequency = 0;
    Extent extent{};
    Stats telemetry{};
    bool open = false, vsync = true;
    std::array<char, 256> name{};
    ~Impl() {
        if (event)
            CloseHandle(event);
    }
    Result<void> check_validation() noexcept {
        if (!debug_messages)
            return {};
        const auto count = debug_messages->GetNumStoredMessagesAllowedByRetrievalFilter();
        bool failed = false;
        for (UINT64 i = 0; i < count; ++i) {
            alignas(D3D12_MESSAGE) std::array<std::byte, 4096> storage{};
            SIZE_T length = storage.size();
            auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());
            if (FAILED(debug_messages->GetMessage(i, message, &length))) {
                failed = true;
                continue;
            }
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                std::fprintf(stderr, "D3D12 validation: %s\n", message->pDescription);
                failed = true;
            }
        }
        debug_messages->ClearStoredMessages();
        if (failed)
            return std::unexpected(Error{ErrorCode::gpu, "D3D12 validation error; see log"});
        return {};
    }
    Result<void> wait_value(std::uint64_t value) noexcept {
        if (fence->GetCompletedValue() == UINT64_MAX)
            return std::unexpected(gpu_error("D3D12 device removed", device->GetDeviceRemovedReason()));
        if (value > fence->GetCompletedValue()) {
            auto hr = fence->SetEventOnCompletion(value, event);
            if (FAILED(hr))
                return std::unexpected(gpu_error("Fence wait registration failed", hr));
            if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
                return std::unexpected(Error{ErrorCode::gpu, "GPU fence timed out"});
        }
        return {};
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(std::uint32_t index) const noexcept {
        auto h = rtvs->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(index) * rtv_stride;
        return h;
    }
    Result<detail::TextureView> allocate_srv() noexcept {
        for (std::uint32_t i = 0; i < srv_capacity; ++i)
            if (!descriptors.test(i)) {
                descriptors.set(i);
                auto cpu = srvs->GetCPUDescriptorHandleForHeapStart();
                cpu.ptr += static_cast<SIZE_T>(i) * srv_stride;
                auto gpu = srvs->GetGPUDescriptorHandleForHeapStart();
                gpu.ptr += static_cast<UINT64>(i) * srv_stride;
                return detail::TextureView{cpu, gpu};
            }
        return std::unexpected(Error{ErrorCode::exhausted, "Bindless SRV heap exhausted"});
    }
    void free_srv(detail::TextureView view) noexcept {
        const auto index = (view.cpu.ptr - srvs->GetCPUDescriptorHandleForHeapStart().ptr) / srv_stride;
        assert(index < srv_capacity && descriptors.test(index));
        descriptors.reset(index);
    }
    Result<void> acquire_buffers() noexcept {
        for (std::uint32_t i = 0; i < frames_in_flight; ++i) {
            auto hr = chain->GetBuffer(i, IID_PPV_ARGS(&backbuffers[i]));
            if (FAILED(hr))
                return std::unexpected(gpu_error("GetBuffer failed", hr));
            device->CreateRenderTargetView(backbuffers[i].Get(), nullptr, rtv(i));
        }
        return {};
    }
};
Device::Device() noexcept = default;
Device::Device(Device &&) noexcept = default;
Device &Device::operator=(Device &&other) noexcept {
    if (this != &other) {
        if (impl_) {
            const auto ignored = wait_idle();
            (void)ignored;
        }
        impl_ = std::move(other.impl_);
    }
    return *this;
}
Device::~Device() {
    if (impl_ && impl_->queue && impl_->fence && impl_->event) {
        const auto ignored = wait_idle();
        (void)ignored;
    }
}
Result<Device> Device::create(DeviceDesc desc) noexcept {
    if (!desc.window)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Device requires a window"});
    Device result;
    result.impl_.reset(new (std::nothrow) Impl);
    if (!result.impl_)
        return std::unexpected(Error{ErrorCode::exhausted, "Device storage allocation failed"});
    auto &p = *result.impl_;
    p.vsync = desc.vsync;
    UINT factory_flags = 0;
    if (desc.validation) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        } else
            return std::unexpected(Error{ErrorCode::unsupported, "D3D12 debug layer unavailable"});
    }
    auto hr = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&p.factory));
    if (FAILED(hr))
        return std::unexpected(gpu_error("DXGI factory creation failed", hr));
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> candidate;
        hr = p.factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                   IID_PPV_ARGS(&candidate));
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
            return std::unexpected(gpu_error("Adapter enumeration failed", hr));
        DXGI_ADAPTER_DESC1 info{};
        candidate->GetDesc1(&info);
        if (info.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;
        ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))))
            continue;
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 options{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options, sizeof(options))) ||
            !options.EnhancedBarriersSupported)
            continue;
        if (FAILED(candidate.As(&p.adapter)))
            continue;
        p.device = std::move(device);
        WideCharToMultiByte(CP_UTF8, 0, info.Description, -1, p.name.data(), static_cast<int>(p.name.size()),
                            nullptr, nullptr);
        break;
    }
    if (!p.device)
        return std::unexpected(Error{ErrorCode::unsupported, "No D3D12 adapter with enhanced barriers"});
    if (desc.validation) {
        hr = p.device.As(&p.debug_messages);
        if (FAILED(hr))
            return std::unexpected(gpu_error("D3D12 info queue unavailable", hr));
    }
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = p.device->CreateCommandQueue(&q, IID_PPV_ARGS(&p.queue));
    if (FAILED(hr))
        return std::unexpected(gpu_error("Queue creation failed", hr));
    hr = p.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&p.fence));
    if (FAILED(hr))
        return std::unexpected(gpu_error("Fence creation failed", hr));
    p.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!p.event)
        return std::unexpected(Error{ErrorCode::platform, "Fence event creation failed", GetLastError()});
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(desc.window, &width, &height))
        return std::unexpected(Error{ErrorCode::platform, SDL_GetError()});
    p.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = p.extent.width;
    sc.Height = p.extent.height;
    sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = frames_in_flight;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    auto hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(desc.window),
                                                         SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd)
        return std::unexpected(Error{ErrorCode::platform, "SDL did not provide an HWND"});
    ComPtr<IDXGISwapChain1> chain;
    hr = p.factory->CreateSwapChainForHwnd(p.queue.Get(), hwnd, &sc, nullptr, nullptr, &chain);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Swapchain creation failed", hr));
    hr = chain.As(&p.chain);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Swapchain interface unavailable", hr));
    hr = p.factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Window association failed", hr));
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = frames_in_flight + texture_capacity;
    hr = p.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&p.rtvs));
    if (FAILED(hr))
        return std::unexpected(gpu_error("RTV heap creation failed", hr));
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = srv_capacity;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = p.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&p.srvs));
    if (FAILED(hr))
        return std::unexpected(gpu_error("SRV heap creation failed", hr));
    p.rtv_stride = p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    p.srv_stride = p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (auto buffers = p.acquire_buffers(); !buffers)
        return std::unexpected(buffers.error());
    for (auto &f : p.frames) {
        hr = p.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator));
        if (FAILED(hr))
            return std::unexpected(gpu_error("Command allocator creation failed", hr));
        hr = p.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, f.allocator.Get(), nullptr,
                                         IID_PPV_ARGS(&f.command));
        if (FAILED(hr))
            return std::unexpected(gpu_error("Command list7 creation failed", hr));
        hr = f.command->Close();
        if (FAILED(hr))
            return std::unexpected(gpu_error("Command close failed", hr));
    }
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = frames_in_flight * 2;
    hr = p.device->CreateQueryHeap(&query, IID_PPV_ARGS(&p.queries));
    if (FAILED(hr))
        return std::unexpected(gpu_error("Timestamp heap creation failed", hr));
    hr = p.queue->GetTimestampFrequency(&p.frequency);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Timestamp frequency unavailable", hr));
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = frames_in_flight * 2 * sizeof(std::uint64_t);
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    hr =
        p.device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &buffer,
                                          D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&p.readback));
    if (FAILED(hr))
        return std::unexpected(gpu_error("Timestamp readback creation failed", hr));
    return result;
}
Result<void> Device::wait(TimelinePoint point) noexcept {
    if (point.value > impl_->timeline)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Timeline point has not been submitted"});
    return impl_->wait_value(point.value);
}
Result<void> Device::wait_idle() noexcept {
    auto &p = *impl_;
    if (p.open)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Cannot drain an open frame"});
    auto hr = p.queue->Signal(p.fence.Get(), ++p.timeline);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Queue signal failed", hr));
    if (auto ready = p.wait_value(p.timeline); !ready)
        return ready;
    return p.check_validation();
}
Result<void> Device::resize(Extent pixels) noexcept {
    auto &p = *impl_;
    if (p.open)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Resize requires a closed frame"});
    if (!pixels.width || !pixels.height) {
        p.extent = pixels;
        return {};
    }
    if (auto idle = wait_idle(); !idle)
        return idle;
    for (auto &buffer : p.backbuffers)
        buffer.Reset();
    auto hr =
        p.chain->ResizeBuffers(frames_in_flight, pixels.width, pixels.height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    if (FAILED(hr))
        return std::unexpected(gpu_error("ResizeBuffers failed", hr));
    p.extent = pixels;
    return p.acquire_buffers();
}
Result<CommandList> Device::begin_frame(Color clear) noexcept {
    auto &p = *impl_;
    if (p.open)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Frame already open"});
    if (auto valid = p.check_validation(); !valid)
        return std::unexpected(valid.error());
    if (!p.extent.width || !p.extent.height)
        return std::unexpected(Error{ErrorCode::suspended, "Window minimized"});
    p.slot = p.chain->GetCurrentBackBufferIndex();
    auto &f = p.frames[p.slot];
    if (auto ready = p.wait_value(f.fence); !ready)
        return std::unexpected(ready.error());
    if (f.fence) {
        void *mapped = nullptr;
        D3D12_RANGE range{p.slot * 2 * sizeof(std::uint64_t), (p.slot * 2 + 2) * sizeof(std::uint64_t)};
        auto hr = p.readback->Map(0, &range, &mapped);
        if (FAILED(hr))
            return std::unexpected(gpu_error("Timestamp mapping failed", hr));
        auto *times = static_cast<std::uint64_t *>(mapped) + p.slot * 2;
        p.telemetry.gpu_us =
            static_cast<double>(times[1] - times[0]) * 1.0e6 / static_cast<double>(p.frequency);
        p.telemetry.gpu_timing_valid = true;
        D3D12_RANGE written{0, 0};
        p.readback->Unmap(0, &written);
    }
    auto hr = f.allocator->Reset();
    if (FAILED(hr))
        return std::unexpected(gpu_error("Command allocator reset failed", hr));
    hr = f.command->Reset(f.allocator.Get(), nullptr);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Command reset failed", hr));
    f.arena.reset();
    f.constant_count = 0;
    ++p.serial;
    p.open = true;
    f.command->EndQuery(p.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, p.slot * 2);
    transition(f.command.Get(), p.backbuffers[p.slot].Get(), Stage::none, Stage::color_output,
               Access::present, Access::render_target);
    const auto target = p.rtv(p.slot);
    const float color[]{clear.r, clear.g, clear.b, clear.a};
    f.command->ClearRenderTargetView(target, color, 0, nullptr);
    f.command->OMSetRenderTargets(1, &target, FALSE, nullptr);
    auto *heap = p.srvs.Get();
    f.command->SetDescriptorHeaps(1, &heap);
    return CommandList{f.command.Get(), p.serial};
}
Result<TimelinePoint> Device::end_frame(CommandList list) noexcept {
    auto &p = *impl_;
    auto &f = p.frames[p.slot];
    if (!p.open || list.serial != p.serial || list.native != f.command.Get())
        return std::unexpected(Error{ErrorCode::invalid_argument, "Expired command token"});
    if (p.geometry_target)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Geometry scope still open"});
    transition(f.command.Get(), p.backbuffers[p.slot].Get(), Stage::color_output, Stage::none,
               Access::render_target, Access::present);
    f.command->EndQuery(p.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, p.slot * 2 + 1);
    f.command->ResolveQueryData(p.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, p.slot * 2, 2, p.readback.Get(),
                                p.slot * 2 * sizeof(std::uint64_t));
    auto hr = f.command->Close();
    p.open = false;
    if (FAILED(hr))
        return std::unexpected(gpu_error("Command close failed", hr));
    ID3D12CommandList *commands[]{f.command.Get()};
    p.queue->ExecuteCommandLists(1, commands);
    // Always signal submitted work even if presentation fails.
    hr = p.queue->Signal(p.fence.Get(), ++p.timeline);
    f.fence = p.timeline;
    if (FAILED(hr))
        return std::unexpected(gpu_error("Frame signal failed", hr));
    hr = p.chain->Present(p.vsync ? 1 : 0, 0);
    if (FAILED(hr))
        return std::unexpected(gpu_error("Present failed", hr));
    return TimelinePoint{p.timeline};
}
Result<TextureHandle> Device::create_render_target(Extent pixels) noexcept {
    auto &p = *impl_;
    if (p.open || !pixels.width || !pixels.height)
        return std::unexpected(
            Error{ErrorCode::invalid_argument,
                  "Render target creation requires positive dimensions and a closed frame"});
    auto handle = p.textures.emplace();
    if (!handle)
        return std::unexpected(handle.error());
    auto &t = *p.textures.get(*handle);
    auto srv = p.allocate_srv();
    if (!srv) {
        const auto ignored = p.textures.erase(*handle);
        (void)ignored;
        return std::unexpected(srv.error());
    }
    t.srv = *srv;
    t.rtv = p.rtv(frames_in_flight + handle->index);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = pixels.width;
    desc.Height = pixels.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    auto hr = p.device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&t.resource));
    if (FAILED(hr)) {
        p.free_srv(t.srv);
        const auto ignored = p.textures.erase(*handle);
        (void)ignored;
        return std::unexpected(gpu_error("Viewport texture creation failed", hr));
    }
    p.device->CreateRenderTargetView(t.resource.Get(), nullptr, t.rtv);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture2D.MipLevels = 1;
    p.device->CreateShaderResourceView(t.resource.Get(), &view, t.srv.cpu);
    t.extent = pixels;
    D3D12_DESCRIPTOR_HEAP_DESC dsv{};
    dsv.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsv.NumDescriptors = 1;
    hr = p.device->CreateDescriptorHeap(&dsv, IID_PPV_ARGS(&t.dsv));
    if (SUCCEEDED(hr)) {
        desc.Format = DXGI_FORMAT_D32_FLOAT;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = desc.Format;
        clear.DepthStencil.Depth = 1;
        hr = p.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, &clear, IID_PPV_ARGS(&t.depth));
    }
    if (FAILED(hr)) {
        p.free_srv(t.srv);
        const auto ignored = p.textures.erase(*handle);
        (void)ignored;
        return std::unexpected(gpu_error("Viewport depth creation failed", hr));
    }
    p.device->CreateDepthStencilView(t.depth.Get(), nullptr, t.dsv->GetCPUDescriptorHandleForHeapStart());
    return *handle;
}
Result<void> Device::destroy_texture(TextureHandle texture) noexcept {
    auto &p = *impl_;
    auto *t = p.textures.get(texture);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    if (auto idle = wait_idle(); !idle)
        return idle;
    p.free_srv(t->srv);
    return p.textures.erase(texture);
}
Result<void> Device::barrier(CommandList list, TextureBarrier b) noexcept {
    auto &p = *impl_;
    auto *t = p.textures.get(b.texture);
    if (!p.open || list.serial != p.serial || list.native != p.frames[p.slot].command.Get())
        return std::unexpected(Error{ErrorCode::invalid_argument, "Expired command token"});
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    if (t->state != b.from || b.to == Access::undefined || b.to == Access::present)
        return std::unexpected(Error{ErrorCode::invalid_argument, "Invalid texture barrier state"});
    transition(p.frames[p.slot].command.Get(), t->resource.Get(), b.before, b.after, b.from, b.to);
    t->state = b.to;
    return {};
}
Result<void> Device::clear_texture(CommandList list, TextureHandle texture, Color clear) noexcept {
    auto *t = impl_->textures.get(texture);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    const auto from = t->state;
    if (auto ready = barrier(list, {texture, from == Access::shader_read ? Stage::fragment : Stage::none,
                                    Stage::color_output, from, Access::render_target});
        !ready)
        return ready;
    const float color[]{clear.r, clear.g, clear.b, clear.a};
    impl_->frames[impl_->slot].command->ClearRenderTargetView(t->rtv, color, 0, nullptr);
    return barrier(
        list, {texture, Stage::color_output, Stage::fragment, Access::render_target, Access::shader_read});
}
Swapchain Device::swapchain() const noexcept {
    return {impl_->extent, frames_in_flight};
}
Stats Device::stats() const noexcept {
    auto stats = impl_->telemetry;
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (SUCCEEDED(impl_->adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
        stats.local_usage = memory.CurrentUsage;
        stats.local_budget = memory.Budget;
        stats.budget_valid = true;
    }
    return stats;
}
LinearArena &Device::frame_arena() noexcept {
    assert(impl_->open);
    return impl_->frames[impl_->slot].arena;
}
const char *Device::adapter_name() const noexcept {
    return impl_->name.data();
}
detail::NativeContext detail::NativeAccess::context(Device &device) noexcept {
    auto &p = *device.impl_;
    return {p.device.Get(), p.queue.Get(), p.srvs.Get(), DXGI_FORMAT_R8G8B8A8_UNORM};
}
Result<detail::TextureView> detail::NativeAccess::texture(Device &device, TextureHandle handle) noexcept {
    auto *t = device.impl_->textures.get(handle);
    if (!t)
        return std::unexpected(Error{ErrorCode::invalid_handle, "Invalid texture"});
    return t->srv;
}
Result<detail::TextureView> detail::NativeAccess::allocate_descriptor(Device &device) noexcept {
    return device.impl_->allocate_srv();
}
void detail::NativeAccess::free_descriptor(Device &device, TextureView view) noexcept {
    device.impl_->free_srv(view);
}
void detail::NativeAccess::begin_overlay(Device &device, CommandList) noexcept {
    auto &p = *device.impl_;
    auto rtv = p.rtv(p.slot);
    p.frames[p.slot].command->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
}
void detail::NativeAccess::end_overlay(Device &, CommandList) noexcept {}
#include "D3D12Geometry.inl"
} // namespace souls::rhi
