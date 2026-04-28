#pragma once 

#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <Volk/volk.h>

#include <vk_mem_alloc.h>

#include "ht_core_types.h"

#include "ht_error.h"
#include "vk_error.h"
#include "vk_types.h"
#include "vk_resources.h"
#include "vk_pso.h"
#include "vk_utils.h"

#include <array>
#include <vector>
#include <span>
#include <functional>
#include <memory>

#include "System/sys_sync.h"
#include "ht_mem_arena.h"

#include "ht_mtx_queue.h"

struct vk_timeline
{
	VkSemaphore sema;
	u64			submitsIssuedCount;

	inline VkSemaphoreSubmitInfo GetWaitAtPoint( VkPipelineStageFlags2 stage ) const
	{
		return {
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = sema,
			.value     = submitsIssuedCount,
			.stageMask = stage,
		};
	}

	inline VkSemaphoreSubmitInfo GetSignalNextPoint( VkPipelineStageFlags2 stage )
	{
		submitsIssuedCount++;
		return {
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = sema,
			.value     = submitsIssuedCount,
			.stageMask = stage,
		};
	}
};

struct vk_swapchain_image
{
	VkSemaphore		canPresentSema;
	vk_image        img;
	desc_hndl32     writeDescIdx;
};

enum class vk_queue_t : u32
{
	GFX = 0,
	COPY,
	//COMP,
	COUNT
};

struct vk_queue
{
	copyable_srwlock    lock;
	VkQueue				hndl;
	VkSemaphore			timelineSema;
	mutable u64			submitionCount;
	u32					familyIdx;
};

struct vk_cmd_pool_buff
{
	VkCommandPool		pool;
	VkCommandBuffer		buff;
	vk_queue_t			parentQueueFamType;
}; 

struct vk_cb_deletion
{
	VkSemaphore			sema;
	u64					waitVal;
	vk_cmd_pool_buff	hndl;
};

struct vk_cb_pool
{
	fixed_mtx_queue<vk_cmd_pool_buff, 128>	free;
	fixed_mtx_queue<vk_cb_deletion, 128>	pending;
};

struct vk_desc_deletion
{
	u64				timelineCounterVal;
	desc_hndl32		hndl;
};

struct vk_resc_deletion
{
	//VkSemaphore			timelineSema = VK_NULL_HANDLE;
	//u64					waitSignal = -1;
	u64						frameTimelineVal;
	union
	{
		vk_buffer			buff;
		vk_image			img;
	};
	vk_resource_type		type;

	inline vk_resc_deletion() = default;
	inline vk_resc_deletion( const vk_buffer& b, u64 counter ) 
		: buff{ b }, type{ vk_resource_type::BUFFER }, frameTimelineVal{ counter } {}
	inline vk_resc_deletion( const vk_image& i, u64 counter ) 
		: img{ i }, type{ vk_resource_type::IMAGE }, frameTimelineVal{ counter } {}
};

struct vk_desc_binding
{
	mtx_queue<desc_hndl32>	slots;

	VkDescriptorType		type;

	vk_desc_binding() = default;

	inline vk_desc_binding( VkDescriptorPoolSize bindingInfo ) : 
		slots{ bindingInfo.descriptorCount }, type{ bindingInfo.type }
	{
		vk_desc_binding_t bindingType = VkDescTypeToBinding( type );
		for( u64 si = 0; si < bindingInfo.descriptorCount; ++si )
		{
			slots.TryPush( desc_hndl32{ .slot = ( u16 ) si, .type = bindingType, .inUse = false } );
		}
	}

	inline desc_hndl32 AllocSlot()
	{
		HT_ASSERT( 0 != slots.capacity() );
		desc_hndl32 hDesc = {};
		while( !slots.TryPop( hDesc ) );

		hDesc.inUse = true;
		return hDesc;
	}

	inline void FreeSlot( desc_hndl32 hDesc )
	{
		HT_ASSERT( hDesc.slot < slots.capacity() );
		HT_ASSERT( !hDesc.inUse );

		hDesc.inUse = false;
		while( !slots.TryPush( hDesc ) );
	}
};

using PFN_VkShaderDestroyer = std::function<void( vk_shader* )>;
using unique_shader_ptr = std::unique_ptr<vk_shader, PFN_VkShaderDestroyer>;

struct vk_context
{
	static constexpr u64 NUM_DESC = vk_desc_binding_t::COUNT;

	// NOTE: we only alloc PERSISTENT resouces on other timelines; 
	// only the main GPU timeline is to alloc and free TRANSIENTS
	std::vector<vk_resc_deletion>			resourceDeletionQueue;
	std::vector<vk_desc_deletion>			descriptorDeletionQueue;

	static_arena<2048>						scratchArena;

	std::vector<vk_swapchain_image>			scImgs;

	std::array<vk_desc_binding, NUM_DESC>   descBindingSlots;
	
	copyable_srwlock                        descUpdatesLock;
	std::vector<vk_descriptor_write>        descPendingUpdates;

	vk_cb_pool		                        cbPools[ ( u64 ) vk_queue_t::COUNT ];

	vk_queue								gfxQueue;
	vk_queue								copyQueue;

