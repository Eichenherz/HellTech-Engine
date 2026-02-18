#pragma once

#ifndef __VK_SYNC_H__
#define __VK_SYNC_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "core_types.h"
#include "ht_error.h"
#include "vk_resources.h"
#include "vk_command_buffer.h"

#include <EASTL/fixed_vector.h>
#include <ankerl/unordered_dense.h>

constexpr VkAccessFlags2 HT_SHADER_ACCESS_READ_WRITE = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
constexpr VkAccessFlags2 HT_COLOR_ATTACHMENT_ACCESS_READ_WRITE = 
VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
constexpr VkAccessFlags2 HT_DEPTH_ATTACHMENT_ACCESS_READ_WRITE = 
VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

constexpr VkPipelineStageFlags2 HT_FRAGMENT_TESTS_STAGE = 
VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

enum vk_read_stages_bits : u64
{
	R_DRAW_INDIRECT            = 1ull << 0, // includes vkCmdDispatchIndirect params
	R_INDEX_INPUT              = 1ull << 1, 
	R_VERTEX_SHADER            = 1ull << 2,
	R_FRAGMENT_SHADER          = 1ull << 3,
	R_EARLY_FRAGMENT_TESTS     = 1ull << 4,
	R_LATE_FRAGMENT_TESTS      = 1ull << 5,
	R_COLOR_ATTACHMENT_OUTPUT  = 1ull << 6,
	R_COMPUTE_SHADER           = 1ull << 7,
	R_TRANSFER                 = 1ull << 8, // covers copy/blit/resolve/clear if you use ALL_TRANSFER/TRANSFER
	R_HOST                     = 1ull << 9,
};

inline u64 VkReadStagesFlagsFromVkStages( VkPipelineStageFlags2 s )
{
	u64 flagBits = 0;

	if( s & VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT )             flagBits |= R_DRAW_INDIRECT;
	if( s & VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT )               flagBits |= R_INDEX_INPUT;

	if( s & VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT )             flagBits |= R_VERTEX_SHADER;
	if( s & VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT )           flagBits |= R_FRAGMENT_SHADER;
	if( s & VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT )      flagBits |= R_EARLY_FRAGMENT_TESTS;
	if( s & VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT )       flagBits |= R_LATE_FRAGMENT_TESTS;
	if( s & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT )   flagBits |= R_COLOR_ATTACHMENT_OUTPUT;

	if( s & VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT )            flagBits |= R_COMPUTE_SHADER;

	if( s & ( VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT |
		VK_PIPELINE_STAGE_2_COPY_BIT |
		VK_PIPELINE_STAGE_2_BLIT_BIT |
		VK_PIPELINE_STAGE_2_RESOLVE_BIT |
		VK_PIPELINE_STAGE_2_CLEAR_BIT ) )
		flagBits |= R_TRANSFER; // ALL_TRANSFER is equivalent to OR of those sub-stages 

	if( s & VK_PIPELINE_STAGE_2_HOST_BIT )                      flagBits |= R_HOST;

	return flagBits;
}

struct vk_access_stage_masks
{
	VkAccessFlags2        accessFlags = VK_ACCESS_2_NONE; 
	VkPipelineStageFlags2 stageFlags = VK_PIPELINE_STAGE_2_NONE;
};

inline vk_access_stage_masks VkGetAccessAndStageFromReadStagesBits( u64 mask )
{
	VkAccessFlags2        accessFlags = 0; 
	VkPipelineStageFlags2 stageFlags = 0;

	if( mask & R_DRAW_INDIRECT )
	{
		accessFlags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT; // indirect draw + dispatch params
		stageFlags  |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	}

	if( mask & R_INDEX_INPUT )
	{
		accessFlags |= VK_ACCESS_2_INDEX_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
	}

	if( mask & R_VERTEX_SHADER )
	{
		accessFlags |= VK_ACCESS_2_SHADER_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
	}

	if( mask & R_FRAGMENT_SHADER )
	{
		accessFlags |= VK_ACCESS_2_SHADER_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	}

	if( mask & R_COMPUTE_SHADER )
	{
		accessFlags |= VK_ACCESS_2_SHADER_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	}

	if( mask & R_EARLY_FRAGMENT_TESTS )
	{
		accessFlags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
	}

	if( mask & R_LATE_FRAGMENT_TESTS )
	{
		accessFlags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	}

	if( mask & R_COLOR_ATTACHMENT_OUTPUT )
	{
		accessFlags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	}

	if( mask & R_TRANSFER )
	{
		accessFlags |= VK_ACCESS_2_TRANSFER_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
	}

	if( mask & R_HOST )
	{
		accessFlags |= VK_ACCESS_2_HOST_READ_BIT;
		stageFlags  |= VK_PIPELINE_STAGE_2_HOST_BIT;
	}

	return { .accessFlags = accessFlags, .stageFlags = stageFlags };
}

