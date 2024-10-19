#ifndef __VK_BACKEND__
#define __VK_BACKEND__

#include "vk_common.hpp"
#include "vk_device.hpp"
#include "vk_frame_graph.hpp"

#include <System/sys_platform.hpp>

struct vk_renderer_config
{
	static constexpr VkFormat		desiredHiZFormat = VK_FORMAT_R32_SFLOAT;
	static constexpr VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	static constexpr VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	static constexpr VkFormat		desiredSwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	static constexpr u8             maxAllowedFramesInFlight = 2;
	static constexpr u8             maxSwapchianImages = 3;
	static constexpr u8             perFrameQueryCount = 2;

	u16             renderWidth;
	u16             rednerHeight;
	u8              framesInFlight = 2;
};

struct gpu_readback
{
	float timeMs;
};

struct virtual_frame;

struct vk_backend
{
	static constexpr vk_renderer_config config = {};

	struct virtual_frame
	{
		deleter_unique_ptr<vk_buffer>		frameData;
		vk_gpu_timer frameTimer;
		// TODO: add more typed cmd buffers as needed ?
		//vk_command_buffer<vk_queue_type::GRAPHICS> cmdBuff;
		VkCommandBuffer cmdBuff;
		VkSemaphore		acquireSwapchainImgSema;
		VkSemaphore		queueSubmittedSema;

		desc_index frameDataIdx;

		//const vk_command_buffer<vk_queue_type::GRAPHICS>& GetCmdBuffer() const
		//{
		//	VK_CHECK( vkResetCommandBuffer( cmdBuff.hndl, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT ) );
		//	cmdBuff.Begin();
		//	return cmdBuff;
		//}
	};


	vk_device pDevice;
	frame_graph frameGraph;

	virtual_frame	vrtFrames[ config.framesInFlight ];

	u64 dllHandle;
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMsg;
	VkSurfaceKHR surface;
	VkSemaphore     timelineSema;
	u64				vFrameIdx;

	const virtual_frame& GetNextFrame( u64 currentFrameIdx ) const
	{
		u64 vrtFrameIdx = currentFrameIdx % std::size( vrtFrames );
		return vrtFrames[ vrtFrameIdx ];
	}

	explicit vk_backend( const sys_window* pWnd );
	void Terminate();

	//const gpu_readback& GetGpuReadback() const;
	void InitResources();
	void HostFrames( const frame_data& frameData );
};

#endif // !__VK_BACKEND__