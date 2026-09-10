#pragma once 

#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <Volk/volk.h>

#include <vk_mem_alloc.h>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_utils.h>
#include <ht_ring_buffer.h>
#include <System/sys_sync.h>

#include "vk_error.h"
#include "vk_types.h"
#include "vk_resources.h"
#include "vk_command_buffer.h"

#include <array>
#include <span>

struct vk_timeline
{
	VkSemaphore         sema                = nullptr;
	u64			        submitsIssuedCount  = 0;

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
	VkSemaphore		    canPresentSema  = nullptr;
	vk_image            img             = {};
	desc_hndl32         writeDescIdx    = std::bit_cast<desc_hndl32>( ~0u );
};

constexpr u64 MAX_CBS_PER_QUEUE = 64;

struct vk_queue
{
    using fixed_queue = fixed_ringbuff_w_lock<vk_cmd_pool, MAX_CBS_PER_QUEUE>;

	copyable_srwlock    submitLock   = {}; // NOTE: as mandated by the vulkan spec
	VkQueue				hndl         = nullptr;
	VkSemaphore			timelineSema = nullptr;
	u64			        submitCount  = 0;
    fixed_queue         freeCbs      = {};
    fixed_queue         pendingCbs   = {};
    vk_queue_t          queueType    = vk_queue_t::COUNT;
	u32					familyIdx    = ~0u;
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
    // NOTE: this divides even just bc of our numbers
    static constexpr u64 SLOT_COUNT = vk_renderer_config::MAX_DESCRIPTOR_COUNT_PER_TYPE / 64;
    alignas( 64 ) atomic_u64    slotsBmp[ SLOT_COUNT ]  = {};
    VkDescriptorType            type                    = {};

    vk_desc_binding() = default;

    vk_desc_binding( VkDescriptorType descType ) : type{ descType } {}

    desc_hndl32 AllocSlot()
    {
        for( u64 qwi = 0; qwi < std::size( slotsBmp ); ++qwi ) // TODO: maybe do the whole atomic ceremony ?
        {
            u64 qword = slotsBmp[ qwi ];
            for( ;; )
            {
                u64 firstFreeMask = FirstUnsetMask64( qword ) ;
                if( !firstFreeMask ) break;

                u64 oldBinState = SysAtomicOr64<sys_fence_t::SEQ_CST>( &slotsBmp[ qwi ], firstFreeMask );
                if( !( firstFreeMask & oldBinState ) )
                {
                    return {
                        .slot   = u32( qwi * 64 + std::countr_zero( firstFreeMask ) ),
                        //.type   = ,
                        .inUse  = true
                    };
                }
                qword = oldBinState | firstFreeMask;
            }
        }
        return std::bit_cast<desc_hndl32>( ~0u );
    }

    void FreeSlot( desc_hndl32 hDesc )
    {
        HT_ASSERT( hDesc.slot < std::size( slotsBmp ) * 64 );
        HT_ASSERT( hDesc.inUse );

        u64 binIdx = hDesc.slot >> 6;
        u64 bitIdx = hDesc.slot & 63;

        SysAtomicAnd64<sys_fence_t::REL>( &slotsBmp[ binIdx ], ~( 1ull << bitIdx ) );
    }
};

// TODO: make sure the gpu atomics are aligned !
struct vk_context
{
	static constexpr u64 NUM_DESC = vk_desc_binding_t::COUNT;
	// NOTE: we only alloc PERSISTENT resources on other timelines;
	// only the main GPU timeline is allowed to alloc and free TRANSIENTS
	borrowed_array<vk_resc_deletion>		resourceDeletionQueue;
	borrowed_array<vk_desc_deletion>		descDeletionQueue;

	inline_array<vk_swapchain_image, 6>	    scImgs;

	std::array<vk_desc_binding, NUM_DESC>   descBindingSlots;
	
	copyable_srwlock                        descUpdatesLock;
	borrowed_array<vk_descriptor_write>    descPendingUpdates;

	vk_queue								gfxQueue;
	vk_queue								copyQueue;

	vk_query_pool							timestampQueryPool;
	vk_query_pool							pplnStatsQueryPool;

	vk_timeline							    gpuFrameTimeline;

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

	vk_buffer           CreateBuffer( const buffer_info& buffInfo );
	vk_image            CreateImage( const image_info& imgInfo );

	void                EnqueueResourceFree( const vk_resc_deletion& rscDeletion )
	{
	    resourceDeletionQueue.push_back( rscDeletion );
	}
	void                EnqueueDescriptorFree( const vk_desc_deletion& rscDeletion )
	{
	    descDeletionQueue.push_back( rscDeletion );
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

	desc_hndl32         AllocDescriptorIdx( const vk_descriptor_info& rscDescInfo );
	void                EnqueueDescriptorIdxFree( desc_hndl32 handle, u64 frameIdx )
	{
		descDeletionQueue.push_back( { frameIdx, handle } );
	}

	void                FlushPendingDescriptorUpdates();
	void                FlushDeletionQueues( u64 frameIdx );

	void                CreateSwapchain();
	u32                 AcquireNextSwapchainImageBlocking( VkSemaphore canGetImgSema ) const;

	vk_command_buffer   AllocateCmdBufferForQueue( vk_queue_t queueType );

	// NOTE: queue submit has implicit host sync for trivial stuff, 
	u64                 QueueSubmit(
		vk_queue&                           queue,
		const vk_command_buffer&            cb,
		std::span<VkSemaphoreSubmitInfo>    waits   = {},
		std::span<VkSemaphoreSubmitInfo>    signals = {},
		VkFence                             vkFence = VK_NULL_HANDLE
	);
	void                QueuePresent( const vk_queue& queue, u32 imgIdx );
};

inline VkSampler vk_context::CreateSampler( const VkSamplerCreateInfo& samplerCreateInfo )
{
    VkSampler sampler;
    VK_CHECK( vkCreateSampler( device, &samplerCreateInfo, nullptr, &sampler ) );
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