constexpr VkAccessFlags2 VK_ALL_WRITE_ACCESSES =
VK_ACCESS_2_MEMORY_WRITE_BIT |
VK_ACCESS_2_SHADER_WRITE_BIT |
VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
VK_ACCESS_2_TRANSFER_WRITE_BIT |
VK_ACCESS_2_HOST_WRITE_BIT;

inline constexpr bool VkIsWriteAccess( VkAccessFlags2 access )
{
	return access & VK_ALL_WRITE_ACCESSES;
}

struct vk_rsc_sync_state
{
	vk_access_stage_masks   lastWriteMask;
	VkImageLayout			imgLayout;
	u64			            perStageReaders;
};



__forceinline VkImageSubresourceRange VkFullResource( const vk_image& img ) 
{
	return {
		.aspectMask = VkSelectAspectMaskFromFormat( img.format ),
		.baseMipLevel = 0,
		.levelCount = VK_REMAINING_MIP_LEVELS,
		.baseArrayLayer = 0,
		.layerCount = VK_REMAINING_ARRAY_LAYERS
	};
}

struct vk_rsc_state_tracker
{
	ankerl::unordered_dense::map<vk_rsc_hndl64, vk_rsc_sync_state> resourceStateTracker;
	eastl::fixed_vector<VkBufferMemoryBarrier2, 16, false> buffBarrierCache;
	eastl::fixed_vector<VkImageMemoryBarrier2, 16, false> imgBarrierCache;

	// NOTE: buffers will always be in VK_IMAGE_LAYOUT_MAX_ENUM aka INVALID
	void UseBuffer( 
		const vk_buffer& rsc, 
		const vk_access_stage_masks& dstMasks, 
		VkDeviceSize offset = 0, 
		VkDeviceSize size = VK_WHOLE_SIZE
	) {
		auto it = resourceStateTracker.find( vk_rsc_hndl64{ rsc } );
		if( std::cend( resourceStateTracker ) == it )
		{
			resourceStateTracker.emplace( vk_rsc_hndl64{ rsc }, vk_rsc_sync_state{ dstMasks, VK_IMAGE_LAYOUT_MAX_ENUM } );
			return;
		}
		
		const vk_access_stage_masks currentLastWriteMask = it->second.lastWriteMask;
		const VkImageLayout currentImgLayout = it->second.imgLayout;
		const u64 currentPerStageReaders = it->second.perStageReaders;

		const vk_access_stage_masks currentReadMask = VkGetAccessAndStageFromReadStagesBits( currentPerStageReaders );

		bool isSyncReqWrite = VkIsWriteAccess( dstMasks.accessFlags );
		if( !isSyncReqWrite )
		{
			bool hasPrevWrite = currentLastWriteMask.accessFlags && currentLastWriteMask.stageFlags;

			u64 syncReqScopeBits = VkReadStagesFlagsFromVkStages( dstMasks.stageFlags );
			bool syncReqScopeSawPrevWrite = currentPerStageReaders & syncReqScopeBits;

			if( hasPrevWrite && syncReqScopeSawPrevWrite ) return;

			if( hasPrevWrite && !syncReqScopeSawPrevWrite )
			{
				// NOTE: implicitly handle the layout trsnsition too 
				EmitBarrier( rsc, currentLastWriteMask, dstMasks, offset, size );
			}

			it->second = {
				.lastWriteMask = currentLastWriteMask,
				.perStageReaders = currentPerStageReaders | syncReqScopeBits
			};
		}
		else
		{
			vk_access_stage_masks srcMask = {
				.accessFlags = currentLastWriteMask.accessFlags | currentReadMask.accessFlags,
				.stageFlags = currentLastWriteMask.stageFlags | currentReadMask.stageFlags
			};

			EmitBarrier( rsc, srcMask, dstMasks, offset, size );
			it->second = { .lastWriteMask = dstMasks, .perStageReaders = 0 };
		}
	}

	inline void UseImage( const vk_image& rsc, const vk_access_stage_masks& dstMasks, VkImageLayout dstLayout )
	{
		VkImageSubresourceRange subResource = VkFullResource( rsc );
		UseImage( rsc, dstMasks, dstLayout, subResource );
	}

