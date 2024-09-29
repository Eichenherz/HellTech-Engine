#ifndef __VK_COMMANDS__
#define __VK_COMMANDS__

#include "vk_common.hpp"
#include "vk_resources.hpp"
#include <span>
#include "vk_utils.hpp"
#include "r_data_structs.h"
#include "vk_descriptors.hpp"

enum class vk_queue_type
{
	GRAPHICS,
	COMPUTE,
	TRANSFER
};

// TODO: don't forget to bind descriptor sets

// TODO: pass push consts differenly
// TODO: pass pipelineLayout differenly ?
struct dispatch_command
{
	std::span<u8> pushConst;
	VkPipeline pso;
	VkPipelineLayout layout;
	u16 workgrX;
	u16 workgrY;
	u16 workgrZ;
};

struct render_command
{
	std::span<u8> pushConst;
	VkRect2D scissor;
	const VkRenderingAttachmentInfoKHR* pColInfo;
	const VkRenderingAttachmentInfoKHR* pDepthInfo;
	VkPipeline pso;
	VkPipelineLayout layout;
};

template<vk_queue_type QUEUE_TYPE>
struct vk_command_buffer
{
	VkCommandBuffer hndl;
	void Begin();
	void End();
	// TODO: Execution barriers too ?
	// NOTE: on PC we don't care about BufferBarriers
	void FlushMemoryBarriers( const std::span<VkMemoryBarrier2> memBarriers );
	void ImageLayoutTransitionBarriers( const std::span<VkImageMemoryBarrier2> imgBarriers );
	void FillBuffer( vk_buffer& buff, u32 val );
	void CopyBuffer( const vk_buffer& src, const vk_buffer& dest );
	void CopyBufferToImage( const vk_buffer& src, const vk_image& dest );

	// TODO: Concepts ?
	template<typename Args_T>
	void BindPSO( const Args_T& args ) 
		requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) );
	void BindDescriptorSet( const vk_descriptor_manager& descMngr )
		requires( (QUEUE_TYPE == vk_queue_type::GRAPHICS) || (QUEUE_TYPE == vk_queue_type::COMPUTE) );

	void Dispatch( const dispatch_command& args )
		requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) );
	void DispatchIndirect( const dispatch_command& args, const vk_buffer& dispatchCmdsBuff )
		requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) );


	// TODO: instanced ?
	void Draw( const render_command& args, u32 firstVertex, u32 vertexCount ) requires( QUEUE_TYPE == vk_queue_type::GRAPHICS );
	void DrawIndirectCount( 
		const render_command& args, 
		const vk_buffer& drawCmds, 
		const vk_buffer& drawCount, 
		u32 maxDrawCount ) requires( QUEUE_TYPE == vk_queue_type::GRAPHICS );
	void DrawIndexed( const render_command& args, const vk_buffer& indexBuffer ) requires( QUEUE_TYPE == vk_queue_type::GRAPHICS );
	void DrawIndexedIndirectCount( 
		const render_command& args, 
		const vk_buffer& indexBuffer, 
		const vk_buffer& drawCmds, 
		const vk_buffer& drawCount, 
		u32 maxDrawCount ) requires( QUEUE_TYPE == vk_queue_type::GRAPHICS );
};


template<vk_queue_type QUEUE_TYPE>
inline constexpr VkPipelineBindPoint VkGetPSOBindPoint()
{
	VkPipelineBindPoint bindPoint = VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_MAX_ENUM;
	if constexpr( QUEUE_TYPE == vk_queue_type::COMPUTE )
	{
		bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
	}
	else if constexpr( QUEUE_TYPE == vk_queue_type::GRAPHICS )
	{
		bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	}
	return bindPoint;
}

