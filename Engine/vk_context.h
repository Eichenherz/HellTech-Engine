#pragma once 

#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <Volk/volk.h>

#include <vk_mem_alloc.h>

#include <ht_core_types.h>
#include <ht_error.h>

#include <ht_ring_buffer.h>
#include <System/sys_sync.h>

#include "vk_error.h"
#include "vk_types.h"
#include "vk_resources.h"
#include "vk_utils.h"
#include "vk_command_buffer.h"

#include <array>
#include <vector>
#include <span>

struct vk_timeline
{
	VkSemaphore         sema;
	u64			        submitsIssuedCount;

	VkSemaphoreSubmitInfo GetWaitAtPoint( VkPipelineStageFlags2 stage ) const
	{
		return {
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = sema,
			.value     = submitsIssuedCount,
			.stageMask = stage,
		};
	}

	VkSemaphoreSubmitInfo GetSignalNextPoint( VkPipelineStageFlags2 stage )
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
	VkSemaphore		    canPresentSema;
	vk_image            img;
	desc_hndl32         writeDescIdx;
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
	fixed_ringbuff_w_lock<vk_cmd_pool_buff, 128>	free;
	fixed_ringbuff_w_lock<vk_cb_deletion, 128>	pending;
};

struct vk_desc_deletion
{
	u64				    timelineCounterVal;
	desc_hndl32		    hndl;
};

struct vk_resc_deletion
{
	u64					frameTimelineVal;
	union
	{
		vk_buffer		buff;
		vk_image		img;
	};
	vk_resource_type	type;

	vk_resc_deletion() = default;
	vk_resc_deletion( const vk_buffer& b, u64 counter ) : buff{ b }, type{ b.TYPE }, frameTimelineVal{ counter } {}
	vk_resc_deletion( const vk_image& i, u64 counter ) : img{ i }, type{ i.TYPE }, frameTimelineVal{ counter } {}
};

struct vk_desc_binding
{
    ringbuff_w_lock<desc_hndl32>	slots   = {};
    VkDescriptorType		        type    = {};

    vk_desc_binding() = default;

    vk_desc_binding( std::span<desc_hndl32> ringBuffMem, VkDescriptorType descType ) :
        slots{ ringBuffMem }, type{ descType }
    {
        vk_desc_binding_t bindingType = VkDescTypeToBinding( type );
        for( u64 si = 0; si < std::size( slots ); ++si )
        {
            slots.TryPush( desc_hndl32{ .slot = ( u16 ) si, .type = bindingType, .inUse = false } );
        }
    }

    desc_hndl32 AllocSlot()
    {
        desc_hndl32 hDesc = {};
        while( !slots.TryPop( hDesc ) );

        hDesc.inUse = true;
        return hDesc;
    }

    void FreeSlot( desc_hndl32 hDesc )
    {
        HT_ASSERT( hDesc.slot < std::size( slots ) );
        HT_ASSERT( !hDesc.inUse );

        hDesc.inUse = false;
        while( !slots.TryPush( hDesc ) );
    }
};

struct vk_context
{
	static constexpr u64 NUM_DESC = vk_desc_binding_t::COUNT;
	// NOTE: we only alloc PERSISTENT resources on other timelines;
	// only the main GPU timeline is allowed to alloc and free TRANSIENTS
	std::vector<vk_resc_deletion>			resourceDeletionQueue;
	std::vector<vk_desc_deletion>			descriptorDeletionQueue;

	inline_vector<vk_swapchain_image, 8>	scImgs;

	std::array<vk_desc_binding, NUM_DESC>   descBindingSlots;
	
	copyable_srwlock                        descUpdatesLock;
	std::vector<vk_descriptor_write>        descPendingUpdates;

	vk_cb_pool		                        cbPools[ ( u64 ) vk_queue_t::COUNT ];

	vk_queue								gfxQueue;
	vk_queue								copyQueue;

	vk_query_pool							timestampQueryPool;
	vk_query_pool							pplnStatsQueryPool;

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

	vk_buffer           CreateBuffer( const buffer_info& buffInfo );
	vk_image            CreateImage( const image_info& imgInfo );

	void                EnqueueResourceFree( const vk_resc_deletion& rscDeletion )
	{
	    resourceDeletionQueue.push_back( rscDeletion );
	}
	void                EnqueueDescriptorFree( const vk_desc_deletion& rscDeletion )
	{
	    descriptorDeletionQueue.push_back( rscDeletion );
	}

