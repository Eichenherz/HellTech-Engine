#pragma once

#ifndef __VK_UTILS__
#define __VK_UTILS__

#include "vk_common.hpp"
#include "sys_os_api.h"

#include <assert.h>
#include <string_view>

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;


constexpr bool objectNaming = 1;


// TODO: gen from VkResult ?
inline std::string_view VkResErrorString( VkResult errorCode )
{
	switch( errorCode )
	{
#define STR(r) case VK_ ##r: return #r
		STR( NOT_READY );
		STR( TIMEOUT );
		STR( EVENT_SET );
		STR( EVENT_RESET );
		STR( INCOMPLETE );
		STR( ERROR_OUT_OF_HOST_MEMORY );
		STR( ERROR_OUT_OF_DEVICE_MEMORY );
		STR( ERROR_INITIALIZATION_FAILED );
		STR( ERROR_DEVICE_LOST );
		STR( ERROR_MEMORY_MAP_FAILED );
		STR( ERROR_LAYER_NOT_PRESENT );
		STR( ERROR_EXTENSION_NOT_PRESENT );
		STR( ERROR_FEATURE_NOT_PRESENT );
		STR( ERROR_INCOMPATIBLE_DRIVER );
		STR( ERROR_TOO_MANY_OBJECTS );
		STR( ERROR_FORMAT_NOT_SUPPORTED );
		STR( ERROR_FRAGMENTED_POOL );
		STR( ERROR_UNKNOWN );
		STR( ERROR_OUT_OF_POOL_MEMORY );
		STR( ERROR_INVALID_EXTERNAL_HANDLE );
		STR( ERROR_FRAGMENTATION );
		STR( ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS );
		STR( ERROR_SURFACE_LOST_KHR );
		STR( ERROR_NATIVE_WINDOW_IN_USE_KHR );
		STR( SUBOPTIMAL_KHR );
		STR( ERROR_OUT_OF_DATE_KHR );
		STR( ERROR_INCOMPATIBLE_DISPLAY_KHR );
		STR( ERROR_VALIDATION_FAILED_EXT );
		STR( ERROR_INVALID_SHADER_NV );
		STR( ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT );
		STR( ERROR_NOT_PERMITTED_EXT );
		STR( ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT );
		STR( THREAD_IDLE_KHR );
		STR( THREAD_DONE_KHR );
		STR( OPERATION_DEFERRED_KHR );
		STR( OPERATION_NOT_DEFERRED_KHR );
		STR( PIPELINE_COMPILE_REQUIRED_EXT );
		STR( RESULT_MAX_ENUM );
#undef STR
	default: return "VK_UNKNOWN_INTERNAL_ERROR";
	}
}
inline VkResult VkResFromStatement( bool statement )
{
	return !statement ? VK_SUCCESS : VkResult( int( 0x8FFFFFFF ) );
}
// TODO: keep ?
#define VK_INTERNAL_ERROR( vk ) VkResFromStatement( bool( vk ) )

