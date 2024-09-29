#pragma once

#include <memory>
#include <functional>
#include <span>
// TODO: build system should update it
#include "vk_mem_alloc.h"
#include "vk_resources.hpp"
#include "vk_swapchain.hpp"
#include "vk_descriptors.hpp"
#include "vk_commands.hpp"

// TODO: move to some header
template<typename T>
using deleter_unique_ptr = std::unique_ptr<T, std::function<void( T& )>>;

// TODO: buffer the pool per frame in flight ?
// TODO: typed queues ?
struct vk_queue
{
	std::vector<VkCommandBuffer> cmdBuffs;
	VkQueue			hndl;
	VkCommandPool   cmdPool;
	u32				index;

	void AllocateCommandBuffers();
	void GetCommandBuffer();
	void Submit( const std::span<VkQueueSubmit2> submits );
	void Present();
};



// TODO: use handle_map/free list and stable addressing if unique/new is cumbersome
struct vk_device
{
	vk_queue graphicsQueue;
	vk_queue computeQueue;
	vk_queue transferQueue;

	VmaAllocator allocator;

	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;


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

