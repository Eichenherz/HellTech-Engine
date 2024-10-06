#ifndef __VK_QUEUE__
#define __VK_QUEUE__

#include "vk_common.hpp"
#include <span>
#include <ranges>
#include "core_types.h"
#include "vk_commands.hpp"

// TODO: multiple sumbits per queue ?
template<vk_queue_type QUEUE_TYPE>
struct vk_queue
{
	VkQueue			hndl;
	VkCommandPool   cmdPool;
	u32				index;

	void Submit( std::initializer_list<vk_command_buffer<QUEUE_TYPE>> cmdBuffers,
				 std::initializer_list<VkSemaphore> waitSemas,
				 std::initializer_list<VkSemaphore> signalSemas,
				 std::initializer_list<u64> signalValues,
				 VkPipelineStageFlags waitDstStageMsk );
	void Present( VkSemaphore renderingCompleteSema, const vk_swapchain& swapchain, u32 swapchainImgIdx )
		requires ( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) );
};


template<vk_queue_type QUEUE_TYPE>
void vk_queue<QUEUE_TYPE>::Submit( 
	std::initializer_list<vk_command_buffer<QUEUE_TYPE>> cmdBuffers,
	std::initializer_list<VkSemaphore> waitSemas,
	std::initializer_list<VkSemaphore> signalSemas,
	std::initializer_list<u64> signalValues,
	VkPipelineStageFlags waitDstStageMsk 
) {
	std::vector<VkCommandBuffer> vkCmdBuffs;
	vkCmdBuffs.reserve( std::size( cmdBuffers ) );

	for( auto& cmdBuff : cmdBuffers )
	{
		cmdBuff.End();
		vkCmdBuffs.push_back( cmdBuff.hndl );
	}

	VkTimelineSemaphoreSubmitInfo timelineInfo = {
		.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.signalSemaphoreValueCount = std::size( signalValues ),
		.pSignalSemaphoreValues = std::data( signalValues )
	};

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = &timelineInfo,
		.waitSemaphoreCount = ( u32 ) std::size( waitSemas ),
		.pWaitSemaphores = std::data( waitSemas ),
		.pWaitDstStageMask = &waitDstStageMsk,
		.commandBufferCount = ( u32 ) std::size( vkCmdBuffs ),
		.pCommandBuffers = std::data( vkCmdBuffs ),
		.signalSemaphoreCount = ( u32 ) std::size( signalSemas ),
		.pSignalSemaphores = std::data( signalSemas )
	};
	// NOTE: queue submit has implicit host sync for trivial stuff
	VK_CHECK( vkQueueSubmit( this->hndl, 1, &submitInfo, 0 ) );
}

template<vk_queue_type QUEUE_TYPE>
void vk_queue<QUEUE_TYPE>::Present( VkSemaphore renderingCompleteSema, const vk_swapchain& swapchain, u32 swapchainImgIdx )
	requires ( ( QUEUE_TYPE == vk_queue_type::GRAPHICS ) || ( QUEUE_TYPE == vk_queue_type::COMPUTE ) )
{
	VkPresentInfoKHR presentInfo = { 
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &renderingCompleteSema,
		.swapchainCount = 1,
		.pSwapchains = &swapchain,
		.pImageIndices = &swapchainImgIdx
	};
	VK_CHECK( vkQueuePresentKHR( this->hndl, &presentInfo ) );
}

#endif // !__VK_QUEUE__
