#pragma once 

#ifndef __VK_ERROR_H__
#define __VK_ERROR_H__

#include "DEFS_WIN32_NO_BS.h"
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <vulkan.h>

#include <Volk/volk.h>

#include <cstdlib>
#include <format>
#include <string>
#include <type_traits>
#include <iostream>
#include <string_view>

#include "ht_error.h"

#include "core_types.h"

// TODO: gen from VkResult ?
inline std::string_view VkResErrorString( VkResult errorCode )
{
	switch( errorCode )			{
#define VK_RES_STR(r) case VK_ ##r: return #r
		VK_RES_STR( NOT_READY );
		VK_RES_STR( TIMEOUT );
		VK_RES_STR( EVENT_SET );
		VK_RES_STR( EVENT_RESET );
		VK_RES_STR( INCOMPLETE );
		VK_RES_STR( ERROR_OUT_OF_HOST_MEMORY );
		VK_RES_STR( ERROR_OUT_OF_DEVICE_MEMORY );
		VK_RES_STR( ERROR_INITIALIZATION_FAILED );
		VK_RES_STR( ERROR_DEVICE_LOST );
		VK_RES_STR( ERROR_MEMORY_MAP_FAILED );
		VK_RES_STR( ERROR_LAYER_NOT_PRESENT );
		VK_RES_STR( ERROR_EXTENSION_NOT_PRESENT );
		VK_RES_STR( ERROR_FEATURE_NOT_PRESENT );
		VK_RES_STR( ERROR_INCOMPATIBLE_DRIVER );
		VK_RES_STR( ERROR_TOO_MANY_OBJECTS );
		VK_RES_STR( ERROR_FORMAT_NOT_SUPPORTED );
		VK_RES_STR( ERROR_FRAGMENTED_POOL );
		VK_RES_STR( ERROR_UNKNOWN );
		VK_RES_STR( ERROR_OUT_OF_POOL_MEMORY );
		VK_RES_STR( ERROR_INVALID_EXTERNAL_HANDLE );
		VK_RES_STR( ERROR_FRAGMENTATION );
		VK_RES_STR( ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS );
		VK_RES_STR( ERROR_SURFACE_LOST_KHR );
		VK_RES_STR( ERROR_NATIVE_WINDOW_IN_USE_KHR );
		VK_RES_STR( SUBOPTIMAL_KHR );
		VK_RES_STR( ERROR_OUT_OF_DATE_KHR );
		VK_RES_STR( ERROR_INCOMPATIBLE_DISPLAY_KHR );
		VK_RES_STR( ERROR_VALIDATION_FAILED_EXT );
		VK_RES_STR( ERROR_INVALID_SHADER_NV );
		VK_RES_STR( ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT );
		VK_RES_STR( ERROR_NOT_PERMITTED_EXT );
		VK_RES_STR( ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT );
		VK_RES_STR( THREAD_IDLE_KHR );
		VK_RES_STR( THREAD_DONE_KHR );
		VK_RES_STR( OPERATION_DEFERRED_KHR );
		VK_RES_STR( OPERATION_NOT_DEFERRED_KHR );
		VK_RES_STR( PIPELINE_COMPILE_REQUIRED_EXT );
		VK_RES_STR( RESULT_MAX_ENUM );
#undef STR
	default: return "VK_UNKNOWN_INTERNAL_ERROR";
	}
}

#define VK_CHECK( vk )																	\
do{																						\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR;							\
	VkResult res = vk;																	\
	if( res ) HtPrintErrAndDie( "{} \nERR: {}", DEV_ERR_STR, VkResErrorString( res ) );	\
}while( 0 )	

