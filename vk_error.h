#ifndef __VK_ERROR_H__
#define __VK_ERROR_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include <string_view>

#include "ht_error.h"

#include "sys_os_api.h"

// TODO: gen from VkResult ?
inline std::string_view VkResErrorString( VkResult errorCode )
{
	switch( errorCode )			{
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
	static_assert( sizeof( vkHandle ) == sizeof( u64 ) );
	VkDebugUtilsObjectNameInfoEXT nameInfo = { 
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
		.objectType = VkGetObjTypeFromHandle<VKH>(),
		.objectHandle = ( u64 ) vkHandle,
		.pObjectName = name
	};

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
}


#endif // !__VK_ERROR_H__
