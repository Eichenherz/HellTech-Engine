#pragma once

#include "vk_common.hpp"
#include "vk_procs.h"

#include <utility>

inline  VkMemoryBarrier2
VkMakeMemoryBarrier2(
	VkAccessFlags2			srcAccess,
	VkPipelineStageFlags2	srcStage,
	VkAccessFlags2			dstAccess,
	VkPipelineStageFlags2	dstStage
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
	VkAccessFlags2			srcAccess,
	VkPipelineStageFlags2	srcStage,
	VkAccessFlags2			dstAccess,
	VkPipelineStageFlags2	dstStage,
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
		.offset = 0,
		.size = VK_WHOLE_SIZE,
	};
}

inline VkBufferMemoryBarrier2 VkReverseBufferBarrier2( const VkBufferMemoryBarrier2& b )
{
	VkBufferMemoryBarrier2 barrier = b;
	std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
	std::swap( barrier.srcStageMask, barrier.dstStageMask );

	return barrier;
}

inline  VkImageMemoryBarrier2
VkMakeImageBarrier2(
	VkImage						hImg,
	VkAccessFlags2           srcAccessMask,
	VkPipelineStageFlags2    srcStageMask,
	VkAccessFlags2           dstAccessMask,
	VkPipelineStageFlags2	dstStageMask,
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
		.memoryBarrierCount = ( u32 ) std::size( memBarriers ),
		.pMemoryBarriers = std::data( memBarriers ),
		.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
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