template<typename VKH>
constexpr VkObjectType VkGetObjTypeFromHandle()
{
	if( std::is_same<VKH, VkInstance>::value ) return VK_OBJECT_TYPE_INSTANCE;
	if( std::is_same<VKH, VkPhysicalDevice>::value ) return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
	if( std::is_same<VKH, VkDevice>::value ) return VK_OBJECT_TYPE_DEVICE;
	if( std::is_same<VKH, VkSemaphore>::value ) return VK_OBJECT_TYPE_SEMAPHORE;
	if( std::is_same<VKH, VkCommandBuffer>::value ) return VK_OBJECT_TYPE_COMMAND_BUFFER;
	if( std::is_same<VKH, VkFence>::value ) return VK_OBJECT_TYPE_FENCE;
	if( std::is_same<VKH, VkDeviceMemory>::value ) return VK_OBJECT_TYPE_DEVICE_MEMORY;
	if( std::is_same<VKH, VkBuffer>::value ) return VK_OBJECT_TYPE_BUFFER;
	if( std::is_same<VKH, VkImage>::value ) return VK_OBJECT_TYPE_IMAGE;
	if( std::is_same<VKH, VkEvent>::value ) return VK_OBJECT_TYPE_EVENT;
	if( std::is_same<VKH, VkQueryPool>::value ) return VK_OBJECT_TYPE_QUERY_POOL;
	if( std::is_same<VKH, VkBufferView>::value ) return VK_OBJECT_TYPE_BUFFER_VIEW;
	if( std::is_same<VKH, VkImageView>::value ) return VK_OBJECT_TYPE_IMAGE_VIEW;
	if( std::is_same<VKH, VkShaderModule>::value ) return VK_OBJECT_TYPE_SHADER_MODULE;
	if( std::is_same<VKH, VkPipelineCache>::value ) return VK_OBJECT_TYPE_PIPELINE_CACHE;
	if( std::is_same<VKH, VkPipelineLayout>::value ) return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
	if( std::is_same<VKH, VkRenderPass>::value ) return VK_OBJECT_TYPE_RENDER_PASS;
	if( std::is_same<VKH, VkPipeline>::value ) return VK_OBJECT_TYPE_PIPELINE;
	if( std::is_same<VKH, VkDescriptorSetLayout>::value ) return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
	if( std::is_same<VKH, VkSampler>::value ) return VK_OBJECT_TYPE_SAMPLER;
	if( std::is_same<VKH, VkDescriptorPool>::value ) return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
	if( std::is_same<VKH, VkDescriptorSet>::value ) return VK_OBJECT_TYPE_DESCRIPTOR_SET;
	if( std::is_same<VKH, VkFramebuffer>::value ) return VK_OBJECT_TYPE_FRAMEBUFFER;
	if( std::is_same<VKH, VkCommandPool>::value ) return VK_OBJECT_TYPE_COMMAND_POOL;
	if( std::is_same<VKH, VkSamplerYcbcrConversion>::value ) return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
	if( std::is_same<VKH, VkDescriptorUpdateTemplate>::value ) return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
	if( std::is_same<VKH, VkSurfaceKHR>::value ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if( std::is_same<VKH, VkSwapchainKHR>::value ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if( std::is_same<VKH, VkSurfaceKHR>::value ) return VK_OBJECT_TYPE_SWAPCHAIN_KHR;

	HT_ASSERT( 0 );
	return VK_OBJECT_TYPE_UNKNOWN;
}

template<typename VKH>
inline void VkDbgNameObj( VKH vkHandle, VkDevice vkDevice, const char* name )
{
	static_assert( sizeof( vkHandle ) == sizeof( u64 ) );
	VkDebugUtilsObjectNameInfoEXT nameInfo = { 
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = VkGetObjTypeFromHandle<VKH>(),
		.objectHandle = ( u64 ) vkHandle,
		.pObjectName = name
	};

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
}

inline VKAPI_ATTR VkBool32 VKAPI_CALL
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

	std::string formattedMsg = std::format( "{}\n{}\n", callbackData->pMessageIdName, callbackData->pMessage );
	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
	{
		std::cout << ">>> VK_WARNING <<<\n" << formattedMsg << "\n";
	}
	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
	{
		//const char* pVkObjName = callbackData->pObjects ? callbackData->pObjects[ 0 ].pObjectName : "";
		std::cout << ">>> VK_ERROR <<<\n" << formattedMsg << "\n";// << pVkObjName << "\n";
		std::abort();
	} 

	return VK_FALSE;
}

#endif // !__VK_ERROR_H__
