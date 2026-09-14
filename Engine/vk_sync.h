#pragma once

#ifndef __VK_SYNC_H__
#define __VK_SYNC_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <ht_core_types.h>

#include "vk_resources.h"

constexpr VkAccessFlags2 VK_ALL_WRITE_ACCESSES =
VK_ACCESS_2_MEMORY_WRITE_BIT |
VK_ACCESS_2_SHADER_WRITE_BIT |
VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
VK_ACCESS_2_TRANSFER_WRITE_BIT |
VK_ACCESS_2_HOST_WRITE_BIT;

constexpr bool VkIsWriteAccess( VkAccessFlags2 access )
{
	return access & VK_ALL_WRITE_ACCESSES;
}

inline VkImageMemoryBarrier2 VkMakeImageBarrier(
	VkImage							img,
	VkPipelineStageFlags2			srcStageMask,
	VkAccessFlags2					srcAccessMask,
	VkPipelineStageFlags2			dstStageMask,
	VkAccessFlags2					dstAccessMask,
	VkImageLayout					srcLayout,
	VkImageLayout					dstLayout,
	const VkImageSubresourceRange&	subResource,
	u32								srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32								dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
) {
	return {
		.sType					= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask			= srcStageMask,
		.srcAccessMask			= srcAccessMask,
		.dstStageMask			= dstStageMask,
		.dstAccessMask			= dstAccessMask,
		.oldLayout				= srcLayout,
		.newLayout				= dstLayout,
		.srcQueueFamilyIndex	= srcQueueFamilyIndex,
		.dstQueueFamilyIndex	= dstQueueFamilyIndex,
		.image					= img,
		.subresourceRange		= subResource
	};
}

inline VkImageMemoryBarrier2 VkMakeImageBarrier(
	const vk_image&					img,
	VkPipelineStageFlags2			srcStageMask,
	VkAccessFlags2					srcAccessMask,
	VkPipelineStageFlags2			dstStageMask,
	VkAccessFlags2					dstAccessMask,
	VkImageLayout					srcLayout,
	VkImageLayout					dstLayout
) {
	return VkMakeImageBarrier( img.hndl, srcStageMask, srcAccessMask, dstStageMask, dstAccessMask,
		srcLayout, dstLayout, VkFullResource( img ), VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED );
}

enum ht_sync_exec_mask : VkPipelineStageFlags2
{
    HT_SYNC_EXEC_MASK_GFX         = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    HT_SYNC_EXEC_MASK_COMP_SRC    = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    // NOTE: draw/dispatch indirect is before compute
    HT_SYNC_EXEC_MASK_COMP_DST    = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
    HT_SYNC_EXEC_MASK_XFER        = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
    HT_SYNC_EXEC_MASK_ALL         = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
};

enum ht_sync_cache_flush_mask : VkAccessFlags2
{
    HT_SYNC_CACHE_FLUSH_MASK_SHADER          = VK_ACCESS_2_SHADER_WRITE_BIT,
    HT_SYNC_CACHE_FLUSH_MASK_XFER            = VK_ACCESS_2_TRANSFER_WRITE_BIT,
    HT_SYNC_CACHE_FLUSH_MASK_COL_TARGET      = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    HT_SYNC_CACHE_FLUSH_MASK_DEPTH_TARGET    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
};

enum ht_sync_cache_inval_mask : VkAccessFlags2
{
    HT_SYNC_CACHE_INVAL_MASK_SHADER          = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_UNIFORM_READ_BIT |
                        VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
    HT_SYNC_CACHE_INVAL_MASK_GFX             = HT_SYNC_CACHE_INVAL_MASK_SHADER | VK_ACCESS_2_INDEX_READ_BIT,
    HT_SYNC_CACHE_INVAL_MASK_XFER            = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
    HT_SYNC_CACHE_INVAL_MASK_COL_TARGET      = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    HT_SYNC_CACHE_INVAL_MASK_DEPTH_TARGET    = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    HT_SYNC_CACHE_INVAL_MASK_TARGET          = HT_SYNC_CACHE_INVAL_MASK_COL_TARGET | HT_SYNC_CACHE_INVAL_MASK_DEPTH_TARGET,
};

constexpr ht_sync_exec_mask& operator|=( ht_sync_exec_mask& a, ht_sync_exec_mask b )
{
    return a = ( ht_sync_exec_mask )( ( VkPipelineStageFlags2 ) a | ( VkPipelineStageFlags2 ) b );
}