#define VK_CHECK( vk )																\
do{																					\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";				\
	VkResult res = vk;																\
	if( res ){																		\
		char dbgStr[256] = {};														\
		strcat_s( dbgStr, sizeof( dbgStr ), DEV_ERR_STR );							\
		strcat_s( dbgStr, sizeof( dbgStr ), std::data( VkResErrorString( res ) ) );	\
		SysErrMsgBox( dbgStr );														\
		abort();																	\
	}																				\
}while( 0 )		

template<typename VKH>
constexpr VkObjectType VkGetObjTypeFromHandle()
{
#define if_same_type( VKT ) if( std::is_same<VKH, VKT>::value )

	if_same_type( VkInstance ) return VK_OBJECT_TYPE_INSTANCE;
	if_same_type( VkPhysicalDevice ) return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
	if_same_type( VkDevice ) return VK_OBJECT_TYPE_DEVICE;
	if_same_type( VkSemaphore ) return VK_OBJECT_TYPE_SEMAPHORE;
	if_same_type( VkCommandBuffer ) return VK_OBJECT_TYPE_COMMAND_BUFFER;
	if_same_type( VkFence ) return VK_OBJECT_TYPE_FENCE;
	if_same_type( VkDeviceMemory ) return VK_OBJECT_TYPE_DEVICE_MEMORY;
	if_same_type( VkBuffer ) return VK_OBJECT_TYPE_BUFFER;
	if_same_type( VkImage ) return VK_OBJECT_TYPE_IMAGE;
	if_same_type( VkEvent ) return VK_OBJECT_TYPE_EVENT;
	if_same_type( VkQueryPool ) return VK_OBJECT_TYPE_QUERY_POOL;
	if_same_type( VkBufferView ) return VK_OBJECT_TYPE_BUFFER_VIEW;
	if_same_type( VkImageView ) return VK_OBJECT_TYPE_IMAGE_VIEW;
	if_same_type( VkShaderModule ) return VK_OBJECT_TYPE_SHADER_MODULE;
	if_same_type( VkPipelineCache ) return VK_OBJECT_TYPE_PIPELINE_CACHE;
	if_same_type( VkPipelineLayout ) return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
	if_same_type( VkRenderPass ) return VK_OBJECT_TYPE_RENDER_PASS;
	if_same_type( VkPipeline ) return VK_OBJECT_TYPE_PIPELINE;
	if_same_type( VkDescriptorSetLayout ) return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
	if_same_type( VkSampler ) return VK_OBJECT_TYPE_SAMPLER;
	if_same_type( VkDescriptorPool ) return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
	if_same_type( VkDescriptorSet ) return VK_OBJECT_TYPE_DESCRIPTOR_SET;
	if_same_type( VkFramebuffer ) return VK_OBJECT_TYPE_FRAMEBUFFER;
	if_same_type( VkCommandPool ) return VK_OBJECT_TYPE_COMMAND_POOL;
	if_same_type( VkSamplerYcbcrConversion ) return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
	if_same_type( VkDescriptorUpdateTemplate ) return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if_same_type( VkSwapchainKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SWAPCHAIN_KHR;

	assert( 0 );
	return VK_OBJECT_TYPE_UNKNOWN;

#undef if_same_type
}

template<typename VKH>
inline void VkDbgNameObj( VKH vkHandle, VkDevice vkDevice, const char* name )
{
	if constexpr( !objectNaming ) return;

	static_assert( sizeof( vkHandle ) == sizeof( u64 ) );
	VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	nameInfo.objectType = VkGetObjTypeFromHandle<VKH>();
	nameInfo.objectHandle = ( u64 ) vkHandle;
	nameInfo.pObjectName = name;

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
}

// TODO: remove ?
static const DXPacked::XMCOLOR white = { 255u, 255u, 255u, 1 };
static const DXPacked::XMCOLOR black = { 0u, 0u, 0u, 1 };
static const DXPacked::XMCOLOR gray = { 0x80u, 0x80u, 0x80u, 1 };
static const DXPacked::XMCOLOR lightGray = { 0xD3u, 0xD3u, 0xD3u, 1 };
static const DXPacked::XMCOLOR red = { 255u, 0u, 0u, 1 };
static const DXPacked::XMCOLOR green = { 0u, 255u, 0u, 1 };
static const DXPacked::XMCOLOR blue = { 0u, 0u, 255u, 1 };
static const DXPacked::XMCOLOR yellow = { 255u, 255u, 0u, 1 };
static const DXPacked::XMCOLOR cyan = { 0u, 255u, 255u, 1 };
static const DXPacked::XMCOLOR magenta = { 255u, 0u, 255u, 1 };


struct vk_label
{
	const VkCommandBuffer& cmdBuff;

	inline vk_label( const VkCommandBuffer& cmdBuff, const char* labelName, DXPacked::XMCOLOR col )
		: cmdBuff{ cmdBuff }
	{
		assert( cmdBuff );

		VkDebugUtilsLabelEXT dbgLabel = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		dbgLabel.pLabelName = labelName;
		dbgLabel.color[ 0 ] = col.r;
		dbgLabel.color[ 1 ] = col.g;
		dbgLabel.color[ 2 ] = col.b;
		dbgLabel.color[ 3 ] = col.a;
		vkCmdBeginDebugUtilsLabelEXT( cmdBuff, &dbgLabel );
	}
	inline ~vk_label()
	{
		assert( cmdBuff );
		vkCmdEndDebugUtilsLabelEXT( cmdBuff );
	}
};

#ifdef _VK_DEBUG_

#include <iostream>

VKAPI_ATTR VkBool32 VKAPI_CALL
VkDbgUtilsMsgCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT		msgSeverity,
	VkDebugUtilsMessageTypeFlagsEXT				msgType,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData
) {
	// NOTE: validation layer bug
	if( callbackData->messageIdNumber == 0xe8616bf2 ) return VK_FALSE;


	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT )
	{
		std::string_view msgView = { callbackData->pMessage };
		std::cout << msgView.substr( msgView.rfind( "| " ) + 2 ) << "\n";

		return VK_FALSE;
	}

	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
	{
		std::cout << ">>> VK_WARNING <<<\n";
	}
	else if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
	{
		std::cout << ">>> VK_ERROR <<<\n";
	}
	std::cout << callbackData->pMessageIdName << "\n" << callbackData->pMessage << "\n" << "\n";

	return VK_FALSE;
}

