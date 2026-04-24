#pragma once

#ifndef __VK_SYNC_H__
#define __VK_SYNC_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include "ht_core_types.h"
#include "vk_resources.h"
#include "vk_command_buffer.h"

#include "ht_fixed_vector.h"
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
	VkAccessFlags2        accessFlags	= VK_ACCESS_2_NONE;
	VkPipelineStageFlags2 stageFlags	= VK_PIPELINE_STAGE_2_NONE;
};

constexpr vk_access_stage_masks COMPUTE_READ = { VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT };
constexpr vk_access_stage_masks COMPUTE_WRITE = { VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT };
constexpr vk_access_stage_masks COMPUTE_READWRITE = { HT_SHADER_ACCESS_READ_WRITE, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT };
constexpr vk_access_stage_masks TRANSFER_WRITE = { VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT };

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

inline VkBufferMemoryBarrier2 VkMakeBufferBarrier(
	VkBuffer                        buff,
	VkPipelineStageFlags2			srcStageMask,
	VkAccessFlags2					srcAccessMask,
	VkPipelineStageFlags2			dstStageMask,
	VkAccessFlags2					dstAccessMask,
	VkDeviceSize					offset              = 0,
	VkDeviceSize					size                = VK_WHOLE_SIZE,
	u32								srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32								dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
) {
	return {
		.sType					= VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
		.srcStageMask			= srcStageMask,
		.srcAccessMask			= srcAccessMask,
		.dstStageMask			= dstStageMask,
		.dstAccessMask			= dstAccessMask,
		.srcQueueFamilyIndex	= srcQueueFamilyIndex,
		.dstQueueFamilyIndex	= dstQueueFamilyIndex,
		.buffer					= buff,
		.offset					= offset,
		.size					= size
	};
}

inline VkBufferMemoryBarrier2 VkMakeBufferBarrier(
	const vk_buffer&                buff,
	const vk_access_stage_masks&	srcSync,
	const vk_access_stage_masks&	dstSync,
	VkDeviceSize					offset	= 0,
	VkDeviceSize					size	= VK_WHOLE_SIZE
) {
	return VkMakeBufferBarrier( buff.hndl, srcSync.stageFlags, srcSync.accessFlags, dstSync.stageFlags,
		dstSync.accessFlags, offset, size, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED );
}



inline VkImageMemoryBarrier2 VkMakeImageBarrier(
	const vk_image&					img,
	const vk_access_stage_masks&	srcSync,
	const vk_access_stage_masks&	dstSync,
	VkImageLayout					srcLayout,
	VkImageLayout					dstLayout,
	const VkImageSubresourceRange&	subResource
) {
	return {
		.sType					= VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
		.srcStageMask			= srcSync.stageFlags,
		.srcAccessMask			= srcSync.accessFlags,
		.dstStageMask			= dstSync.stageFlags,
		.dstAccessMask			= dstSync.accessFlags,
		.oldLayout				= srcLayout,
		.newLayout				= dstLayout,
		.srcQueueFamilyIndex	= VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex	= VK_QUEUE_FAMILY_IGNORED,
		.image					= img.hndl,
		.subresourceRange		= subResource
	};
}


using vk_rsc_hndl64 = u64;
struct vk_rsc_state_tracker
{
	template <class Key, class T>
	using unordered_dense = ankerl::unordered_dense::map<Key, T>;

	unordered_dense<vk_rsc_hndl64, vk_rsc_sync_state>	resourceStateTracker;
	fixed_vector<VkBufferMemoryBarrier2, 16>			buffBarrierCache;
	fixed_vector<VkImageMemoryBarrier2, 16>				imgBarrierCache;

	// NOTE: buffers will always be in VK_IMAGE_LAYOUT_MAX_ENUM aka INVALID
	void UseBuffer( 
		const vk_buffer&				rsc,
		const vk_access_stage_masks&	dstMasks,
		VkDeviceSize					offset = 0,
		VkDeviceSize					size = VK_WHOLE_SIZE
	) {
		auto it = resourceStateTracker.find( ( u64 ) rsc.hndl );
		if( std::cend( resourceStateTracker ) == it )
		{
			resourceStateTracker.emplace( ( u64 ) rsc.hndl, vk_rsc_sync_state{ dstMasks, VK_IMAGE_LAYOUT_MAX_ENUM } );
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
				buffBarrierCache.push_back( VkMakeBufferBarrier( rsc, currentLastWriteMask, dstMasks, offset, size ) );
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

			buffBarrierCache.push_back( VkMakeBufferBarrier( rsc, srcMask, dstMasks, offset, size ) );
			it->second = { .lastWriteMask = dstMasks, .perStageReaders = 0 };
		}
	}

	inline void UseImage( const vk_image& rsc, const vk_access_stage_masks& dstMasks, VkImageLayout dstLayout )
	{
		VkImageSubresourceRange subResource = VkFullResource( rsc );
		UseImage( rsc, dstMasks, dstLayout, subResource );
	}

	void UseImage( 
		const vk_image&					rsc,
		const vk_access_stage_masks&	dstMasks,
		VkImageLayout					dstLayout,
		const VkImageSubresourceRange&	subResource
	) {
		auto it = resourceStateTracker.find( ( u64 ) rsc.hndl );
		if( std::cend( resourceStateTracker ) == it )
		{
			resourceStateTracker.emplace( ( u64 ) rsc.hndl, vk_rsc_sync_state{ dstMasks, dstLayout } );
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
				imgBarrierCache.push_back( VkMakeImageBarrier( 
					rsc, currentLastWriteMask, dstMasks, currentImgLayout, dstLayout, subResource ) );
			}
			// NOTE: here we use the read flags bc there's no just change my layout barrier
			if( !hasPrevWrite && isLayoutTransition )
			{
				imgBarrierCache.push_back( VkMakeImageBarrier(
					rsc, currentReadMask, dstMasks, currentImgLayout, dstLayout, subResource ) );
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
			imgBarrierCache.push_back( VkMakeImageBarrier(
				rsc, srcMask, dstMasks, currentImgLayout, dstLayout, subResource ) );
			it->second = { .lastWriteMask = dstMasks, .imgLayout = dstLayout, .perStageReaders = 0 };
		}
	}

	inline void StopTrackingResource( vk_rsc_hndl64 hndl )
	{
		resourceStateTracker.erase( hndl );
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