template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::Begin()
{
	VkCommandBufferBeginInfo cmdBufBegInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT 
	};
	vkBeginCommandBuffer( this->hndl, &cmdBufBegInfo );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::End()
{
	VK_CHECK( vkEndCommandBuffer( this->hndl ) );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::FlushMemoryBarriers( const std::span<VkMemoryBarrier2> memBarriers )
{
	VkDependencyInfo dependency = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.memoryBarrierCount = ( u32 ) std::size( memBarriers ),
		.pMemoryBarriers = std::data( memBarriers ),
	};
	vkCmdPipelineBarrier2( this->hndl, &dependency );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::ImageLayoutTransitionBarriers(const std::span<VkImageMemoryBarrier2> imgBarriers)
{
	VkDependencyInfo dependency = {
		.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
		.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
		.pImageMemoryBarriers = std::data( imgBarriers ),
	};
	vkCmdPipelineBarrier2( this->hndl, &dependency );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::FillBuffer( vk_buffer& buff, u32 val )
{
	vkCmdFillBuffer( this->hndl, buff.hndl, 0, buff.size, val );
}


template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::CopyBuffer( const vk_buffer& src, const vk_buffer& dest )
{
	VkBufferCopy copyRegion = { 0,0,src.size };
	vkCmdCopyBuffer( this->hndl, src.hndl, dest.hndl, 1, &copyRegion );
}
// TODO: do we need more stuff here
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::CopyBufferToImage( const vk_buffer& src, const vk_image& dest )
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


template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::BindDescriptorSet( const vk_descriptor_manager& descMngr )
	requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
{
	constexpr VkPipelineBindPoint bindPoint = VkGetPSOBindPoint();
	vkCmdBindDescriptorSets( this->hndl, bindPoint, descMngr.globalPipelineLayout, 0, 1, &descMngr.set, 0, 0 );
}
template<vk_queue_type QUEUE_TYPE>
template<typename Args_T>
inline void vk_command_buffer<QUEUE_TYPE>::BindPSO( const Args_T& args )
	requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
{
	constexpr VkPipelineBindPoint bindPoint = VkGetPSOBindPoint();
	vkCmdBindPipeline( cmdBuff, bindPoint, args.pso );
	vkCmdPushConstants( cmdBuff, args.layout, bindPoint, 0, std::size( args.pushConst ), std::data( args.pushConst ) );
}


template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::Dispatch( const dispatch_command& args )
	requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
{
	this->BindPSO( args );
	vkCmdDispatch( this->hndl, args.workgrX, args.workgrY, args.workgrZ );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::DispatchIndirect( const dispatch_command& args, const vk_buffer& dispatchCmdsBuff )
	requires( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
{
	this->BindPSO( args );
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

template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::Draw( const render_command& args, u32 firstVertex, u32 vertexCount ) 
	requires( QUEUE_TYPE == vk_queue_type::GRAPHICS )
{
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	this->BindPSO( args );
	vkCmdDraw( this->hndl, vertexCount, 1, firstVertex, 0 );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::DrawIndirectCount( 
	const render_command& args,
	const vk_buffer& drawCmds, 
	const vk_buffer& drawCount, 
	u32 maxDrawCount 
) requires( QUEUE_TYPE == vk_queue_type::GRAPHICS ) {
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	this->BindPSO( args );
	u32 maxDrawCnt = drawCmds.size / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		this->hndl, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_indirect ) );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::DrawIndexed( const render_command& args, const vk_buffer& indexBuffer )
	requires( QUEUE_TYPE == vk_queue_type::GRAPHICS )
{
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	this->BindPSO( args );
	vkCmdBindIndexBuffer( this->hndl, indexBuffer.hndl, 0, VK_INDEX_TYPE_UINT16 );
	vkCmdDrawIndexed( this->hndl, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
}
template<vk_queue_type QUEUE_TYPE>
inline void vk_command_buffer<QUEUE_TYPE>::DrawIndexedIndirectCount(
	const render_command& args, 
	const vk_buffer& indexBuffer, 
	const vk_buffer& drawCmds,
	const vk_buffer& drawCount,
	u32 maxDrawCount
) requires( QUEUE_TYPE == vk_queue_type::GRAPHICS ) {
	vk_rendering_cmd beginRenderScope{ this->hndl, args };
	this->BindPSO( args );
	vkCmdBindIndexBuffer( this->hndl, indexBuffer.hndl, 0, VK_INDEX_TYPE_UINT32 );
	vkCmdDrawIndexedIndirectCount(
		this->hndl, drawCmds.hndl, offsetof( draw_command, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );
}

#endif
