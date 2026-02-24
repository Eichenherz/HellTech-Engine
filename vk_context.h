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
#include "vk_resources.h"
#include "vk_pso.h"
#include "vk_utils.h"

#include <array>
#include <vector>
#include <span>
#include <functional>
#include <memory>

#include <EASTL/bonus/fixed_ring_buffer.h>
#include <EASTL/bonus/ring_buffer.h>
#include <EASTL/bitvector.h>

struct vk_timeline
{
	VkSemaphore sema;
	u64 submitsIssuedCount;
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
	COMP,
	COUNT
};

struct vk_queue
{
	VkQueue									hndl;
	VkSemaphore								timelineSema;
	u64										submitionCount;
	u32										familyIdx;
};

struct vk_cmd_pool_buff
{
	VkCommandPool		pool;
	VkCommandBuffer		buff;
	VkSemaphore			timelineSema;
	u64				    submissionIdx;
	vk_queue_t			parentQueueFamType;
};

struct vk_cb_hndl32
{
	u32 idx : 16;
	u32 type : 2;
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
using fixed_ring_buff = eastl::fixed_ring_buffer<T, N>;

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

	fixed_ring_buff<vk_resc_deletion, 128>	resourceDeletionQueue{};
	fixed_ring_buff<vk_desc_deletion, 128>	descriptroDeletionQueue{};

	fixed_arena<2048>						scratchArena;

	std::vector<vk_swapchain_image>			scImgs;

	std::array<vk_desc_binding, NUM_DESC>   descBindingSlots;
	std::vector<vk_descriptor_write>        descPendingUpdates;

	std::vector<vk_cmd_pool_buff>           gfxCmdPoolAndBuffs;
	std::vector<vk_cmd_pool_buff>           computeCmdPoolAndBuffs;
	std::vector<vk_cmd_pool_buff>           copyCmdPoolAndBuffs;

	vk_queue								gfxQueue;
	vk_queue								copyQueue;

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
	VkPipeline CreateComptuePipeline( const vk_shader& shader, const char* pName );

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
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				.semaphoreCount = 1,
				.pSemaphores = &timeline.sema,
				.pValues = &targetCount,
			};

			return vkWaitSemaphores( device, &waitInfo, waitTime );
		}
		return VK_SUCCESS;
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

	vk_cb_hndl32 AllocateCmdPoolAndBuff( vk_queue_t queueType );
	inline VkCommandBuffer GetCmdBuff( vk_cb_hndl32 hndl )
	{
		vk_queue_t queueType = ( vk_queue_t ) hndl.type;
		using enum vk_queue_t;
		switch( queueType )
		{
			case GFX: return gfxCmdPoolAndBuffs[ hndl.idx ].buff;
			case COPY: return copyCmdPoolAndBuffs[ hndl.idx ].buff;
			default: HT_ASSERT( false ); return VK_NULL_HANDLE;
		}
	}
	void DeferredRecycleCmdPoolAndBuff( vk_cb_hndl32 hndl, const vk_timeline& timeline );

	// NOTE: queue submit has implicit host sync for trivial stuff, 
	void QueueSubmitToTimeline(
		const vk_queue& queue,
		const vk_timeline& timeline,
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
