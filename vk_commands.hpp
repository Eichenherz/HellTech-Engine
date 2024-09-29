#ifndef __VK_COMMANDS__
#define __VK_COMMANDS__

#include "vk_common.hpp"
#include "vk_resources.hpp"
#include <span>

// TODO: strong type per queue type ?
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
};

struct vk_transfer_command : vk_command_buffer
{
	void CopyBuffer( const vk_buffer& src, const vk_buffer& dest );
	void CopyBufferToImage( const vk_buffer& src, const vk_image& dest );
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

struct vk_compute_command : vk_command_buffer
{
	void Dispatch( const dispatch_command& args );
	void DispatchIndirect( const dispatch_command& args, const vk_buffer& dispatchCmdsBuff );
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

struct vk_render_command : vk_command_buffer
{
	// TODO: instanced ?
	void Draw( const render_command& args, u32 firstVertex, u32 vertexCount ); 
	void DrawIndirectCount( const render_command& args, const vk_buffer& drawCmds, const vk_buffer& drawCount, u32 maxDrawCount );
	void DrawIndexed( const render_command& args, const vk_buffer& indexBuffer );
	void DrawIndexedIndirectCount( 
		const render_command& args, 
		const vk_buffer& indexBuffer, 
		const vk_buffer& drawCmds, 
		const vk_buffer& drawCount, 
		u32 maxDrawCount );
};

#endif