#endif

// NOTE: for timestamps we need 2 queries 
struct vk_gpu_timer
{
	vk_buffer resultBuff;
	VkQueryPool queryPool;
	u32         queryCount;
	float       timestampPeriod;
};

inline VkQueryPool VkMakeQueryPool( VkDevice vkDevice, u32 queryCount, const char* name )
{
	VkQueryPoolCreateInfo queryPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = queryCount,
		//.pipelineStatistics
	};

	VkQueryPool queryPool = {};
	VK_CHECK( vkCreateQueryPool( vkDevice, &queryPoolInfo, 0, &queryPool ) );
	VkDbgNameObj( queryPool, vkDevice, name );
	return queryPool;
}

inline float VkCmdReadGpuTimeInMs( VkCommandBuffer cmdBuff, const vk_gpu_timer& vkTimer )
{
	vkCmdCopyQueryPoolResults(
		cmdBuff, vkTimer.queryPool, 0, vkTimer.queryCount, vkTimer.resultBuff.hndl, 0, sizeof( u64 ),
		VK_QUERY_RESULT_64_BIT );// | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );

	VkMemoryBarrier2 readTimestampsBarrier[] = {
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_HOST_READ_BIT, VK_PIPELINE_STAGE_2_HOST_BIT )
	};

	VkCmdPipelineFlushCacheBarriers( cmdBuff, readTimestampsBarrier );

	const u64* pTimestamps = ( const u64* ) vkTimer.resultBuff.hostVisible;
	u64 timestampBeg = pTimestamps[ 0 ];
	u64 timestampEnd = pTimestamps[ 1 ];

	constexpr float NS_TO_MS = 1e-6f;
	return ( timestampEnd - timestampBeg ) / vkTimer.timestampPeriod * NS_TO_MS;
}

struct vk_time_section
{
	const VkCommandBuffer& cmdBuff;
	const VkQueryPool& queryPool;
	const u32 queryIdx;

	inline vk_time_section( const VkCommandBuffer& _cmdBuff, const VkQueryPool& _queryPool, u32 _queryIdx )
		: cmdBuff{ _cmdBuff }, queryPool{ _queryPool }, queryIdx{ _queryIdx }
	{
		vkCmdWriteTimestamp2( cmdBuff, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, queryPool, queryIdx );
	}

	inline ~vk_time_section()
	{
		// VK_PIPELINE_STAGE_2_NONE_KHR
		vkCmdWriteTimestamp2( cmdBuff, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, queryPool, queryIdx + 1 );
	}
};

inline VkDeviceAddress VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
	VkBufferDeviceAddressInfo deviceAddrInfo = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
		.buffer = hndl
	};
	VkDeviceAddress bda = vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
	VK_CHECK( VK_INTERNAL_ERROR( !bda ) );
	return bda;
}

inline VkImageView
VkMakeImgView(
	VkDevice		vkDevice,
	VkImage			vkImg,
	VkFormat		imgFormat,
	u32				mipLevel,
	u32				levelCount,
	VkImageAspectFlags aspectMask,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32				arrayLayer = 0,
	u32				layerCount = 1
) {
	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = vkImg,
		.viewType = imgViewType,
		.format = imgFormat,
		.subresourceRange = {
			.aspectMask = aspectMask,
			.baseMipLevel = mipLevel,
			.levelCount = levelCount,
			.baseArrayLayer = arrayLayer,
			.layerCount = layerCount,
	}
	};
	VkImageView view;
	VK_CHECK( vkCreateImageView( vkDevice, &viewInfo, 0, &view ) );

	return view;
}

#endif // !__VK_UTILS__