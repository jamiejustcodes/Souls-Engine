#pragma once
#include <souls/rhi/SoulsRHI.hpp>
#if defined(SOULS_RHI_D3D12)
#include <d3d12.h>
#include <dxgi1_6.h>
#else
#include <volk.h>
#endif
namespace souls::rhi::detail {
#if defined(SOULS_RHI_D3D12)
struct NativeContext { ID3D12Device* device; ID3D12CommandQueue* queue; ID3D12DescriptorHeap* srv_heap; DXGI_FORMAT format; };
struct TextureView { D3D12_CPU_DESCRIPTOR_HANDLE cpu; D3D12_GPU_DESCRIPTOR_HANDLE gpu; };
#else
struct NativeContext { VkInstance instance; VkPhysicalDevice physical; VkDevice device; VkQueue queue; std::uint32_t family; VkFormat format; std::uint32_t min_images, images; };
struct TextureView { VkImageView view; };
#endif
// Private integration bridge. Public engine interfaces have no SDK headers or UI types.
struct NativeAccess {
    static NativeContext context(Device& device) noexcept;
    static Result<TextureView> texture(Device& device, TextureHandle handle) noexcept;
    static void begin_overlay(Device& device, CommandList list) noexcept;
    static void end_overlay(Device& device, CommandList list) noexcept;
#if defined(SOULS_RHI_D3D12)
    static Result<TextureView> allocate_descriptor(Device& device) noexcept;
    static void free_descriptor(Device& device, TextureView descriptor) noexcept;
#endif
};
}
