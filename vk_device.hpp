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
#include "vk_shaders.hpp"
#include "vk_pipelines.hpp"
#include "metaprogramming.hpp"

// TODO: buffer VkCommandPool per frame in flight ?
// TODO: multiple sumbits per queue ?
template<vk_queue_type QUEUE_TYPE>
struct vk_queue
{
	VkQueue			hndl;
	VkCommandPool   cmdPool;
	u32				index;

	void Submit( std::initializer_list<vk_command_buffer<QUEUE_TYPE>> cmdBuffers,
				 std::initializer_list<VkSemaphore> waitSemas,
				 std::initializer_list<VkSemaphore> signalSemas,
				 std::initializer_list<u64> signalValues,
				 VkPipelineStageFlags waitDstStageMsk );
	void Present( VkSemaphore renderingCompleteSema, const vk_swapchain& swapchain, u32 swapchainImgIdx )
		requires ( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
};


template<vk_queue_type QUEUE_TYPE>
void vk_queue<QUEUE_TYPE>::Submit( 
	std::initializer_list<vk_command_buffer<QUEUE_TYPE>> cmdBuffers,
	std::initializer_list<VkSemaphore> waitSemas,
	std::initializer_list<VkSemaphore> signalSemas,
	std::initializer_list<u64> signalValues,
	VkPipelineStageFlags waitDstStageMsk 
) {
	VkTimelineSemaphoreSubmitInfo timelineInfo = {
		.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.signalSemaphoreValueCount = std::size( signalValues ),
		.pSignalSemaphoreValues = std::data( signalValues )
	};

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = &timelineInfo,
		.waitSemaphoreCount = ( u32 ) std::size( waitSemas ),
		.pWaitSemaphores = std::data( waitSemas ),
		.pWaitDstStageMask = &waitDstStageMsk,
		.commandBufferCount = ( u32 ) std::size( cmdBuffers ),
		.pCommandBuffers = std::data( cmdBuffers ),
		.signalSemaphoreCount = ( u32 ) std::size( signalSemas ),
		.pSignalSemaphores = std::data( signalSemas )
	};
	// NOTE: queue submit has implicit host sync for trivial stuff
	VK_CHECK( vkQueueSubmit( this->hndl, 1, &submitInfo, 0 ) );
}

template<vk_queue_type QUEUE_TYPE>
void vk_queue<QUEUE_TYPE>::Present( VkSemaphore renderingCompleteSema, const vk_swapchain& swapchain, u32 swapchainImgIdx )
	requires ( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
{
	VkPresentInfoKHR presentInfo = { 
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderingCompleteSema,
		.swapchainCount = 1,
		.pSwapchains = &swapchain,
		.pImageIndices = &swapchainImgIdx
	};
	VK_CHECK( vkQueuePresentKHR( this->hndl, &presentInfo ) );
}

// TODO: use handle_map/free list and stable addressing if unique/new is cumbersome
struct vk_device
{
	vk_descriptor_manager descriptorManager;

	vk_swapchain swapchain;

	vk_queue<vk_queue_type::GRAPHICS> graphicsQueue;
	vk_queue<vk_queue_type::COMPUTE> computeQueue;
	vk_queue<vk_queue_type::TRANSFER> transferQueue;
	
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

	// TODO: sep initial validation form sc creation WHEN RESIZE
	// TODO: tweak settings/config
	vk_swapchain CreateSwapchain( VkSurfaceKHR vkSurf, VkFormat	scDesiredFormat, vk_queue_type presentQueueType );
	u32 AcquireNextSwapcahinImage( VkSemaphore acquireScImgSema, u64 timeout );

	// TODO: do we need a different value for timeline semas ?
	// TODO: separate the TimelineSemas from normal semas ?
	VkSemaphore CreateVkSemaphore( bool isTimeline );
	// TODO: pass {sema, value} pairs ?
	void WaitSemaphores( std::initializer_list<VkSemaphore> semas, std::initializer_list<u64> values, u64 maxWait );

	// TODO: AddrMode ?
	VkSampler CreateSampler(
		VkSamplerReductionMode reductionMode, VkFilter filter, VkSamplerAddressMode addressMode, VkSamplerMipmapMode mipmapMode );
	
	VkPipeline CreateVertexInputInterfacePipelineStage( VkPrimitiveTopology primitiveTopology, const char* name );
	VkPipeline CreateVertexShaderPipelineStage(
		std::span<VkDynamicState> dynamicState, 
		const vk_rasterization_config& rasterCfg, 
		const vk_shader_metadata& shader,
		const char* name );
	// TODO: depth clamp ?
	VkPipeline CreateFragmentShaderPipelineStage(
		vk_depth_stencil_config depthStencilCfg,
		const vk_shader_metadata& shader,
		const char* name );
	VkPipeline CreateFragmentOutputInterfacePipelineStage(
		std::span<VkFormat> colorAttachmentFormats,
		VkFormat depthAttachmentFormat,
		const vk_color_blending_config& colorBlendingCfg,
		bool colorBlendingEnable,
		const char* name );
	// TODO: specialization consts ?
	VkPipeline CreateGraphicsPipeline( std::span<VkPipeline> libaries, const char* name );
	VkPipeline CreateComputePipeline( 
		const vk_shader& shader, const std::span<vk_specialization_type> consts, const char* name );

	// TODO: allocate more ?
	// TODO: allocate VkCmdBuffer and just pass around references ?
	template<vk_queue_type QUEUE_TYPE>
	vk_command_buffer<QUEUE_TYPE> AllocateCommandBuffers( const vk_queue<QUEUE_TYPE>& queue )
	{
		VkCommandBufferAllocateInfo cmdBuffAllocInfo = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = queue.cmdPool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};
		VkCommandBuffer cmdBuff;
		VK_CHECK( vkAllocateCommandBuffers( this->device, &cmdBuffAllocInfo, &cmdBuff ) );

		return { cmdBuff };
	}
};

vk_device VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf );

