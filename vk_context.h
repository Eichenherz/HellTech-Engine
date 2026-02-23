#pragma once 

#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <Volk/volk.h>

#include <vk_mem_alloc.h>

#include "core_types.h"

#include "ht_error.h"
#include "ht_mem_arena.h"
#include "vk_error.h"
#include "vk_types.h"
#include "vk_command_buffer.h"
#include "vk_resources.h"
#include "vk_pso.h"
#include "vk_utils.h"

#include <vector>
#include <type_traits>
#include <span>
#include <functional>
#include <memory>

#include <EASTL/fixed_vector.h>
#include <EASTL/bonus/fixed_ring_buffer.h>
#include <EASTL/bonus/ring_buffer.h>
#include <EASTL/bitvector.h>

struct vk_swapchain_image
{
	VkSemaphore		canPresentSema;
	vk_image        img;
	desc_hndl32     writeDescIdx;
};

struct vk_queue
{
	VkQueue	        hndl;
	VkSemaphore     timelineSema; // NOTE: timeline sema
	u64             submitionCount;
	u32				familyIdx;
	VkQueueFlags	familyFlags;
};

struct vk_virtual_frame
{
	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuff;
};

struct vk_desc_deletion
{
	u64				timelineCounterVal;
	desc_hndl32		hndl;
};

struct vk_resc_deletion
{
	union
	{
		vk_buffer buff;
		vk_image  img;
	};
	vk_resource_type type;
	u64 timelineCounterVal;

	inline vk_resc_deletion() = default;
	inline vk_resc_deletion( const vk_buffer& b, u64 counter ) 
		: buff{ b }, type{ vk_resource_type::BUFFER }, timelineCounterVal{ counter } {}
	inline vk_resc_deletion( const vk_image& i, u64 counter ) 
		: img{ i }, type{ vk_resource_type::IMAGE }, timelineCounterVal{ counter } {}
};


template<typename T, u64 N>
using ring_buff = eastl::fixed_ring_buffer<T, N>;

using vk_frame_vector = eastl::fixed_vector<vk_virtual_frame, vk_renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED, false>;

using eastl_bitvector = eastl::bitvector<EASTLAllocatorType, u64>;

struct vk_desc_binding
{
	// NOTE: eastl::ring_buffer is no sized without .resize or push_back !!!!
	eastl::ring_buffer<desc_hndl32> slots;
	eastl_bitvector inUseMasks;

	VkDescriptorType type;

	vk_desc_binding() = default;
	inline vk_desc_binding( VkDescriptorPoolSize bindingInfo ) : 
		slots{ bindingInfo.descriptorCount }, 
		inUseMasks{ bindingInfo.descriptorCount, 0 }, 
		type{ bindingInfo.type }
	{
		vk_desc_binding_t bindingType = VkDescTypeToBinding( type );
		for( u64 si = 0; si < bindingInfo.descriptorCount; ++si )
		{
			slots.push_back( { .slot = ( u16 ) si, .type = bindingType } );
		}
	}

	inline desc_hndl32 AllocSlot()
	{
		HT_ASSERT( std::size( slots ) != 0 );
		desc_hndl32 hDesc = slots.front();
		slots.pop_front();

		inUseMasks[ hDesc.slot ] = 1;
		return hDesc;
	}

	inline void FreeSlot( desc_hndl32 hDesc )
	{
		HT_ASSERT( hDesc.slot < std::size( inUseMasks ) );
		HT_ASSERT( hDesc.slot < slots.capacity() );
		HT_ASSERT( !inUseMasks[ hDesc.slot ] );

		slots.push_back( hDesc );
		inUseMasks[ hDesc.slot ] = 0;
	}
};

using PFN_VkShaderDestoryer = std::function<void( vk_shader* )>;
using unique_shader_ptr = std::unique_ptr<vk_shader, PFN_VkShaderDestoryer>;

struct vk_context
{
	static constexpr u64 NUM_DESC = vk_desc_binding_t::COUNT;
	ring_buff<vk_resc_deletion, 128>		resourceDeletionQueue{};
	ring_buff<vk_desc_deletion, 128>		descriptroDeletionQueue{};

	fixed_arena<2048>						scratchArena;

	vk_frame_vector							vrtFrames;

	std::vector<vk_swapchain_image>			scImgs;

	std::array<vk_desc_binding, NUM_DESC>   descBindingSlots;
	std::vector<vk_descriptor_write>        descPendingUpdates;

	vk_queue								gfxQueue;
	vk_queue								copyQueue;

	VmaAllocator							allocator;

	VkSwapchainKHR		                    swapchain;

	VkDescriptorPool						descPool;
	VkDescriptorSetLayout					descSetLayout;
	VkDescriptorSet							descSet;

	VkPhysicalDeviceProperties				gpuProps;
	VkPhysicalDevice						gpu;
	VkDevice								device;
	
	VkInstance								inst;
	VkDebugUtilsMessengerEXT				dbgMsg;
	VkSurfaceKHR							surf;

	VkPipelineLayout						globalPipelineLayout;

	u32										deviceMask;
	float									timestampPeriod;
	u32										waveSize;

	vk_swapchain_config                     scConfig;

	vk_buffer CreateBuffer( const buffer_info& buffInfo );
	vk_image CreateImage( const image_info& imgInfo );

