#include "vk_commands.hpp"
#include "vk_utils.hpp"
#include "r_data_structs.h"


void vk_command_buffer::Begin()
{
	VkCommandBufferBeginInfo cmdBufBegInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 
	};
	vkBeginCommandBuffer( this->hndl, &cmdBufBegInfo );
}
void vk_command_buffer::End()
{
	VK_CHECK( vkEndCommandBuffer( this->hndl ) );
}
void vk_command_buffer::FlushMemoryBarriers( const std::span<VkMemoryBarrier2> memBarriers )
{
	VkDependencyInfo dependency = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = ( u32 ) std::size( memBarriers ),
		.pMemoryBarriers = std::data( memBarriers ),
	};
	vkCmdPipelineBarrier2( this->hndl, &dependency );
}
void vk_command_buffer::ImageLayoutTransitionBarriers(const std::span<VkImageMemoryBarrier2> imgBarriers)
{
	VkDependencyInfo dependency = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
		.pImageMemoryBarriers = std::data( imgBarriers ),
	};
	vkCmdPipelineBarrier2( this->hndl, &dependency );
}
void vk_command_buffer::FillBuffer( vk_buffer& buff, u32 val )
{
	vkCmdFillBuffer( this->hndl, buff.hndl, 0, buff.size, val );
}


void vk_transfer_command::CopyBuffer( const vk_buffer& src, const vk_buffer& dest )
{
	VkBufferCopy copyRegion = { 0,0,src.size };
	vkCmdCopyBuffer( this->hndl, src.hndl, dest.hndl, 1, &copyRegion );
}
// TODO: do we need more stuff here
void vk_transfer_command::CopyBufferToImage( const vk_buffer& src, const vk_image& dest )
{
	VkBufferImageCopy imgCopyRegion = {
		.bufferOffset = 0,
		.imageSubresource = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
			.imageOffset = {},
			.imageExtent = { u32( dest.width ), u32( dest.height ),1 }
	};

	vkCmdCopyBufferToImage( this->hndl, src.hndl, dest.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );
}


inline void VkBindComputePSO( VkCommandBuffer cmdBuff, const dispatch_command& args )
{
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, args.pso );
	vkCmdPushConstants( 
		cmdBuff, args.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, std::size( args.pushConst ), std::data( args.pushConst ) );
}

void vk_compute_command::Dispatch( const dispatch_command& args )
{
	VkBindComputePSO( this->hndl, args );
	vkCmdDispatch( this->hndl, args.workgrX, args.workgrY, args.workgrZ );
}
void vk_compute_command::DispatchIndirect( const dispatch_command& args, const vk_buffer& dispatchCmdsBuff )
{
	VkBindComputePSO( this->hndl, args );
	vkCmdDispatchIndirect( this->hndl, dispatchCmdsBuff.hndl, 0 );
}

// NOTE: what if CPP had "scoped-paired" functions ?
struct vk_rendering_cmd
{
	VkCommandBuffer& _cmdBuff;

	inline vk_rendering_cmd( VkCommandBuffer cmdBuff, const render_command& args ) : _cmdBuff{ cmdBuff }
	{
		VkRenderingInfo renderInfo = { 
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = args.scissor,
			.layerCount = 1,
			.colorAttachmentCount = ( u32 )bool( args.pColInfo ),
			.pColorAttachments = args.pColInfo,
			.pDepthAttachment = args.pDepthInfo
		};
		vkCmdBeginRendering( _cmdBuff, &renderInfo );
		vkCmdSetScissor( cmdBuff, 0, 1, &args.scissor );
	}
	inline ~vk_rendering_cmd()
	{
		vkCmdEndRendering( _cmdBuff );
	}
};
inline void VkBindGraphicsPSO( VkCommandBuffer cmdBuff, const render_command& args )
{
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, args.pso );
	vkCmdPushConstants( cmdBuff, args.layout, VK_SHADER_STAGE_ALL, 0, std::size( args.pushConst ), std::data( args.pushConst ) );
}

void vk_render_command::Draw( const render_command& args, u32 firstVertex, u32 vertexCount )
{
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	VkBindGraphicsPSO( this->hndl, args );
	vkCmdDraw( this->hndl, vertexCount, 1, firstVertex, 0 );
}
void vk_render_command::DrawIndirectCount( 
	const render_command& args,
	const vk_buffer& drawCmds, 
	const vk_buffer& drawCount, 
	u32 maxDrawCount 
) {
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	VkBindGraphicsPSO( this->hndl, args );
	u32 maxDrawCnt = drawCmds.size / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		this->hndl, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_indirect ) );
}
void vk_render_command::DrawIndexed( const render_command& args, const vk_buffer& indexBuffer )
{
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	VkBindGraphicsPSO( this->hndl, args );
	vkCmdBindIndexBuffer( this->hndl, indexBuffer.hndl, 0, VK_INDEX_TYPE_UINT16 );
	vkCmdDrawIndexed( this->hndl, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
}
void vk_render_command::DrawIndexedIndirectCount(
	const render_command& args, 
	const vk_buffer& indexBuffer, 
	const vk_buffer& drawCmds,
	const vk_buffer& drawCount,
	u32 maxDrawCount
) {
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	VkBindGraphicsPSO( this->hndl, args );
	vkCmdBindIndexBuffer( this->hndl, indexBuffer.hndl, 0, VK_INDEX_TYPE_UINT32 );
	vkCmdDrawIndexedIndirectCount(
		this->hndl, drawCmds.hndl, offsetof( draw_command, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );
}