constexpr ht_sync_cache_flush_mask& operator|=( ht_sync_cache_flush_mask& a, ht_sync_cache_flush_mask b )
{
    return a = ( ht_sync_cache_flush_mask )( ( VkAccessFlags2 ) a | ( VkAccessFlags2 ) b );
}

constexpr ht_sync_exec_mask HtSyncExecMaskInvert( ht_sync_exec_mask exec )
{
    switch( exec )
    {
    case HT_SYNC_EXEC_MASK_COMP_SRC: return HT_SYNC_EXEC_MASK_COMP_DST;
    case HT_SYNC_EXEC_MASK_COMP_DST: return HT_SYNC_EXEC_MASK_COMP_SRC;
    }

    return exec;
}

constexpr ht_sync_cache_inval_mask HtSyncGetInvalFromExec( ht_sync_exec_mask exec )
{
    switch( exec )
    {
    case HT_SYNC_EXEC_MASK_GFX:  return HT_SYNC_CACHE_INVAL_MASK_GFX;
    case HT_SYNC_EXEC_MASK_XFER: return HT_SYNC_CACHE_INVAL_MASK_XFER;
    }

    return HT_SYNC_CACHE_INVAL_MASK_SHADER;
}

constexpr ht_sync_cache_inval_mask HtSyncGetTargetInvalFromAspect( const vk_image& img )
{
    switch( VkSelectAspectMaskFromFormat( img.format ) )
    {
    case VK_IMAGE_ASPECT_DEPTH_BIT: return HT_SYNC_CACHE_INVAL_MASK_DEPTH_TARGET;
    }

    return HT_SYNC_CACHE_INVAL_MASK_COL_TARGET;
}

constexpr ht_sync_cache_flush_mask HtSyncGetFlushFromInval( ht_sync_cache_inval_mask inval )
{
    return ( ht_sync_cache_flush_mask )( inval & VK_ALL_WRITE_ACCESSES );
}

constexpr VkMemoryBarrier2 VkMakeBarrier(
    VkPipelineStageFlags2       srcExec,
    VkAccessFlags2              accessFlush,
    VkPipelineStageFlags2       dstExec,
    VkAccessFlags2              invalMask
) {
   return {
        .sType			= VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask	= srcExec,
        .srcAccessMask	= accessFlush,
        .dstStageMask	= dstExec,
        .dstAccessMask	= invalMask,
    };
}

struct ht_img_layout_transition
{
    const vk_image&     img;
    VkImageLayout       srcLayout;
    VkImageLayout       dstLayout;
};

constexpr ht_sync_cache_inval_mask HtSyncInvalFromLayout( VkImageLayout layout )
{
    switch( layout )
    {
    case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:        return HT_SYNC_CACHE_INVAL_MASK_TARGET;
    case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
    case VK_IMAGE_LAYOUT_GENERAL:                   return HT_SYNC_CACHE_INVAL_MASK_SHADER;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:      return HT_SYNC_CACHE_INVAL_MASK_XFER;
    }
    return ( ht_sync_cache_inval_mask ) VK_ACCESS_2_NONE;
}

constexpr ht_sync_cache_flush_mask HtSyncFlushFromLayout( VkImageLayout layout )
{
    return HtSyncGetFlushFromInval( HtSyncInvalFromLayout( layout ) );
}

inline VkImageMemoryBarrier2 VkMakeImageBarrier(
    const vk_image&             img,
    ht_sync_exec_mask           srcExec,
    ht_sync_cache_flush_mask    flushMask,
    ht_sync_exec_mask           dstExec,
    ht_sync_cache_inval_mask    invalMask,
    VkImageLayout               srcLayout,
    VkImageLayout               dstLayout
) {
    return {
        .sType					= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask			= srcExec,
        .srcAccessMask			= flushMask,
        .dstStageMask			= dstExec,
        .dstAccessMask			= invalMask,
        .oldLayout				= srcLayout,
        .newLayout				= dstLayout,
        .srcQueueFamilyIndex	= VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex	= VK_QUEUE_FAMILY_IGNORED,
        .image					= img.hndl,
        .subresourceRange		= VkFullResource( img )
    };
}

#endif // !__VK_SYNC_H__

