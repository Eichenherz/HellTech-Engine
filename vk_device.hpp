#pragma once

#include <memory>
#include <functional>
#include <span>
// TODO: build system should update it
#include "vk_mem_alloc.h"
#include "r_vk_resources.hpp"
#include "vk_swapchain.hpp"
#include "vk_descriptors.hpp"

// TODO: move to some header
template<typename T>
using deleter_unique_ptr = std::unique_ptr<T, std::function<void( T& )>>;

// TODO: use handle_map/free list and stable addressing if unique/new is cumbersome
struct vk_device
{
	VmaAllocator allocator;

	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;

	VkQueue			compQueue;
	VkQueue			transfQueue;
	VkQueue			gfxQueue;
	u32				gfxQueueIdx;
	u32				compQueueIdx;
	u32				transfQueueIdx;

	deleter_unique_ptr<vk_buffer> CreateBuffer( const buffer_info& buffInfo );
	deleter_unique_ptr<vk_image> CreateImage( const image_info& imgInfo );

	void DestroyBuffer( vk_buffer& buff );
	void DestroyImage( vk_image& img );

	// NOTE: hardcoded for bindless
	vk_descriptor_manager CreateDescriptorManagerBindless();
	void FlushDescriptorUpdates( std::span<VkWriteDescriptorSet> descriptorUpdates );

	VkDescriptorPool CreateDescriptorPool( std::span<u32> descriptorCount, u32 maxSetCount );
	VkDescriptorSetLayout CreateDescriptorSetLayout( std::span<u32> descriptorCount );
	VkDescriptorSet CreateDescriptorSet( VkDescriptorPool pool, VkDescriptorSetLayout setLayout );
	VkPipelineLayout CreatePipelineLayout( VkDescriptorSetLayout setLayout );
};

vk_device VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf );

