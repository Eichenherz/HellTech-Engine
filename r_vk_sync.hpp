#pragma once

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include "sys_os_api.h"

#include <utility>

inline  VkMemoryBarrier2
VkMakeMemoryBarrier2(
	VkAccessFlags2KHR			srcAccess,
	VkPipelineStageFlags2KHR	srcStage,
	VkAccessFlags2KHR			dstAccess,
	VkPipelineStageFlags2KHR	dstStage
) {
	return {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = srcStage,
		.srcAccessMask = srcAccess,
		.dstStageMask = dstStage,
		.dstAccessMask = dstAccess,
	};
}

inline  VkBufferMemoryBarrier2
VkMakeBufferBarrier2(
	VkBuffer					hBuff,
	VkAccessFlags2KHR			srcAccess,
	VkPipelineStageFlags2KHR	srcStage,
	VkAccessFlags2KHR			dstAccess,
	VkPipelineStageFlags2KHR	dstStage,
	VkDeviceSize				buffOffset = 0,
	VkDeviceSize				buffSize = VK_WHOLE_SIZE,
	u32							srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED
) {
	return { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
		.srcStageMask = srcStage,
		.srcAccessMask = srcAccess,
		.dstStageMask = dstStage,
		.dstAccessMask = dstAccess,
		.srcQueueFamilyIndex = srcQueueFamIdx,
		.dstQueueFamilyIndex = dstQueueFamIdx,
		.buffer = hBuff,
		.offset = buffOffset,
		.size = buffSize,
	};
}

inline VkBufferMemoryBarrier2KHR VkReverseBufferBarrier2( const VkBufferMemoryBarrier2& b )
{
	VkBufferMemoryBarrier2 barrier = b;
	std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
	std::swap( barrier.srcStageMask, barrier.dstStageMask );

	return barrier;
}

inline  VkImageMemoryBarrier2
VkMakeImageBarrier2(
	VkImage						hImg,
	VkAccessFlags2KHR           srcAccessMask,
	VkPipelineStageFlags2KHR    srcStageMask,
	VkAccessFlags2KHR           dstAccessMask,
	VkPipelineStageFlags2KHR	dstStageMask,
	VkImageLayout               oldLayout,
	VkImageLayout               newLayout,
	VkImageAspectFlags			aspectMask,
	u32							baseMipLevel = 0,
	u32							mipCount = 0,
	u32							baseArrayLayer = 0,
	u32							layerCount = 0,
	u32							srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
) {
	return {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask = srcStageMask,
		.srcAccessMask = srcAccessMask,
		.dstStageMask = dstStageMask,
		.dstAccessMask = dstAccessMask,
		.oldLayout = oldLayout,
		.newLayout = newLayout,
		.srcQueueFamilyIndex = srcQueueFamilyIndex,
		.dstQueueFamilyIndex = dstQueueFamilyIndex,
		.image = hImg,
		.subresourceRange = {
			.aspectMask = aspectMask,
			.baseMipLevel = baseMipLevel,
			.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS,
			.baseArrayLayer = baseArrayLayer,
			.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS,
	    },
	};
}


inline void VkCmdPipelineFlushCacheAndPerformImgLayoutTransitionBarriers(
	VkCommandBuffer cmdBuff,
	const std::span<VkMemoryBarrier2> memBarriers,
	const std::span<VkImageMemoryBarrier2> imgBarriers
) {
	VkDependencyInfo dependency = { 
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = std::size( memBarriers ),
		.pMemoryBarriers = std::data( memBarriers ),
		.imageMemoryBarrierCount = std::size( imgBarriers ),
		.pImageMemoryBarriers = std::data( imgBarriers ),
	};
	vkCmdPipelineBarrier2( cmdBuff, &dependency );
}

inline void VkCmdPipelineFlushCacheBarriers( VkCommandBuffer cmdBuff, const std::span<VkMemoryBarrier2> memBarriers )
{
	VkDependencyInfo dependency = { 
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = std::size( memBarriers ),
		.pMemoryBarriers = std::data( memBarriers ),
	};
	vkCmdPipelineBarrier2( cmdBuff, &dependency );
}

inline void VkCmdPipelineImgLayoutTransitionBarriers( VkCommandBuffer cmdBuff, const std::span<VkImageMemoryBarrier2> imgBarriers)
{
	VkDependencyInfo dependency = { 
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = std::size( imgBarriers ),
		.pImageMemoryBarriers = std::data( imgBarriers ),
	};
	vkCmdPipelineBarrier2( cmdBuff, &dependency );
}

inline void VkDebugSyncBarrierEverything( VkCommandBuffer cmdBuff )
{
	VkMemoryBarrier2 everythingBarrier = { 
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
		.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
		.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
	};
	
	VkDependencyInfo dependency = { 
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = 1,
		.pMemoryBarriers = &everythingBarrier
	};
	vkCmdPipelineBarrier2( cmdBuff, &dependency );
}