	inline void EnqueueResourceFree( const vk_resc_deletion& rscDeletion )
	{
		resourceDeletionQueue.push_back( rscDeletion );
	}
	inline void EnqueueDescriptorFree( const vk_desc_deletion& rscDeletion )
	{
		descriptroDeletionQueue.push_back( rscDeletion );
	}
	// TODO: depth clamp ?
	// VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipeline CreateGfxPipeline(
		std::span<const vk_gfx_shader_stage>	shaderStages,
		std::span<const VkDynamicState>			dynamicStates,
		const VkFormat*							pColorAttachmentFormats,
		u32										colorAttachmentCount,
		VkFormat								depthAttachmentFormat,
		const vk_gfx_pso_config&				psoConfig,
		VkPipelineLayout						vkPipelineLayout = VK_NULL_HANDLE );
	VkPipeline CreateComptuePipeline( const vk_shader& shader, const char* pName );

	unique_shader_ptr CreateShaderFromSpirv( std::span<const u8> spvByteCode );
	inline void DestroyShaderModule( VkShaderModule module )
	{
		vkDestroyShaderModule( device, module, 0 );
	}

	inline VkSampler CreateSampler( const VkSamplerCreateInfo& samplerCreateInfo )
	{
		VkSampler sampler;
		VK_CHECK( vkCreateSampler( device, &samplerCreateInfo, 0, &sampler ) );
		return sampler;
	}

	inline VkSemaphore CreateBinarySemaphore()
	{
		VkSemaphoreCreateInfo semaInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkSemaphore sema;
		VK_CHECK( vkCreateSemaphore( device, &semaInfo, 0, &sema ) );

		return sema;
	}

	inline vk_command_buffer GetFrameCmdBuff( u64 frameInFlightIdx )
	{
		HT_ASSERT( frameInFlightIdx < std::size( vrtFrames ) );
		vk_virtual_frame& thisVrtFrame = vrtFrames[ frameInFlightIdx ];

		VK_CHECK( vkResetCommandPool( device, thisVrtFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

		return { thisVrtFrame.cmdBuff, globalPipelineLayout, descSet };
	}

	inline void HostTransitionImageLayout( const VkHostImageLayoutTransitionInfo* transitions, u32 transitionCount ) const
	{
		VK_CHECK( vkTransitionImageLayout( device, transitionCount, transitions ) );
	}
	inline void HostCopyMemoryToImage( const vk_image& dst, const void* pSrc )
	{
		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( dst.format );
		VkMemoryToImageCopy memToImgCopy = {
			.sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
			.pHostPointer = pSrc,
			.imageSubresource = {
				.aspectMask     = aspectFlags, 
				.mipLevel       = 0,
				.baseArrayLayer = 0,
				.layerCount     = 1
            },
			.imageExtent = dst.Extent3D(),
		};

		VkCopyMemoryToImageInfo copyMemInfo = {
			.sType          = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO,
			.dstImage       = dst.hndl,
			.dstImageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.regionCount    = 1,
			.pRegions       = &memToImgCopy
		};

		VK_CHECK( vkCopyMemoryToImage( device, &copyMemInfo ) );
	}

	desc_hndl32 AllocDescriptor( const vk_descriptor_info& rscDescInfo );
	inline void FreeDescriptor( desc_hndl32 hDesc )
	{
		descBindingSlots[ hDesc.type ].FreeSlot( hDesc );
	}
	inline void EnqueueDescriptorFree( desc_hndl32 handle, u64 frameIdx )
	{
		descriptroDeletionQueue.push_back( { frameIdx, handle } );
	}

	void FlushPendingDescriptorUpdates();

	void FlushDeletionQueues( u64 frameIdx );

	void CreateSwapchin();

	inline u32 AcquireNextSwapchainImageBlocking( VkSemaphore canGetImgSema ) const
	{
		u32 imgIdx;
		VK_CHECK( vkAcquireNextImageKHR( device, swapchain, UINT64_MAX, canGetImgSema, 0, &imgIdx ) );
		return imgIdx;
	}

	// NOTE: passing UINT64_MAX will block forever
	inline VkResult QueueTryWaitFor( const vk_queue& queue, u64 maxDiffAllowed, u64 waitTime )
	{
		u64 signalsCount = 0;
		VK_CHECK( vkGetSemaphoreCounterValue( device, queue.timelineSema, &signalsCount ) );

		if( queue.submitionCount - signalsCount >= maxDiffAllowed )
		{
			u64 targetCount = queue.submitionCount;
			VkSemaphoreWaitInfo waitInfo = { 
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.semaphoreCount = 1,
				.pSemaphores = &queue.timelineSema,
				.pValues = &targetCount,
			};

			return vkWaitSemaphores( device, &waitInfo, waitTime );
		}
		return VK_SUCCESS;
	}

	// NOTE: queue submit has implicit host sync for trivial stuff, 
	void QueueSubmit(
		vk_queue& queue,
		std::span<VkSemaphoreSubmitInfo> waits,
		std::span<VkSemaphoreSubmitInfo> signals,
		VkCommandBuffer cmdBuff
	);
	inline void QueuePresent( const vk_queue& queue, u32 imgIdx )
	{
		VkPresentInfoKHR presentInfo = { 
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &scImgs[ imgIdx ].canPresentSema,
			.swapchainCount = 1,
			.pSwapchains = &swapchain,
			.pImageIndices = &imgIdx
		};
		VK_CHECK( vkQueuePresentKHR( queue.hndl, &presentInfo ) );
	}
};

vk_context VkMakeContext( uintptr_t hInst, uintptr_t hWnd, const vk_renderer_config& cfg );

#endif // !__VK_DEVICE_H__