	vk_shader           CreateShaderFromSpirv( std::span<const u8> spvByteCode );
	void                DestroyShaderModule( VkShaderModule module )
	{
	    vkDestroyShaderModule( device, module, nullptr );
	}

	// TODO: depth clamp ?
	VkPipeline          CreateGfxPipeline(
		std::span<const vk_gfx_shader_stage>	shaderStages,
		std::span<const VkDynamicState>			dynamicStates,
		std::span<const VkFormat>				colorAttachmentFormats,
		VkFormat								depthAttachmentFormat,
		const vk_gfx_pso_config&				psoConfig,
		VkPipelineLayout						vkPipelineLayout = VK_NULL_HANDLE );

	vk_compute_pipeline CreateComputePipeline( const vk_shader& shader );

	VkSampler           CreateSampler( const VkSamplerCreateInfo& samplerCreateInfo );

	VkSemaphore         CreateBinarySemaphore();

	// NOTE: passing UINT64_MAX will block forever
	VkResult            TimelineTryWaitFor( const vk_timeline& timeline, u64 maxDiffAllowed, u64 waitTime );

	VkFence             AllocFence();
	bool                FenceWaitAndResetOnDone( VkFence vkFence, u64 timeoutNanosecs );

	desc_hndl32         AllocDescriptorIdx( const vk_descriptor_info& rscDescInfo );
	void                EnqueueDescriptorIdxFree( desc_hndl32 handle, u64 frameIdx )
	{
		descriptorDeletionQueue.push_back( { frameIdx, handle } );
	}

	void                FlushPendingDescriptorUpdates();
	void                FlushDeletionQueues( u64 frameIdx );

	void                CreateSwapchain();
	u32                 AcquireNextSwapchainImageBlocking( VkSemaphore canGetImgSema ) const;

	vk_command_buffer   AllocateCmdPoolAndBuff( vk_queue_t queueType );

	// NOTE: queue submit has implicit host sync for trivial stuff, 
	void                QueueSubmit(
		const vk_queue&                  queue,
		const vk_command_buffer&         cb,
		std::span<VkSemaphoreSubmitInfo> waits   = {},
		std::span<VkSemaphoreSubmitInfo> signals = {},
		VkFence                          vkFence = VK_NULL_HANDLE
	);
	void                QueuePresent( const vk_queue& queue, u32 imgIdx );
};

inline VkSampler vk_context::CreateSampler( const VkSamplerCreateInfo& samplerCreateInfo )
{
    VkSampler sampler;
    VK_CHECK( vkCreateSampler( device, &samplerCreateInfo, 0, &sampler ) );
    return sampler;
}
inline VkResult vk_context::TimelineTryWaitFor( const vk_timeline& timeline, u64 maxDiffAllowed, u64 waitTime )
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
inline VkFence vk_context::AllocFence()
{
    if( std::size( copyFencesPool ) != 0 )
    {
        VkFence fence = *std::rbegin( copyFencesPool );
        copyFencesPool.pop_back();
        return fence;
    }

    VkFenceCreateInfo ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };

    VkFence fence;
    VK_CHECK( vkCreateFence( device, &ci, nullptr, &fence ) );

    return fence;
}
inline bool vk_context::FenceWaitAndResetOnDone( VkFence vkFence, u64 timeoutNanosecs )
{
    VkResult vkRes = vkWaitForFences( device, 1, &vkFence, VK_TRUE, timeoutNanosecs );
    if( VK_TIMEOUT == vkRes ) return false;

    HT_ASSERT( vkRes < VK_TIMEOUT );
    vkResetFences( device, 1, &vkFence );

    copyFencesPool.push_back( vkFence );
    return true;
}
inline u32 vk_context::AcquireNextSwapchainImageBlocking( VkSemaphore canGetImgSema ) const
{
    u32 imgIdx;
    VK_CHECK( vkAcquireNextImageKHR( device, swapchain, UINT64_MAX, canGetImgSema, 0, &imgIdx ) );
    return imgIdx;
}
inline void vk_context::QueuePresent( const vk_queue& queue, u32 imgIdx )
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

vk_context VkMakeContext( uintptr_t hInst, uintptr_t hWnd, const vk_renderer_config& cfg );

#endif // !__VK_DEVICE_H__
