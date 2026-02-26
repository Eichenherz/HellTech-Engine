#ifndef __VK_COMMAND_BUFFER_H__
#define __VK_COMMAND_BUFFER_H__

#define VK_NO_PROTOTYPES
#include <vulkan.h>
#include <Volk/volk.h>

#include "core_types.h"

#include "vk_resources.h"
#include "r_data_structs.h"

#include <span>

// TODO: move
#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;

struct vk_scoped_label
{
	VkCommandBuffer cmdBuff;

	inline vk_scoped_label( VkCommandBuffer _cmdBuff, const char* labelName, DXPacked::XMCOLOR col )
	{
		this->cmdBuff = _cmdBuff;

		VkDebugUtilsLabelEXT dbgLabel = {
			.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
			.pLabelName = labelName,
			.color = { ( float ) col.r, ( float ) col.g, ( float ) col.b, ( float ) col.a },
		};
		vkCmdBeginDebugUtilsLabelEXT( cmdBuff, &dbgLabel );
	}
	inline ~vk_scoped_label()
	{
		vkCmdEndDebugUtilsLabelEXT( cmdBuff );
	}
};

// TODO: handle dynamic state  
struct vk_rendering_info
{
	alignas( 8 ) VkViewport							viewport;
	alignas( 8 ) VkRect2D							scissor;
	std::span<const VkRenderingAttachmentInfo>	colorAttachments;
	const VkRenderingAttachmentInfo*			pDepthAttachment;
};


struct vk_scoped_renderpass
{
	VkCommandBuffer cmdBuff;

	inline vk_scoped_renderpass( VkCommandBuffer _cmdBuff, const VkRenderingInfo& renderInfo ) 
	{
		this->cmdBuff = _cmdBuff;
		vkCmdBeginRendering( cmdBuff, &renderInfo );
	}
	inline ~vk_scoped_renderpass()
	{
		vkCmdEndRendering( cmdBuff );
	}
};

struct vk_command_buffer
{
	VkCommandBuffer hndl;
	VkPipelineLayout bindlessPipelineLayout;
	VkDescriptorSet bindlessDescriptorSet;
	VkPipelineBindPoint currentBindPoint;

	vk_command_buffer( VkCommandBuffer cmdBuff, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet )
	{
		this->hndl = cmdBuff;
		this->bindlessPipelineLayout = pipelineLayout;
		this->bindlessDescriptorSet = descriptorSet;
		this->currentBindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;

		VkCommandBufferBeginInfo cmdBufBegInfo = { 
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};
		vkBeginCommandBuffer( hndl, &cmdBufBegInfo );
	}

	vk_scoped_label CmdIssueScopedLabel( const char* labelName, DXPacked::XMCOLOR col = {} )
	{
		return { hndl, labelName, col };
	}

	vk_scoped_renderpass CmdIssueScopedRenderPass( const vk_rendering_info& renderingInfo ) 
	{
		vkCmdSetScissor( hndl, 0, 1, &renderingInfo.scissor );
		vkCmdSetViewport( hndl, 0, 1, &renderingInfo.viewport );

		VkRenderingInfo vkRenderInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = renderingInfo.scissor,
			.layerCount = 1,
			.colorAttachmentCount = ( u32 ) std::size( renderingInfo.colorAttachments ),
			.pColorAttachments = std::data( renderingInfo.colorAttachments ),
			.pDepthAttachment = renderingInfo.pDepthAttachment
		};
		return { hndl, vkRenderInfo };
	}

	void CmdBindPipelineAndBindlessDesc( VkPipeline pipeline, const VkPipelineBindPoint bindPoint )
	{
		if( bindPoint != currentBindPoint )
		{
			vkCmdBindDescriptorSets( hndl, bindPoint, bindlessPipelineLayout, 0, 1, &bindlessDescriptorSet, 0, 0 );
			currentBindPoint = bindPoint;
		}
		vkCmdBindPipeline( hndl, bindPoint, pipeline );
	}

	void CmdPushConstants( const void* pData, u32 size )
	{
		VkPushConstantsInfo pushConstInfo = {
			.sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
			.layout = bindlessPipelineLayout,
			.stageFlags = VK_SHADER_STAGE_ALL,
			.offset = 0,
			.size = size,
			.pValues = pData
		};
		vkCmdPushConstants2( hndl, &pushConstInfo );
	}

	void CmdDispatch( DirectX::XMUINT3 numWorkgroups )
	{
		vkCmdDispatch( hndl, numWorkgroups.x, numWorkgroups.y, numWorkgroups.z );
	}

	void CmdDrawIndexedIndirectCount( 
		const vk_buffer& idxBuffer, 
		VkIndexType      idxType,
		const vk_buffer& drawCmds, 
		const vk_buffer& drawCount,
		u32              maxDrawCount
	) {
		vkCmdBindIndexBuffer( hndl, idxBuffer.hndl, 0, idxType );
		vkCmdDrawIndexedIndirectCount( hndl, drawCmds.hndl, 0, drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );
	}

	void CmdPipelineBarriers( 
		std::span<const VkBufferMemoryBarrier2> buffBarriers, 
		std::span<const VkImageMemoryBarrier2> imgBarriers 
	) const 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = ( u32 ) std::size( buffBarriers ),
			.pBufferMemoryBarriers = std::data( buffBarriers ),
			.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
			.pImageMemoryBarriers = std::data( imgBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdCopyBuffer( const vk_buffer& src, const vk_buffer& dst, const VkBufferCopy& copyRegion )
	{
		vkCmdCopyBuffer( hndl, src.hndl, dst.hndl, 1, &copyRegion );
	}

	void CmdCopyBufferToImageSubresource( 
		const vk_buffer&					src, 
		u64									srcOffset, 
		const vk_image&						dst,
		const VkImageSubresourceLayers&     dstSubresourceLayers
	) {
		VkBufferImageCopy imgCopyRegion = {
			.bufferOffset = srcOffset,
			.imageSubresource = dstSubresourceLayers,
			.imageOffset = {},
			.imageExtent = { u32( dst.width ), u32( dst.height ), 1 }
		};

		vkCmdCopyBufferToImage( hndl, src.hndl, dst.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );
	}

	void CmdPipelineMemoryBarriers( std::span<VkMemoryBarrier2> memBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.memoryBarrierCount = ( u32 ) std::size( memBarriers ),
			.pMemoryBarriers = std::data( memBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdPipelineBufferBarriers( const VkBufferMemoryBarrier2& buffBarrier ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = 1,
			.pBufferMemoryBarriers = &buffBarrier,
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdPipelineBufferBarriers( std::span<VkBufferMemoryBarrier2> buffBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = ( u32 ) std::size( buffBarriers ),
			.pBufferMemoryBarriers = std::data( buffBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdPipelineImageBarriers( const VkImageMemoryBarrier2& imgBarrier ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = 1,
			.pImageMemoryBarriers = &imgBarrier,
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdPipelineImageBarriers( std::span<VkImageMemoryBarrier2> imgBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
			.pImageMemoryBarriers = std::data( imgBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
	}

	void CmdFillVkBuffer( const vk_buffer& vkBuffer, u32 fillValue )
	{
		vkCmdFillBuffer( hndl, vkBuffer.hndl, 0, vkBuffer.sizeInBytes, fillValue );
	}

	void CmdEndCmbBuffer()
	{
		VK_CHECK( vkEndCommandBuffer( hndl ) );
	}
};

#endif // !__VK_COMMAND_BUFFER_H__
