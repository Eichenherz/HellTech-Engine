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
	VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR };
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.srcQueueFamilyIndex = srcQueueFamIdx;
	barrier.dstQueueFamilyIndex = dstQueueFamIdx;
	barrier.buffer = hBuff;
	barrier.offset = buffOffset;
	barrier.size = buffSize;

	return barrier;
}

inline  VkBufferMemoryBarrier2KHR VkReverseBufferBarrier2( const VkBufferMemoryBarrier2& b )
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
	VkImageMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.image = hImg;
	barrier.srcAccessMask = srcAccessMask;
	barrier.srcStageMask = srcStageMask;
	barrier.dstAccessMask = dstAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
	barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	barrier.subresourceRange.aspectMask = aspectMask;
	barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
	barrier.subresourceRange.baseMipLevel = baseMipLevel;
	barrier.subresourceRange.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS;
	barrier.subresourceRange.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS;

	return barrier;
}

inline void VkCmdPipelineBarrier(
	VkCommandBuffer cmdBuff,
	const std::span<VkBufferMemoryBarrier2> buffBarriers,
	const std::span<VkImageMemoryBarrier2> imgBarriers
) {
	VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.imageMemoryBarrierCount = std::size( imgBarriers );
	dependency.pImageMemoryBarriers = std::data( imgBarriers );
	dependency.bufferMemoryBarrierCount = std::size( buffBarriers );
	dependency.pBufferMemoryBarriers = std::data( buffBarriers );
	vkCmdPipelineBarrier2( cmdBuff, &dependency );
}

inline void VkCmdPipelineFullMemoryBarrier( VkCommandBuffer cmdBuff, const std::span<VkMemoryBarrier2> memBarriers )
{
	VkDependencyInfo dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependency.memoryBarrierCount = std::size( memBarriers );
	dependency.pMemoryBarriers = std::data( memBarriers );
	vkCmdPipelineBarrier2( cmdBuff, &dependency );
}