#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#include <Volk/volk.h>

#include <3rdParty/vk_mem_alloc.h>

#include "vk_error.h"
#include "vk_resources.h"
#include "vk_descriptor.h"
#include "vk_swapchain.h"
#include "vk_pso.h"
#include "core_types.h"
#include "sys_os_api.h"

#include <type_traits>
#include <functional>

// TODO:
struct renderer_config
{
	static constexpr u8 MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
	static constexpr u8 MAX_SWAPCHAIN_IMG_ALLOWED = 3;

	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat		desiredHiZFormat = VK_FORMAT_R32_SFLOAT;
	VkFormat        desiredSwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	u16             renderWidth;
	u16             rednerHeight;
	u8              framesInFlightCount = 2;
	u8              swapchainImageCount = 3;
};
static renderer_config renderCfg = {};

struct vk_queue
{
	VkQueue	hndl;
	u32	index;
	VkQueueFlags familyFlags;
	bool canPresent;

	// NOTE: queue submit has implicit host sync for trivial stuff, 
	// so in theroy we shouldn't worry about memcpy to HOST_VISIBLE | DEVICE_LOCAL
	inline void QueueSubmit( 
		std::span<VkSemaphoreSubmitInfo> waits,
		std::span<VkSemaphoreSubmitInfo> signals,
		VkCommandBuffer cmdBuff
	) {
		VkCommandBufferSubmitInfo cmdInfo = {
			.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = cmdBuff,
		};

		VkSubmitInfo2 submitInfo = { 
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.waitSemaphoreInfoCount = (u32) std::size( waits ),
			.pWaitSemaphoreInfos = std::data( waits ),
			.commandBufferInfoCount = 1,
			.pCommandBufferInfos = &cmdInfo,
			.signalSemaphoreInfoCount = (u32) std::size( signals ),
			.pSignalSemaphoreInfos = std::data( signals ),
		};
		VK_CHECK( vkQueueSubmit2( hndl, 1, &submitInfo, VK_NULL_HANDLE ) );
	}
};

struct vk_timeline
{
	VkSemaphore     sema; // NOTE: timeline sema
	u64             submitionCount;
};


using PFN_VkShaderDestoryer = std::function<void( vk_shader* )>;
using unique_shader_ptr = std::unique_ptr<vk_shader, PFN_VkShaderDestoryer>;

struct vk_device_ctx
{
	vk_swapchain    sc;
	vk_desc_state   descState;
	vk_queue		gfxQueue;

	VmaAllocator    allocator;

	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;
	
	VkPipelineLayout globalPipelineLayout;

	u32             deviceMask;
	float           timestampPeriod;
	u8				waveSize;

	vk_buffer CreateBuffer( const buffer_info& buffInfo );
	vk_image CreateImage( const image_info& imgInfo );
	// TODO: depth clamp ?
	// VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipeline CreateGfxPipeline(
		std::span<const vk_gfx_shader_stage>	shaderStages,
		std::span<const VkDynamicState>			dynamicStates,
		const VkFormat*							pColorAttachmentFormats,
		u32										colorAttachmentCount,
		VkFormat								depthAttachmentFormat,
		const vk_gfx_pipeline_state&			pipelineState );
	VkPipeline CreateComptuePipeline( const vk_shader& shader, vk_specializations consts, const char* pName = "" );
	inline vk_timeline CreateGpuTimeline( u64 intialValue )
	{
		VkSemaphoreTypeCreateInfo timelineInfo = { 
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
			.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
			.initialValue = intialValue,
		};
		VkSemaphoreCreateInfo timelineSemaInfo = { 
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, 
			.pNext = &timelineInfo 
		};

		VkSemaphore timelineSema;
		VK_CHECK( vkCreateSemaphore( device, &timelineSemaInfo, 0, &timelineSema ) );

		return { .sema = timelineSema, .submitionCount = 0 };
	}

	unique_shader_ptr CreateShaderFromSpirv( std::span<const u8> spvByteCode );
	inline void DestroyShaderModule( VkShaderModule module )
	{
		vkDestroyShaderModule( device, module, 0 );
	}

	// NOTE: passing UINT64_MAX will block forever
	inline VkResult TryWaitOnTimelineFor( const vk_timeline& timeline, u64 maxDiffAllowed, u64 waitTime )
	{
		u64 signalsCount = 0;
		VK_CHECK( vkGetSemaphoreCounterValue( device, timeline.sema, &signalsCount ) );

		if( timeline.submitionCount - signalsCount >= maxDiffAllowed )
		{
			u64 targetCount = timeline.submitionCount;
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
	inline void ResetCmdPool( VkCommandPool cmdPool )
	{
		VK_CHECK( vkResetCommandPool( device, cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );
	}
	inline void TransitionImageLayout( const VkHostImageLayoutTransitionInfo* transitions, u32 transitionCount ) const
	{
		VK_CHECK( vkTransitionImageLayout( device, transitionCount, transitions ) );
	}
	inline void CopyMemoryToImage( const vk_image& dst, const void* pSrc )
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
	inline u32 AcquireNextSwapchainImageBlocking( VkSemaphore virtualFrameSema ) const
	{
		u32 imgIdx;
		VK_CHECK( vkAcquireNextImageKHR( device, sc.swapchain, UINT64_MAX, virtualFrameSema, 0, &imgIdx ) );
		return imgIdx;
	}
	inline void QueuePresent( const vk_queue& queue, u32 imgIdx )
	{
		VkPresentInfoKHR presentInfo = { 
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &sc.imgs[ imgIdx ].canPresentSema,
			.swapchainCount = 1,
			.pSwapchains = &sc.swapchain,
			.pImageIndices = &imgIdx
		};
		VK_CHECK( vkQueuePresentKHR( queue.hndl, &presentInfo ) );
	}
};

vk_device_ctx VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf, const renderer_config& cfg );

#endif // !__VK_DEVICE_H__