	void UseImage( 
		const vk_image& rsc, 
		const vk_access_stage_masks& dstMasks, 
		VkImageLayout dstLayout, 
		const VkImageSubresourceRange& subResource 
	) {
		auto it = resourceStateTracker.find( vk_rsc_hndl64{ rsc } );
		if( std::cend( resourceStateTracker ) == it )
		{
			resourceStateTracker.emplace( vk_rsc_hndl64{ rsc }, vk_rsc_sync_state{ dstMasks, dstLayout } );
			return;
		}

		const vk_access_stage_masks currentLastWriteMask = it->second.lastWriteMask;
		const VkImageLayout currentImgLayout = it->second.imgLayout;
		const u64 currentPerStageReaders = it->second.perStageReaders;

		const vk_access_stage_masks currentReadMask = VkGetAccessAndStageFromReadStagesBits( currentPerStageReaders );

		bool isSyncReqWrite = VkIsWriteAccess( dstMasks.accessFlags );
		if( !isSyncReqWrite )
		{
			bool hasPrevWrite = currentLastWriteMask.accessFlags && currentLastWriteMask.stageFlags;

			u64 syncReqScopeBits = VkReadStagesFlagsFromVkStages( dstMasks.stageFlags );
			bool syncReqScopeSawPrevWrite = currentPerStageReaders & syncReqScopeBits;

			// NOTE: buffers will always be in VK_IMAGE_LAYOUT_MAX_ENUM aka INVALID
			bool needsLayoutTransition = ( VK_IMAGE_LAYOUT_MAX_ENUM != dstLayout );
			bool isLayoutTransition = needsLayoutTransition && ( dstLayout != currentImgLayout );

			if( hasPrevWrite && syncReqScopeSawPrevWrite && !isLayoutTransition ) return;

			if( hasPrevWrite && !syncReqScopeSawPrevWrite )
			{
				// NOTE: implicitly handle the layout trsnsition too 
				EmitBarrier( rsc, currentLastWriteMask, dstMasks, currentImgLayout, dstLayout, subResource );
			}
			// NOTE: here we use the read flags bc there's no just change my layout barrier
			if( !hasPrevWrite && isLayoutTransition )
			{
				EmitBarrier( rsc, currentReadMask, dstMasks, currentImgLayout, dstLayout, subResource );
			}

			it->second = {
				.lastWriteMask = currentLastWriteMask,
				.imgLayout = dstLayout,
				.perStageReaders = currentPerStageReaders | syncReqScopeBits
			};
		}
		else
		{
			vk_access_stage_masks srcMask = {
				.accessFlags = currentLastWriteMask.accessFlags | currentReadMask.accessFlags,
				.stageFlags = currentLastWriteMask.stageFlags | currentReadMask.stageFlags
			};
			EmitBarrier( rsc, srcMask, dstMasks, currentImgLayout, dstLayout, subResource );
			it->second = { .lastWriteMask = dstMasks, .imgLayout = dstLayout, .perStageReaders = 0 };
		}
	}

	inline void StopTrackingResource( vk_rsc_hndl64 hndl )
	{
		resourceStateTracker.erase( hndl );
	}

	inline void EmitBarrier(
		const vk_buffer& buff,
		const vk_access_stage_masks& srcSync,
		const vk_access_stage_masks& dstSync,
		VkDeviceSize offset,
		VkDeviceSize size
	) {
		buffBarrierCache.push_back( {
			.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
			.srcStageMask = srcSync.stageFlags,
			.srcAccessMask = srcSync.accessFlags,
			.dstStageMask = dstSync.stageFlags,
			.dstAccessMask = dstSync.accessFlags,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.buffer = buff.hndl,
			.offset = offset,
			.size = size
		} );
	}

	inline void EmitBarrier(
		const vk_image& img,
		const vk_access_stage_masks& srcSync,
		const vk_access_stage_masks& dstSync,
		VkImageLayout srcLayout,
		VkImageLayout dstLayout,
		const VkImageSubresourceRange& subResource
	) {
		HT_ASSERT( VK_IMAGE_LAYOUT_MAX_ENUM != dstLayout );

		
		imgBarrierCache.push_back( {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask = srcSync.stageFlags,
			.srcAccessMask = srcSync.accessFlags,
			.dstStageMask = dstSync.stageFlags,
			.dstAccessMask = dstSync.accessFlags,
			.oldLayout = srcLayout,
			.newLayout = dstLayout,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = img.hndl,
			.subresourceRange = subResource
			} );
	}

	inline void FlushBarriers( const vk_command_buffer& cmdBuff )
	{
		if( !std::size( buffBarrierCache ) && !std::size( imgBarrierCache ) ) return;
		cmdBuff.CmdPipelineBarriers( buffBarrierCache, imgBarrierCache );
		buffBarrierCache.resize( 0 );
		imgBarrierCache.resize( 0 );
	}
};

#endif // !__VK_SYNC_H__