	vk_timeline							    gpuFrameTimeline;

	VmaAllocator							allocator;

	VkSwapchainKHR		                    swapchain;

	// TODO: sync when doing parallel uploads
	std::vector<VkFence>                    copyFencesPool;

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
		descriptorDeletionQueue.push_back( rscDeletion );
	}

	unique_shader_ptr CreateShaderFromSpirv( std::span<const u8> spvByteCode );
	inline void DestroyShaderModule( VkShaderModule module )
	{
		vkDestroyShaderModule( device, module, 0 );
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

	VkPipeline CreateComputePipeline( const vk_shader& shader );

	inline VkSampler CreateSampler( const VkSamplerCreateInfo& samplerCreateInfo )
	{
		VkSampler sampler;
		VK_CHECK( vkCreateSampler( device, &samplerCreateInfo, 0, &sampler ) );
		return sampler;
	}

	VkSemaphore CreateBinarySemaphore();

	// NOTE: passing UINT64_MAX will block forever
	inline VkResult TimelineTryWaitFor( const vk_timeline& timeline, u64 maxDiffAllowed, u64 waitTime )
	{
		u64 submissionsCompleted = 0;
		VK_CHECK( vkGetSemaphoreCounterValue( device, timeline.sema, &submissionsCompleted ) );

		if( timeline.submitsIssuedCount >= maxDiffAllowed + submissionsCompleted )
		{
			u64 targetCount = timeline.submitsIssuedCount;
			VkSemaphoreWaitInfo waitInfo = { 
				.sType			= VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.semaphoreCount = 1,
				.pSemaphores	= &timeline.sema,
				.pValues		= &targetCount,
			};

			return vkWaitSemaphores( device, &waitInfo, waitTime );
		}
		return VK_SUCCESS;
	}

	inline VkFence AllocFence()
	{
		if( std::size( copyFencesPool ) != 0 )
		{
			VkFence fence = *std::rbegin( copyFencesPool );
			copyFencesPool.pop_back();
			return fence;
		}

		VkFenceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

		VkFence fence;
		vkCreateFence( device, &ci, NULL, &fence );

		return fence;
	}

	inline bool FenceWaitAndResetOnDone( VkFence vkFence, u64 timeoutNanosecs )
	{
		VkResult vkRes = vkWaitForFences( device, 1, &vkFence, VK_TRUE, timeoutNanosecs );
		if( VK_TIMEOUT == vkRes ) return false;

		HT_ASSERT( vkRes < VK_TIMEOUT );
		vkResetFences( device, 1, &vkFence );

		copyFencesPool.push_back( vkFence );
		return true;
	}

	inline void HostTransitionImageLayout( const VkHostImageLayoutTransitionInfo* transitions, u32 transitionCount ) const
	{
		VK_CHECK( vkTransitionImageLayout( device, transitionCount, transitions ) );
	}
	inline void HostCopyMemoryToImage( const vk_image& dst, const void* pSrc )
	{
		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( dst.format );
		VkMemoryToImageCopy memToImgCopy = {
			.sType				= VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
			.pHostPointer		= pSrc,
			.imageSubresource	= {
				.aspectMask     = aspectFlags, 
				.mipLevel       = 0,
				.baseArrayLayer = 0,
				.layerCount     = 1
            },
			.imageExtent		= dst.Extent3D(),
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

	desc_hndl32 AllocDescriptorIdx( const vk_descriptor_info& rscDescInfo );
	inline void EnqueueDescriptorIdxFree( desc_hndl32 handle, u64 frameIdx )
	{
		descriptorDeletionQueue.push_back( { frameIdx, handle } );
	}

	void FlushPendingDescriptorUpdates();

	void FlushDeletionQueues( u64 frameIdx );

	void CreateSwapchain();

	inline u32 AcquireNextSwapchainImageBlocking( VkSemaphore canGetImgSema ) const
	{
		u32 imgIdx;
		VK_CHECK( vkAcquireNextImageKHR( device, swapchain, UINT64_MAX, canGetImgSema, 0, &imgIdx ) );
		return imgIdx;
	}

	vk_cmd_pool_buff AllocateCmdPoolAndBuff( vk_queue_t queueType );

	// NOTE: queue submit has implicit host sync for trivial stuff, 
	void QueueSubmit(
		const vk_queue&                  queue,
		const vk_cmd_pool_buff&          cb,
		std::span<VkSemaphoreSubmitInfo> waits   = {},
		std::span<VkSemaphoreSubmitInfo> signals = {},
		VkFence                          vkFence = VK_NULL_HANDLE
	);

	inline void QueuePresent( const vk_queue& queue, u32 imgIdx )
	{
		VkPresentInfoKHR presentInfo = { 
			.sType				= VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores	= &scImgs[ imgIdx ].canPresentSema,
			.swapchainCount		= 1,
			.pSwapchains		= &swapchain,
			.pImageIndices		= &imgIdx
		};
		VK_CHECK( vkQueuePresentKHR( queue.hndl, &presentInfo ) );
	}
};

vk_context VkMakeContext( uintptr_t hInst, uintptr_t hWnd, const vk_renderer_config& cfg );

#endif // !__VK_DEVICE_H__
