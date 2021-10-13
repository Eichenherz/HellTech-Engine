#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>
#include <vulkan_win32.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string_view>
#include <charconv>
#include <span>

// TODO: use own allocator
// TODO: precompiled header
#include "diy_pch.h"

// NOTE: clang-cl on VS issue
#ifdef __clang__
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;

#include "sys_os_api.h"
#include "core_lib_api.h"

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

//====================CONSTS====================//
constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 256 * MB;
constexpr u64 MAX_MIP_LEVELS = 12;
constexpr u32 NOT_USED_IDX = -1;
constexpr u32 OBJ_CULL_WORKSIZE = 64;
constexpr u32 MLET_CULL_WORKSIZE = 256;
//==============================================//
// TODO: cvars
//====================CVARS====================//
static bool colorBlending = 0;
//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//
constexpr bool multiPassDepthPyramid = 1;
static_assert( multiPassDepthPyramid );
// TODO: enable gfx debug outside of VS Debug
constexpr bool vkValidationLayerFeatures = 1;
constexpr bool worldLeftHanded = 1;
constexpr bool objectNaming = 1;
// TODO: cvar or constexpr ?
constexpr bool dbgDraw = true;
//==============================================//

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
	nameInfo.objectHandle = (u64) vkHandle;
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
){
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

constexpr VkValidationFeatureEnableEXT enabledValidationFeats[] = {
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
};

static u64 VK_DLL = 0;

// TODO: gfx_api_instance ?
struct vk_instance
{
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMsg;
};

inline static vk_instance VkMakeInstance()
{
	constexpr const char* ENABLED_INST_EXTS[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
	#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	#endif // VK_USE_PLATFORM_WIN32_KHR
	#ifdef _VK_DEBUG_
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	#endif // _VK_DEBUG_
	};

	constexpr const char* LAYERS[] =
	{
	#ifdef _VK_DEBUG_
		"VK_LAYER_KHRONOS_validation",
		//"VK_LAYER_LUNARG_api_dump"
	#endif // _VK_DEBUG_
	};

	VK_CHECK( VK_INTERNAL_ERROR( !( VK_DLL = SysDllLoad( "vulkan-1.dll" ) ) ) );

	// TODO: full vk file generation ?

	vkGetInstanceProcAddr = ( PFN_vkGetInstanceProcAddr ) SysGetProcAddr( VK_DLL, "vkGetInstanceProcAddr" );
	vkCreateInstance = ( PFN_vkCreateInstance ) vkGetInstanceProcAddr( 0, "vkCreateInstance" );
	vkEnumerateInstanceExtensionProperties =
		( PFN_vkEnumerateInstanceExtensionProperties ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceExtensionProperties" );
	vkEnumerateInstanceLayerProperties =
		( PFN_vkEnumerateInstanceLayerProperties ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceLayerProperties" );
	vkEnumerateInstanceVersion = ( PFN_vkEnumerateInstanceVersion ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceVersion" );

	u32 vkExtsNum = 0;
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, 0 ) );
	std::vector<VkExtensionProperties> givenExts( vkExtsNum );
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, std::data( givenExts ) ) );
	for( std::string_view requiredExt : ENABLED_INST_EXTS )
	{
		bool foundExt = false;
		for( VkExtensionProperties& availableExt : givenExts )
		{
			if( requiredExt == availableExt.extensionName )
			{
				foundExt = true;
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !foundExt ) );
	};

	u32 layerCount = 0;
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, 0 ) );
	std::vector<VkLayerProperties> layersAvailable( layerCount );
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, std::data( layersAvailable ) ) );
	for( std::string_view requiredLayer : LAYERS )
	{
		bool foundLayer = false;
		for( VkLayerProperties& availableLayer : layersAvailable )
		{
			if( requiredLayer == availableLayer.layerName )
			{
				foundLayer = true;
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !foundLayer ) );
	}


	VkInstance vkInstance = 0;
	VkDebugUtilsMessengerEXT vkDbgUtilsMsgExt = 0;

	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VK_CHECK( vkEnumerateInstanceVersion( &appInfo.apiVersion ) );
	VK_CHECK( VK_INTERNAL_ERROR( appInfo.apiVersion < VK_API_VERSION_1_2 ) );

	VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
#ifdef _VK_DEBUG_

	VkValidationFeaturesEXT vkValidationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	vkValidationFeatures.enabledValidationFeatureCount = std::size( enabledValidationFeats );
	vkValidationFeatures.pEnabledValidationFeatures = enabledValidationFeats;

	VkDebugUtilsMessengerCreateInfoEXT vkDbgExt = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
	vkDbgExt.pNext = vkValidationLayerFeatures ? &vkValidationFeatures : 0;
	vkDbgExt.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	vkDbgExt.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	vkDbgExt.pfnUserCallback = VkDbgUtilsMsgCallback;

	instInfo.pNext = &vkDbgExt;
#endif
	instInfo.pApplicationInfo = &appInfo;
	instInfo.enabledLayerCount = std::size( LAYERS );
	instInfo.ppEnabledLayerNames = LAYERS;
	instInfo.enabledExtensionCount = std::size( ENABLED_INST_EXTS );
	instInfo.ppEnabledExtensionNames = ENABLED_INST_EXTS;
	VK_CHECK( vkCreateInstance( &instInfo, 0, &vkInstance ) );

	VkLoadInstanceProcs( vkInstance, *vkGetInstanceProcAddr );

#ifdef _VK_DEBUG_
	VK_CHECK( vkCreateDebugUtilsMessengerEXT( vkInstance, &vkDbgExt, 0, &vkDbgUtilsMsgExt ) );
#endif

	return { vkInstance, vkDbgUtilsMsgExt };
}

// TODO: keep global ?
static VkInstance				vkInst = 0;
static VkDebugUtilsMessengerEXT	vkDbgMsg = 0;


// TODO: separate GPU and logical device ?
// TODO: physical_device_info ?
struct device
{
	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;
	VkQueue			gfxQueue;
	VkQueue			compQueue;
	VkQueue			transfQueue;
	u32				gfxQueueIdx;
	u32				compQueueIdx;
	u32				transfQueueIdx;
	float           timestampPeriod;
	u8				waveSize;
};

inline static device VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf )
{
	constexpr const char* ENABLED_DEVICE_EXTS[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,

		//VK_KHR_PRESENT_ID_EXTENSION_NAME,
		//VK_KHR_PRESENT_WAIT_EXTENSION_NAME,

		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,

		VK_KHR_ZERO_INITIALIZE_WORKGROUP_MEMORY_EXTENSION_NAME,

		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,

		VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,

		VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
		VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
		VK_EXT_INLINE_UNIFORM_BLOCK_EXTENSION_NAME
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );


	VkPhysicalDeviceInlineUniformBlockPropertiesEXT inlineBlockProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT };
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, &inlineBlockProps };
	VkPhysicalDeviceSubgroupProperties waveProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &conservativeRasterProps };
	VkPhysicalDeviceVulkan12Properties gpuProps12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, &waveProps };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps12 };

	
	VkPhysicalDeviceInlineUniformBlockFeaturesEXT inlineBlockFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES_EXT };
	VkPhysicalDeviceIndexTypeUint8FeaturesEXT uint8IdxFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT, &inlineBlockFeatures };
	VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeaturesKHR zeroInitWorkgrMemFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES_KHR, &uint8IdxFeatures };
	VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, &zeroInitWorkgrMemFeatures };
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicStateFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, &sync2Features };
	VkPhysicalDeviceVulkan12Features gpuFeatures12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &extDynamicStateFeatures };
	// NOTE: Vulkan SDK 1.2.189 complains about this stuff , wtf ?
	VkPhysicalDeviceVulkan11Features gpuFeatures11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &gpuFeatures12 };
	VkPhysicalDeviceFeatures2 gpuFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &gpuFeatures11 };

	// TODO: check for more stuff ?
	VkPhysicalDevice gpu = 0;
	for( u64 i = 0; i < numDevices; ++i )
	{
		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, 0 ) || !extsNum ) continue;
		std::vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, std::data( availableExts ) ) ) continue;

		for( std::string_view requiredExt : ENABLED_DEVICE_EXTS )
		{
			bool foundExt = false;
			for( VkExtensionProperties& availableExt : availableExts )
			{
				if( requiredExt == availableExt.extensionName )
				{
					foundExt = true;
					break;
				}
			}
			if( !foundExt ) goto NEXT_DEVICE;
		};

		vkGetPhysicalDeviceProperties2( availableDevices[ i ], &gpuProps2 );
		if( gpuProps2.properties.apiVersion < VK_API_VERSION_1_2 ) continue;
		if( gpuProps2.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) continue;
		if( !gpuProps2.properties.limits.timestampComputeAndGraphics ) continue;

		vkGetPhysicalDeviceFeatures2( availableDevices[ i ], &gpuFeatures );

		gpu = availableDevices[ i ];

		break;

	NEXT_DEVICE:;
	}
	VK_CHECK( VK_INTERNAL_ERROR( !gpu ) );

	gpuFeatures.features.geometryShader = 0;
	// NOTE: might help debugging ?
	gpuFeatures.features.robustBufferAccess = 0;


	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, 0 );
	VK_CHECK( VK_INTERNAL_ERROR( !queueFamNum ) );
	std::vector<VkQueueFamilyProperties>  queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, std::data( queueFamProps ) );

	constexpr VkQueueFlags careAboutQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	constexpr VkQueueFlags presentQueueFlags = careAboutQueueTypes ^ VK_QUEUE_TRANSFER_BIT;

	u32 qGfxIdx = -1, qCompIdx = -1, qTransfIdx = -1;

	for( u32 qIdx = 0; qIdx < queueFamNum; ++qIdx )
	{
		if( queueFamProps[ qIdx ].queueCount == 0 ) continue;

		VkQueueFlags familyFlags = queueFamProps[ qIdx ].queueFlags & careAboutQueueTypes;
		if( familyFlags & presentQueueFlags )
		{
			VkBool32 present = 0;
			vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qIdx, vkSurf, &present );
			VK_CHECK( VK_INTERNAL_ERROR( !present ) );
		}
		if( familyFlags & VK_QUEUE_GRAPHICS_BIT ) qGfxIdx = qIdx;
		else if( familyFlags & VK_QUEUE_COMPUTE_BIT ) qCompIdx = qIdx;
		else if( familyFlags & VK_QUEUE_TRANSFER_BIT ) qTransfIdx = qIdx;
	}
	
	VK_CHECK( VK_INTERNAL_ERROR( ( qGfxIdx == u32( -1 ) ) || ( qCompIdx == u32( -1 ) ) || ( qTransfIdx == u32( -1 ) ) ) );
	
	float queuePriorities = 1.0f;
	VkDeviceQueueCreateInfo queueInfos[ 3 ] = {};
	for( u32 qi = 0; qi < std::size( queueInfos ); ++qi )
	{
		queueInfos[ qi ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[ qi ].queueFamilyIndex = qi;
		queueInfos[ qi ].queueCount = 1;
		queueInfos[ qi ].pQueuePriorities = &queuePriorities;
	}

	device dc = {};

	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.pNext = &gpuFeatures;
	deviceInfo.queueCreateInfoCount = std::size( queueInfos );
	deviceInfo.pQueueCreateInfos = &queueInfos[ 0 ];
	deviceInfo.enabledExtensionCount = std::size( ENABLED_DEVICE_EXTS );
	deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS;
	VK_CHECK( vkCreateDevice( gpu, &deviceInfo, 0, &dc.device ) );

	
	VkLoadDeviceProcs( dc.device );

	vkGetDeviceQueue( dc.device, queueInfos[ qGfxIdx ].queueFamilyIndex, 0, &dc.gfxQueue );
	vkGetDeviceQueue( dc.device, queueInfos[ qCompIdx ].queueFamilyIndex, 0, &dc.compQueue );
	vkGetDeviceQueue( dc.device, queueInfos[ qTransfIdx ].queueFamilyIndex, 0, &dc.transfQueue );
	VK_CHECK( VK_INTERNAL_ERROR( !dc.gfxQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !dc.compQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !dc.transfQueue ) );

	dc.gfxQueueIdx = queueInfos[ qGfxIdx ].queueFamilyIndex;
	dc.compQueueIdx = queueInfos[ qCompIdx ].queueFamilyIndex;
	dc.transfQueueIdx = queueInfos[ qTransfIdx ].queueFamilyIndex;
	dc.gpu = gpu;
	dc.gpuProps = gpuProps2.properties;
	dc.timestampPeriod = gpuProps2.properties.limits.timestampPeriod;
	dc.waveSize = waveProps.subgroupSize;

	return dc;
}


static device dc;


// TODO: move to memory section
// TODO: pass VkPhysicalDevice differently
// TODO: multi gpu ?
// TODO: multi threaded
// TODO: better alloc strategy ?
// TODO: alloc vector for debug only ?
// TODO: check alloc num ?
// TODO: recycle memory ?
// TODO: redesign ?
// TODO: per mem-block flags
struct vk_mem_view
{
	VkDeviceMemory	device;
	void*			host = 0;
};

struct vk_allocation
{
	VkDeviceMemory  deviceMem;
	u8*				hostVisible = 0;
	u64				dataOffset;
};

struct vk_mem_arena
{
	std::vector<vk_mem_view>	mem;
	std::vector<vk_mem_view>	dedicatedAllocs;
	u64							maxParentHeapSize;
	u64							minVkAllocationSize = VK_MIN_DEVICE_BLOCK_SIZE;
	u64							size;
	u64							allocated;
	VkDevice					device;
	VkMemoryPropertyFlags		memTypeProperties;
	u32							memTypeIdx;
};

static vk_mem_arena vkRscArena, vkStagingArena, vkAlbumArena, vkHostComArena, vkDbgArena;

inline bool IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
inline u64 FwdAlign( u64 addr, u64 alignment )
{
	assert( IsPowOf2( alignment ) );
	u64 mod = addr & ( alignment - 1 );
	return mod ? addr + ( alignment - mod ) : addr;
}

// TODO: integrate into rsc creation
inline i32
VkFindMemTypeIdx(
	const VkPhysicalDeviceMemoryProperties* pVkMemProps,
	VkMemoryPropertyFlags				requiredProps,
	u32									memTypeBitsRequirement
){
	for( u64 memIdx = 0; memIdx < pVkMemProps->memoryTypeCount; ++memIdx )
	{
		u32 memTypeBits = ( 1 << memIdx );
		bool isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps->memoryTypes[ memIdx ].propertyFlags;
		bool hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps ) return (i32) memIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( "Memory type unmatch !" ) );

	return -1;
}

// TODO: move alloc flags ?
// NOTE: NV driver bug not allow HOST_VISIBLE + VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT for uniforms
inline static VkDeviceMemory
VkTryAllocDeviceMem(
	VkDevice								vkDevice,
	u64										size,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated
){
	VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	allocFlagsInfo.pNext = dedicated;
	allocFlagsInfo.flags = // allocFlags;
		//VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT |
		VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	
	VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
#if 1
	memoryAllocateInfo.pNext = ( memTypeIdx == 0xA ) ? 0 : &allocFlagsInfo;
#else
	memoryAllocateInfo.pNext = &allocFlagsInfo;
#endif
	memoryAllocateInfo.allocationSize = size;
	memoryAllocateInfo.memoryTypeIndex = memTypeIdx;

	VkDeviceMemory mem;
	VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &mem ) );

	return mem;
}

inline static vk_mem_arena
VkMakeMemoryArena(
	const VkPhysicalDeviceMemoryProperties& memProps,
	VkMemoryPropertyFlags				memType,
	VkDevice							vkDevice
){
	i32 i = 0;
	for( ; i < memProps.memoryTypeCount; ++i )
	{
		if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;
	}

	VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

	VkMemoryHeap backingHeap = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ];

	vk_mem_arena vkArena = {};
	vkArena.allocated = 0;
	vkArena.size = 0;
	vkArena.memTypeIdx = i;
	vkArena.memTypeProperties = memProps.memoryTypes[ i ].propertyFlags;
	vkArena.maxParentHeapSize = backingHeap.size;
	vkArena.minVkAllocationSize = ( backingHeap.size < VK_MIN_DEVICE_BLOCK_SIZE ) ? ( 1 * MB ) : VK_MIN_DEVICE_BLOCK_SIZE;
	vkArena.device = vkDevice;

	return vkArena;
}

// TODO: assert vs VK_CHECK vs default + warning
// TODO: must alloc in block with BUFFER_ADDR
inline vk_allocation
VkArenaAlignAlloc(
	vk_mem_arena* vkArena,
	u64										size,
	u64										align,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo* dedicated
){
	assert( size <= vkArena->maxParentHeapSize );

	u64 allocatedWithOffset = FwdAlign( vkArena->allocated, align );
	vkArena->allocated = allocatedWithOffset;

	if( dedicated )
	{
		VkDeviceMemory deviceMem = VkTryAllocDeviceMem( vkArena->device, size, memTypeIdx, allocFlags, dedicated );
		void* hostVisible = 0;
		if( vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
		{
			VK_CHECK( vkMapMemory( vkArena->device, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
		}
		vk_mem_view lastDedicated = { deviceMem,hostVisible };
		vkArena->dedicatedAllocs.push_back( lastDedicated );
		return { lastDedicated.device, (u8*) lastDedicated.host, 0 };
	}

	if( ( vkArena->allocated + size ) > vkArena->size )
	{
		u64 newArenaSize = std::max( size, vkArena->minVkAllocationSize );
		VkDeviceMemory deviceMem = VkTryAllocDeviceMem( vkArena->device, newArenaSize, memTypeIdx, allocFlags, 0 );
		void* hostVisible = 0;
		if( vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
		{
			VK_CHECK( vkMapMemory( vkArena->device, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
		}
		vkArena->mem.push_back( { deviceMem,hostVisible } );
		vkArena->size = newArenaSize;
		vkArena->allocated = 0;
	}

	assert( ( vkArena->allocated + size ) <= vkArena->size );
	assert( vkArena->allocated % align == 0 );

	vk_mem_view lastBlock = vkArena->mem[ std::size( vkArena->mem ) - 1 ];
	vk_allocation allocId = { lastBlock.device, (u8*) lastBlock.host, vkArena->allocated };
	vkArena->allocated += size;

	return allocId;
}

// TODO: revisit
inline void
VkArenaTerimate( vk_mem_arena* vkArena )
{
	for( u64 i = 0; i < std::size( vkArena->mem ); ++i )
		vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
	for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
}

// TODO: move debug stuff out of here ?
inline static void
VkStartGfxMemory( VkPhysicalDevice vkPhysicalDevice, VkDevice vkDevice )
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkPhysicalDevice, &memProps );

	vkRscArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkDevice );
	vkAlbumArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkDevice );
	vkStagingArena = 
		VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vkDevice );
	vkHostComArena = VkMakeMemoryArena( memProps,
										VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
										VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
										VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
										vkDevice );

	vkDbgArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkDevice );
}

inline u64 VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
	static_assert( std::is_same<VkDeviceAddress, u64>::value );

	VkBufferDeviceAddressInfo deviceAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	deviceAddrInfo.buffer = hndl;

	return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
}

// TODO: keep memory in buffer ?
// TODO: keep devicePointer here ?
// TODO: keep usage flags ?
// TODO: add buffer stride ?
struct buffer_data
{
	VkBuffer		hndl;
	VkDeviceMemory	mem;
	u64				size;
	//u64				offset = 0;
	u8*             hostVisible;
	u64				devicePointer;
};

inline VkDescriptorBufferInfo Descriptor( const buffer_data& b )
{
	//return VkDescriptorBufferInfo{ hndl,offset,size };
	return VkDescriptorBufferInfo{ b.hndl,0,b.size };
}

// TODO: use a texture_desc struct
// TODO: add more data ?
// TODO: VkDescriptorImageInfo 
// TODO: rsc don't directly store the memory, rsc manager refrences it ?
struct image
{
	VkImage			hndl;
	VkImageView		view;
	VkDeviceMemory	mem;
	VkFormat		nativeFormat;
	u16				width;
	u16				height;
	u8				layerCount;
	u8				mipCount;

	inline VkExtent3D Extent3D() const
	{
		return { width,height,1 };
	}
};

// TODO: pass aspect mask ? ?
inline static VkImageView
VkMakeImgView(
	VkDevice		vkDevice,
	VkImage			vkImg,
	VkFormat		imgFormat,
	u32				mipLevel,
	u32				levelCount,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32				arrayLayer = 0,
	u32				layerCount = 1
){
	VkImageAspectFlags aspectMask =
		( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.viewType = imgViewType;
	viewInfo.format = imgFormat;
	viewInfo.subresourceRange.aspectMask = aspectMask;
	viewInfo.subresourceRange.baseMipLevel = mipLevel;
	viewInfo.subresourceRange.levelCount = levelCount;
	viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
	viewInfo.subresourceRange.layerCount = layerCount;
	viewInfo.image = vkImg;

	VkImageView view;
	VK_CHECK( vkCreateImageView( vkDevice, &viewInfo, 0, &view ) );

	return view;
}

// TODO: pass device for rsc creation, and stuff
// TODO: re-think resource creation and management

// TODO: Pass BuffCreateInfo
static buffer_data
VkCreateAllocBindBuffer(
	u64					sizeInBytes,
	VkBufferUsageFlags	usage,
	vk_mem_arena& vkArena
){
	buffer_data buffData = {};

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	//bufferInfo.flags = ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? 
	//	VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT : 0;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( vkCreateBuffer( vkArena.device, &bufferInfo, 0, &buffData.hndl ) );

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
	buffMemReqs2.buffer = buffData.hndl;
	vkGetBufferMemoryRequirements2( vkArena.device, &buffMemReqs2, &memReqs2 );

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena.memTypeIdx ) ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	VkMemoryAllocateFlags allocFlags =
		( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.buffer = buffMemReqs2.buffer;

	bool dedicatedAlloc = dedicatedReqs.requiresDedicatedAllocation; //|| dedicatedReqs.prefersDedicatedAllocation;

	vk_allocation bufferMem = VkArenaAlignAlloc( &vkArena,
												 memReqs2.memoryRequirements.size,
												 memReqs2.memoryRequirements.alignment,
												 vkArena.memTypeIdx,
												 allocFlags,
												 dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	buffData.mem = bufferMem.deviceMem;
	buffData.hostVisible = ( bufferMem.hostVisible ) ? ( bufferMem.hostVisible + bufferMem.dataOffset ) : 0;
	buffData.size = sizeInBytes;

	VK_CHECK( vkBindBufferMemory( vkArena.device, buffData.hndl, buffData.mem, bufferMem.dataOffset ) );

	if( allocFlags == VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
	{
		buffData.devicePointer = VkGetBufferDeviceAddress( vkArena.device, buffData.hndl );
		assert( buffData.devicePointer );
	}

	return buffData;
}

static image
VkCreateAllocBindImage(
	const VkImageCreateInfo& imgInfo,
	vk_mem_arena& vkArena,
	VkPhysicalDevice			gpu = dc.gpu
){
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );

	image img = {};
	img.nativeFormat = imgInfo.format;
	img.width = imgInfo.extent.width;
	img.height = imgInfo.extent.height;
	img.mipCount = imgInfo.mipLevels;
	img.layerCount = imgInfo.arrayLayers;
	VK_CHECK( vkCreateImage( vkArena.device, &imgInfo, 0, &img.hndl ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.hndl;
	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkArena.device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena.memTypeIdx ) ) );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc( &vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  vkArena.memTypeIdx,
											  0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkArena.device, img.hndl, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena.device, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

// TODO: fomralize image creation even more ?
static image
VkCreateAllocBindImage(
	VkFormat			format,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			extent,
	u32					mipCount,
	vk_mem_arena&		vkArena,
	VkPhysicalDevice	gpu = dc.gpu
){
	VkFormatFeatureFlags formatFeatures = 0;
	if( usageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );


	image img = {};

	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = img.nativeFormat = format;
	img.width = extent.width;
	img.height = extent.height;
	imgInfo.extent = extent;
	imgInfo.mipLevels = img.mipCount = mipCount;
	imgInfo.arrayLayers = img.layerCount = 1;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( vkCreateImage( vkArena.device, &imgInfo, 0, &img.hndl ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.hndl;

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkArena.device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	//VK_CHECK( VK_INTERNAL_ERROR( memTypeIdx == vkArena->memTypeIdx ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc( &vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  vkArena.memTypeIdx,
											  0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkArena.device, img.hndl, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena.device, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

// TODO: differentiate barriers by usage ? like layout transition, exec dependency
// TODO: pass buff_data ?
inline VkBufferMemoryBarrier
VkMakeBufferBarrier(
	VkBuffer		hBuff,
	VkAccessFlags	srcAccess,
	VkAccessFlags	dstAccess,
	VkDeviceSize	buffOffset = 0,
	VkDeviceSize	buffSize = VK_WHOLE_SIZE,
	u32				srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32				dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED
){
	VkBufferMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	memBarrier.srcAccessMask = srcAccess;
	memBarrier.dstAccessMask = dstAccess;
	memBarrier.srcQueueFamilyIndex = srcQueueFamIdx;
	memBarrier.dstQueueFamilyIndex = dstQueueFamIdx;
	memBarrier.buffer = hBuff;
	memBarrier.offset = buffOffset;
	memBarrier.size = buffSize;

	return memBarrier;
}

inline VkImageMemoryBarrier
VkMakeImgBarrier(
	VkImage				image,
	VkAccessFlags		srcAccessMask,
	VkAccessFlags		dstAccessMask,
	VkImageLayout		oldLayout,
	VkImageLayout		newLayout,
	VkImageAspectFlags	aspectMask,
	u32					srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32					dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32					baseMipLevel = 0,
	u32					mipCount = 0,
	u32					baseArrayLayer = 0,
	u32					layerCount = 0
){
	VkImageMemoryBarrier imgMemBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	imgMemBarrier.srcAccessMask = srcAccessMask;
	imgMemBarrier.dstAccessMask = dstAccessMask;
	imgMemBarrier.oldLayout = oldLayout;
	imgMemBarrier.newLayout = newLayout;
	imgMemBarrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
	imgMemBarrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	imgMemBarrier.image = image;
	imgMemBarrier.subresourceRange.aspectMask = aspectMask;
	imgMemBarrier.subresourceRange.baseMipLevel = baseMipLevel;
	imgMemBarrier.subresourceRange.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS;
	imgMemBarrier.subresourceRange.baseArrayLayer = baseArrayLayer;
	imgMemBarrier.subresourceRange.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS;

	return imgMemBarrier;
}

inline static VkBufferMemoryBarrier2KHR
VkMakeBufferBarrier2(
	VkBuffer					hBuff,
	VkAccessFlags2KHR			srcAccess,
	VkPipelineStageFlags2KHR	srcStage,
	VkAccessFlags2KHR			dstAccess,
	VkPipelineStageFlags2KHR	dstStage,
	VkDeviceSize				buffOffset = 0,
	VkDeviceSize				buffSize = VK_WHOLE_SIZE,
	u32							srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED
){
	VkBufferMemoryBarrier2KHR barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR };
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.srcQueueFamilyIndex = srcQueueFamIdx;
	barrier.dstQueueFamilyIndex = dstQueueFamIdx;
	barrier.buffer = hBuff;
	barrier.offset = buffOffset;
	barrier.size = buffSize;

	return barrier;
}

inline static VkBufferMemoryBarrier2KHR VkReverseBufferBarrier2( const VkBufferMemoryBarrier2KHR& b )
{
	VkBufferMemoryBarrier2KHR barrier = b;
	std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
	std::swap( barrier.srcStageMask, barrier.dstStageMask );

	return barrier;
}

inline static VkImageMemoryBarrier2KHR
VkMakeImageBarrier2(
	VkImage						hImg,
	VkAccessFlags2KHR           srcAccessMask,
	VkPipelineStageFlags2KHR    srcStageMask,
	VkAccessFlags2KHR           dstAccessMask,
	VkPipelineStageFlags2KHR	dstStageMask,
	VkImageLayout               oldLayout,
	VkImageLayout               newLayout,
	VkImageAspectFlags			aspectMask,
	u32							baseMipLevel = 0,
	u32							mipCount = 0,
	u32							baseArrayLayer = 0,
	u32							layerCount = 0,
	u32							srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
){
	VkImageMemoryBarrier2KHR barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR };
	barrier.image = hImg;
	barrier.srcAccessMask = srcAccessMask;
	barrier.srcStageMask = srcStageMask;
	barrier.dstAccessMask = dstAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
	barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	barrier.subresourceRange.aspectMask = aspectMask;
	barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
	barrier.subresourceRange.baseMipLevel = baseMipLevel;
	barrier.subresourceRange.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS;
	barrier.subresourceRange.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS;

	return barrier;
}



struct virtual_frame
{
	buffer_data		frameData;
	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuff;
	VkSemaphore		canGetImgSema;
	VkSemaphore		canPresentSema;
};

// TODO: buffer_desc ?
// TODO: revisit
static inline virtual_frame VkCreateVirtualFrame(
	VkDevice		vkDevice,
	u32				exectutionQueueIdx,
	u32				bufferSize,
	vk_mem_arena&	arena
){
	virtual_frame vrtFrame = {};

	VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmdPoolInfo.queueFamilyIndex = exectutionQueueIdx;
	VK_CHECK( vkCreateCommandPool( vkDevice, &cmdPoolInfo, 0, &vrtFrame.cmdPool ) );

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmdBuffAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdBuffAllocInfo.commandBufferCount = 1;
	cmdBuffAllocInfo.commandPool = vrtFrame.cmdPool;
	VK_CHECK( vkAllocateCommandBuffers( vkDevice, &cmdBuffAllocInfo, &vrtFrame.cmdBuff ) );

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrame.canGetImgSema ) );
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrame.canPresentSema ) );

	vrtFrame.frameData = VkCreateAllocBindBuffer( 
		bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, arena );

	return vrtFrame;
}



struct swapchain
{
	VkSwapchainKHR	swapchain;
	VkImageView		imgViews[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkImage			imgs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkFormat		imgFormat;
	u16				width;
	u16				height;
	u8				imgCount;
};

// TODO: where to place these ?
extern HINSTANCE hInst;
extern HWND hWnd;

inline static VkSurfaceKHR VkMakeWinSurface( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR surfInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	surfInfo.hinstance = hInst;
	surfInfo.hwnd = hWnd;

	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;

#else
#error Must provide OS specific Surface
#endif // VK_USE_PLATFORM_WIN32_KHR
}
// TODO: sep initial validation form sc creation when resize ?
// TODO: tweak settings/config
// TODO: present from compute and graphics
inline static swapchain
VkMakeSwapchain(
	VkDevice			vkDevice,
	VkPhysicalDevice	vkPhysicalDevice,
	VkSurfaceKHR		vkSurf,
	u32					queueFamIdx,
	VkFormat			scDesiredFormat
){
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkPhysicalDevice, vkSurf, &surfaceCaps ) );

	VkCompositeAlphaFlagBitsKHR surfaceComposite =
		( surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR )
		? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
		: ( surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR )
		? VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
		: ( surfaceCaps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR )
		? VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
		: VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

	VkSurfaceFormatKHR scFormatAndColSpace = {};
	{
		u32 formatCount = 0;
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, 0 ) );
		std::vector<VkSurfaceFormatKHR> formats( formatCount );
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, std::data( formats ) ) );

		for( u64 i = 0; i < formatCount; ++i )
			if( formats[ i ].format == scDesiredFormat )
			{
				scFormatAndColSpace = formats[ i ];
				break;
			}

		VK_CHECK( VK_INTERNAL_ERROR( !scFormatAndColSpace.format ) );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, 0 ) );
		std::vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, std::data( presentModes ) ) );

		constexpr VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		for( u32 j = 0; j < numPresentModes; ++j )
			if( presentModes[ j ] == desiredPresentMode )
			{
				presentMode = desiredPresentMode;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !presentMode ) );
	}


	u32 scImgCount = VK_SWAPCHAIN_MAX_IMG_ALLOWED;
	assert( ( scImgCount > surfaceCaps.minImageCount ) && ( scImgCount < surfaceCaps.maxImageCount ) );
	assert( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) &&
			( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkImageUsageFlags scImgUsage =
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT;
	VK_CHECK( VK_INTERNAL_ERROR( ( surfaceCaps.supportedUsageFlags & scImgUsage ) != scImgUsage ) );


	VkSwapchainCreateInfoKHR scInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	scInfo.surface = vkSurf;
	scInfo.minImageCount = scImgCount;
	scInfo.imageFormat = scFormatAndColSpace.format;
	scInfo.imageColorSpace = scFormatAndColSpace.colorSpace;
	scInfo.imageExtent = surfaceCaps.currentExtent;
	assert( surfaceCaps.maxImageArrayLayers >= 1 );
	scInfo.imageArrayLayers = 1;
	scInfo.imageUsage = scImgUsage;
	scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	scInfo.preTransform = surfaceCaps.currentTransform;
	scInfo.compositeAlpha = surfaceComposite;
	scInfo.presentMode = presentMode;
	scInfo.queueFamilyIndexCount = 1;
	scInfo.pQueueFamilyIndices = &queueFamIdx;
	scInfo.clipped = VK_TRUE;
	scInfo.oldSwapchain = 0;

	VkImageFormatProperties scImageProps = {};
	VK_CHECK( vkGetPhysicalDeviceImageFormatProperties( vkPhysicalDevice,
														scInfo.imageFormat,
														VK_IMAGE_TYPE_2D,
														VK_IMAGE_TILING_OPTIMAL,
														scInfo.imageUsage,
														scInfo.flags,
														&scImageProps ) );


	swapchain sc = {};
	VK_CHECK( vkCreateSwapchainKHR( vkDevice, &scInfo, 0, &sc.swapchain ) );

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, 0 ) );
	VK_CHECK( VK_INTERNAL_ERROR( !( scImgsNum == scInfo.minImageCount ) ) );
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, sc.imgs ) );

	for( u64 i = 0; i < scImgsNum; ++i )
	{
		sc.imgViews[ i ] = VkMakeImgView( vkDevice, sc.imgs[ i ], scInfo.imageFormat, 0, 1, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );
	}

	sc.width = scInfo.imageExtent.width;
	sc.height = scInfo.imageExtent.height;
	sc.imgCount = scInfo.minImageCount;
	sc.imgFormat = scInfo.imageFormat;

	return sc;
}


static VkSurfaceKHR vkSurf;
static swapchain sc;


// TODO:
struct renderer_config
{
	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
};
// TODO: remake
struct render_context
{
	VkPipeline		gfxPipeline;
	VkPipeline		gfxMeshletPipeline;
	VkPipeline		gfxMergedPipeline;
	VkPipeline		compPipeline;
	VkPipeline		compHiZPipeline;
	
	VkPipeline		compAvgLumPipe;
	VkPipeline		compTonemapPipe;
	VkPipeline      compExpanderPipe;
	VkPipeline      compClusterCullPipe;
	VkPipeline      compExpMergePipe;

	VkRenderPass	renderPass;
	VkRenderPass	render2ndPass;

	VkSampler		quadMinSampler;
	VkSampler		pbrTexSampler;

	image			depthTarget;
	image			colorTarget;

	image			depthPyramid;
	VkImageView		depthPyramidChain[ MAX_MIP_LEVELS ];
	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

	virtual_frame	vrtFrames[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	VkSemaphore     timelineSema;
	u64				vFrameIdx = 0;
	u8				framesInFlight = VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
};

static render_context rndCtx;

// TODO: redesign
// NOTE: for timestamps we need 2 queries 
struct vk_gpu_timer
{
	buffer_data resultBuff;
	VkQueryPool queryPool;
	u32         queryCount;
	float       timestampPeriod;
};

static vk_gpu_timer vkGpuTimer[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];

// TODO: extend ?
struct vk_time_section
{
	const VkCommandBuffer& cmdBuff;
	const VkQueryPool& queryPool;
	const u32 queryIdx;

	inline vk_time_section( const VkCommandBuffer& _cmdBuff, const VkQueryPool& _queryPool, u32 _queryIdx )
		: cmdBuff{ _cmdBuff }, queryPool{ _queryPool }, queryIdx{ _queryIdx }
	{
		vkCmdWriteTimestamp2KHR( cmdBuff, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT_KHR, queryPool, queryIdx );
	}

	inline ~vk_time_section()
	{
		// VK_PIPELINE_STAGE_2_NONE_KHR
		vkCmdWriteTimestamp2KHR( cmdBuff, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR, queryPool, queryIdx + 1 );
	}
};

static inline vk_gpu_timer VkMakeGpuTimer( VkDevice vkDevice, u32 timerRegionsCount, float tsPeriod )
{
	u32 queryCount = 2 * timerRegionsCount;
	buffer_data resultBuff = VkCreateAllocBindBuffer( queryCount * sizeof( u64 ), VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkHostComArena );
	VkDbgNameObj( resultBuff.hndl, vkDevice, "Buff_Timestamp_Queries" );

	VkQueryPoolCreateInfo queryPoolInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = queryCount;
	queryPoolInfo.pipelineStatistics;

	VkQueryPool queryPool = {};
	VK_CHECK( vkCreateQueryPool( vkDevice, &queryPoolInfo, 0, &queryPool ) );
	VkDbgNameObj( queryPool, vkDevice, "VkQueryPool_GPU_timer" );

	return { resultBuff, queryPool, queryCount, tsPeriod };
}

static inline float VkCmdReadGpuTimeInMs( VkCommandBuffer cmdBuff, const vk_gpu_timer& vkTimer )
{
	vkCmdCopyQueryPoolResults( 
		cmdBuff, vkTimer.queryPool, 0, vkTimer.queryCount, vkTimer.resultBuff.hndl, 0, sizeof( u64 ),
		VK_QUERY_RESULT_64_BIT );// | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT );

	auto readTimestampsBarrier = VkMakeBufferBarrier2(
		vkTimer.resultBuff.hndl,
		VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
		VK_ACCESS_2_HOST_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_HOST_BIT_KHR );
	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &readTimestampsBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );

	const u64* pTimestamps = ( const u64* ) vkTimer.resultBuff.hostVisible;
	u64 timestampBeg = pTimestamps[ 0 ];
	u64 timestampEnd = pTimestamps[ 1 ];

	constexpr float nsToMs = 1e-6;
	return ( timestampEnd - timestampBeg ) / vkTimer.timestampPeriod * nsToMs;
}


#include "r_data_structs.h"


// TODO: tewak ? make own ?
// TODO: provide with own allocators
#define WIN32
#include "spirv_reflect.h"
#undef WIN32
// TODO: variable entry point
constexpr char SHADER_ENTRY_POINT[] = "main";

// TODO: rethink 
// TODO: cache shader ?
struct vk_shader
{
	VkShaderModule	module;
	std::vector<u8>	spvByteCode;
	u64				timestamp;
	char			entryPointName[ 32 ];
};

// TODO: where to place this ?
struct group_size
{
	u32 x;
	u32 y;
	u32 z;
};

// TODO: should make obsolete
// TODO: vk_shader_program ?
// TODO: put shader module inside ?
struct vk_program
{
	VkPipelineLayout			pipeLayout;
	VkDescriptorUpdateTemplate	descUpdateTemplate;
	VkShaderStageFlags			pushConstStages;
	VkDescriptorSetLayout       descSetLayout;
	group_size					groupSize;
};

struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo buff;
		VkDescriptorImageInfo img;
	};

	vk_descriptor_info() = default;
	
	vk_descriptor_info( VkBuffer buff, u64 offset, u64 range ) : buff{ buff, offset, range }{}
	vk_descriptor_info( VkDescriptorBufferInfo buffInfo ) : buff{ buffInfo }{}
	vk_descriptor_info( VkSampler sampler, VkImageView view, VkImageLayout imgLayout ) : img{ sampler, view, imgLayout }{}
	vk_descriptor_info( VkDescriptorImageInfo imgInfo ) : img{ imgInfo }{}
};


constexpr VkDescriptorType globalDescTable[] = {
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	VK_DESCRIPTOR_TYPE_SAMPLER
};

struct vk_global_descriptor
{
	VkDescriptorSetLayout	setLayout;
	VkDescriptorSet			set;
};

static vk_global_descriptor globBindlessDesc;


struct vk_slot_info
{
	VkDescriptorType type;
	u32 slot;  
	u32 count;
};
using vk_slot_list = std::initializer_list<vk_slot_info>;
inline static VkDescriptorSetLayout VkMakeDescriptorSetLayout(
	VkDevice         vkDevice,
	vk_slot_list	 bindingList
){
	std::vector<VkDescriptorSetLayoutBinding> bindingEntries;
	for( auto& e : bindingList )
	{
		bindingEntries.push_back( { e.slot, e.type, e.count, VK_SHADER_STAGE_ALL } );
	}
	constexpr VkDescriptorBindingFlags flag =
		VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
	std::vector<VkDescriptorBindingFlags> flags( std::size( bindingEntries ), flag );

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	descSetFalgs.bindingCount = std::size( flags );
	descSetFalgs.pBindingFlags = std::data( flags );
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetLayoutInfo.pNext = &descSetFalgs;
	descSetLayoutInfo.bindingCount = std::size( bindingEntries );
	descSetLayoutInfo.pBindings = std::data( bindingEntries );

	VkDescriptorSetLayout layout = {};
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &layout ) );

	return layout;
}

using vk_binding_list = std::initializer_list<std::pair<u32, u32>>;

// TODO: write own c++ vector + stack_vector
inline static vk_global_descriptor VkMakeBindlessGlobalDescriptor(
	VkDevice         vkDevice,
	VkDescriptorPool vkDescPool, 
	vk_binding_list	 bindingList
){
	std::vector<VkDescriptorSetLayoutBinding> bindingEntries;
	for( auto& e : bindingList )
	{
		bindingEntries.push_back( { e.first, globalDescTable[ e.first ], e.second, VK_SHADER_STAGE_ALL } );
	}
	constexpr VkDescriptorBindingFlags flag = 
		VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
	std::vector<VkDescriptorBindingFlags> flags( std::size( bindingEntries ), flag );

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	descSetFalgs.bindingCount = std::size( flags );
	descSetFalgs.pBindingFlags = std::data( flags );
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetLayoutInfo.pNext = &descSetFalgs;
	descSetLayoutInfo.bindingCount = std::size( bindingEntries );
	descSetLayoutInfo.pBindings = std::data( bindingEntries );

	VkDescriptorSetLayout layout = {};
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &layout ) );


	VkDescriptorSetAllocateInfo descSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descSetInfo.descriptorPool = vkDescPool;
	descSetInfo.descriptorSetCount = 1;
	descSetInfo.pSetLayouts = &layout;

	VkDescriptorSet set = {};
	VK_CHECK( vkAllocateDescriptorSets( vkDevice, &descSetInfo, &set ) );

	return { layout, set };
}

inline static VkShaderModule VkMakeShaderModule( VkDevice vkDevice, const u32* spv, u64 size )
{
	VkShaderModuleCreateInfo shaderModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shaderModuleInfo.codeSize = size;
	shaderModuleInfo.pCode = spv;

	VkShaderModule sm = {};
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &sm ) );
	return sm;
}

// TODO: no C++ and vector ?
inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	// TODO: 
	using namespace std;
	constexpr std::string_view shadersFolder = "Shaders/"sv;
	constexpr std::string_view shaderExtension = ".spv"sv;

	std::vector<u8> binSpvShader = SysReadFile( shaderPath );

	vk_shader shader = {};
	shader.spvByteCode = std::move( binSpvShader );
	shader.module = VkMakeShaderModule( vkDevice, 
										  (const u32*) std::data( shader.spvByteCode ), 
										  std::size( shader.spvByteCode ) );
	
	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shadersFolder ) );
	shaderName.remove_suffix( std::size( shaderExtension ) - 1 );
	VkDbgNameObj( shader.module, vkDevice, &shaderName[ 0 ] );

	return shader;
}

inline static void VkReflectShaderLayout(
	const VkPhysicalDeviceProperties&			gpuProps,
	const std::vector<u8>&						spvByteCode,
	std::vector<VkDescriptorSetLayoutBinding>&	descSetBindings,
	std::vector<VkPushConstantRange>&			pushConstRanges,
	group_size&									gs,
	char*										entryPointName,
	u64											entryPointNameStrLen 
){
	SpvReflectShaderModule shaderReflection;
	VK_CHECK( (VkResult) spvReflectCreateShaderModule( std::size( spvByteCode ) * sizeof( spvByteCode[ 0 ] ),
													   std::data( spvByteCode ),
													   &shaderReflection ) );

	SpvReflectDescriptorSet& set = shaderReflection.descriptor_sets[ 0 ];

	for( u64 bindingIdx = 0; bindingIdx < set.binding_count; ++bindingIdx )
	{
		if( set.set > 0 ) continue;

		const SpvReflectDescriptorBinding& descBinding = *set.bindings[ bindingIdx ];

		if( bindingIdx < std::size( descSetBindings ) )
		{
			// NOTE: if binding matches, assume the same resource will be used in multiple shaders in the same pipeline/program
			// TODO: should VK_CHECK here ?
			assert( descSetBindings[ bindingIdx ].descriptorType == VkDescriptorType( descBinding.descriptor_type ) );
			descSetBindings[ bindingIdx ].stageFlags |= shaderReflection.shader_stage;
			continue;
		}

		VkDescriptorSetLayoutBinding binding = {};
		binding.binding = descBinding.binding;
		binding.descriptorType = VkDescriptorType( descBinding.descriptor_type );
		binding.descriptorCount = descBinding.count;
		binding.stageFlags = shaderReflection.shader_stage;
		descSetBindings.push_back( binding );
	}

	for( u64 pci = 0; pci < shaderReflection.push_constant_block_count; ++pci )
	{
		VkPushConstantRange pushConstRange = {};
		pushConstRange.stageFlags = shaderReflection.shader_stage;
		pushConstRange.offset = shaderReflection.push_constant_blocks[ pci ].offset;
		pushConstRange.size = shaderReflection.push_constant_blocks[ pci ].size;
		VK_CHECK( VK_INTERNAL_ERROR( pushConstRange.size > gpuProps.limits.maxPushConstantsSize ) );

		pushConstRanges.push_back( pushConstRange );
	}

	assert( shaderReflection.entry_point_count == 1 );
	const SpvReflectEntryPoint& entryPoint = shaderReflection.entry_points[ 0 ];
	assert( std::strlen( entryPoint.name ) <= entryPointNameStrLen );
	std::memcpy( entryPointName, entryPoint.name, entryPointNameStrLen );
	if( VkShaderStageFlags( shaderReflection.shader_stage ) == VK_SHADER_STAGE_COMPUTE_BIT )
		gs = { entryPoint.local_size.x, entryPoint.local_size.y, entryPoint.local_size.z };

	spvReflectDestroyShaderModule( &shaderReflection );
}

// TODO: map spec consts ?
using vk_shader_list = std::initializer_list<vk_shader*>;
using vk_specializations = std::initializer_list<u32>;
using vk_dynamic_states = std::initializer_list<VkDynamicState>;

// TODO: bindlessLayout only for the shaders that use it ?
inline static vk_program VkMakePipelineProgram(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties&	gpuProps,
	VkPipelineBindPoint					bindPoint,
	vk_shader_list						shaders,
	VkDescriptorSetLayout				bindlessLayout = globBindlessDesc.setLayout 
){
	assert( std::size( shaders ) );
	
	vk_program program = {};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkPushConstantRange>	pushConstRanges;
	group_size gs = {};
	
	for( vk_shader* s : shaders )
	{
		VkReflectShaderLayout( 
			gpuProps, s->spvByteCode, bindings, pushConstRanges, gs, s->entryPointName, std::size( s->entryPointName ) );
	}
		
	VkDescriptorSetLayout descSetLayout = {};
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	descSetLayoutInfo.bindingCount = std::size( bindings );
	descSetLayoutInfo.pBindings = std::data( bindings );
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &descSetLayout ) );

	VkDescriptorSetLayout setLayouts[] = { descSetLayout, bindlessLayout };

	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = std::size( setLayouts );
	pipeLayoutInfo.pSetLayouts = setLayouts;
	pipeLayoutInfo.pushConstantRangeCount = std::size( pushConstRanges );
	pipeLayoutInfo.pPushConstantRanges = std::data( pushConstRanges );
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &program.pipeLayout ) );

	if( std::size( bindings ) )
	{
		std::vector<VkDescriptorUpdateTemplateEntry> entries;
		entries.reserve( std::size( bindings ) );
		for( const VkDescriptorSetLayoutBinding& binding : bindings )
		{
			VkDescriptorUpdateTemplateEntry entry = {};
			entry.dstBinding = binding.binding;
			entry.dstArrayElement = 0;
			entry.descriptorCount = binding.descriptorCount;
			entry.descriptorType = binding.descriptorType;
			entry.offset = std::size( entries ) * sizeof( vk_descriptor_info );
			entry.stride = sizeof( vk_descriptor_info );
			entries.emplace_back( entry );
		}

		VkDescriptorUpdateTemplateCreateInfo templateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };
		templateInfo.descriptorUpdateEntryCount = std::size( entries );
		templateInfo.pDescriptorUpdateEntries = std::data( entries );
		templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
		templateInfo.descriptorSetLayout = descSetLayout;
		templateInfo.pipelineBindPoint = bindPoint;
		templateInfo.pipelineLayout = program.pipeLayout;
		templateInfo.set = 0;
		VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &program.descUpdateTemplate ) );
	}
	
	//vkDestroyDescriptorSetLayout( vkDevice, descSetLayout, 0 );

	program.descSetLayout = descSetLayout;
	program.pushConstStages = std::size( pushConstRanges ) ? pushConstRanges[ 0 ].stageFlags : 0;
	program.groupSize = gs;

	return program;
}

// TODO: 
inline void VkKillPipelineProgram( VkDevice vkDevice, vk_program* program )
{}

inline static VkSpecializationInfo
VkMakeSpecializationInfo(
	std::vector<VkSpecializationMapEntry>& specializations,
	const vk_specializations& consts
){
	specializations.resize( std::size( consts ) );
	u64 sizeOfASpecConst = sizeof( *std::cbegin( consts ) );
	for( u64 i = 0; i < std::size( consts ); ++i )
		specializations[ i ] = { u32( i ), u32( i * sizeOfASpecConst ), u32( sizeOfASpecConst ) };

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = std::size( specializations );
	specInfo.pMapEntries = std::data( specializations );
	specInfo.dataSize = std::size( consts ) * sizeOfASpecConst;
	specInfo.pData = std::cbegin( consts );

	return specInfo;
}

// TODO: store more stuff ?
struct vk_gfx_pipeline_state
{
	VkPolygonMode		polyMode = VK_POLYGON_MODE_FILL;
	VkCullModeFlags		cullFlags = VK_CULL_MODE_BACK_BIT;
	VkFrontFace			frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkPrimitiveTopology primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkBlendFactor       srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	VkBlendFactor       dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	VkBlendFactor       srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	VkBlendFactor       dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	float               extraPrimitiveOverestimationSize = 0.0f;
	bool                conservativeRasterEnable = false;
	bool				depthWrite = true;
	bool				depthTestEnable = true;
	bool				blendCol = colorBlending;
};
// TODO: shader stages more general
// TODO: specialization for gfx ?
// TODO: depth clamp ?
// TODO: entry point name
VkPipeline VkMakeGfxPipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkRenderPass		vkRndPass,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		vs,
	VkShaderModule		fs,
	const vk_gfx_pipeline_state& pipelineState
){
	VkPipelineShaderStageCreateInfo shaderStagesInfo[ 2 ] = {};
	shaderStagesInfo[ 0 ].sType = shaderStagesInfo[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;

	shaderStagesInfo[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStagesInfo[ 0 ].module = vs;
	shaderStagesInfo[ 0 ].pName = SHADER_ENTRY_POINT;
	shaderStagesInfo[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStagesInfo[ 1 ].module = fs;
	shaderStagesInfo[ 1 ].pName = SHADER_ENTRY_POINT;

	u32 shaderStagesCount = bool( vs ) + bool( fs );

	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inAsmStateInfo.topology = pipelineState.primTopology;

	VkPipelineViewportStateCreateInfo viewportInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateInfo.dynamicStateCount = std::size( dynamicStates );
	dynamicStateInfo.pDynamicStates = dynamicStates;

	// TODO: place inside if ?
	VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterState =
	{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT };
	conservativeRasterState.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT;
	conservativeRasterState.extraPrimitiveOverestimationSize = pipelineState.extraPrimitiveOverestimationSize;
	
	VkPipelineRasterizationStateCreateInfo rasterInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterInfo.pNext = pipelineState.conservativeRasterEnable ? &conservativeRasterState : 0;
	rasterInfo.depthClampEnable = 0;
	rasterInfo.rasterizerDiscardEnable = 0;
	rasterInfo.polygonMode = pipelineState.polyMode;
	rasterInfo.cullMode = pipelineState.cullFlags;
	rasterInfo.frontFace = pipelineState.frontFace;
	rasterInfo.lineWidth = 1.0f;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = pipelineState.depthTestEnable;
	depthStencilState.depthWriteEnable = pipelineState.depthWrite;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER;
	depthStencilState.depthBoundsTestEnable = VK_TRUE;
	depthStencilState.minDepthBounds = 0;
	depthStencilState.maxDepthBounds = 1.0f;

	VkPipelineColorBlendAttachmentState blendConfig = {};
	blendConfig.blendEnable = pipelineState.blendCol;
	blendConfig.srcColorBlendFactor = pipelineState.srcColorBlendFactor;
	blendConfig.dstColorBlendFactor = pipelineState.dstColorBlendFactor;
	blendConfig.colorBlendOp = VK_BLEND_OP_ADD;
	blendConfig.srcAlphaBlendFactor = pipelineState.srcAlphaBlendFactor;
	blendConfig.dstAlphaBlendFactor = pipelineState.dstAlphaBlendFactor;
	blendConfig.alphaBlendOp = VK_BLEND_OP_ADD;
	blendConfig.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &blendConfig;

	// TODO: only if we use frag
	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.stageCount = shaderStagesCount;// std::size( shaderStagesInfo );
	pipelineInfo.pStages = shaderStagesInfo;
	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	pipelineInfo.pVertexInputState = &vtxInCreateInfo;
	pipelineInfo.pInputAssemblyState = &inAsmStateInfo;
	pipelineInfo.pViewportState = &viewportInfo;
	pipelineInfo.pRasterizationState = &rasterInfo;
	pipelineInfo.pMultisampleState = &multisamplingInfo;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.renderPass = vkRndPass;
	pipelineInfo.basePipelineIndex = -1;

	VkPipeline vkGfxPipeline;
	VK_CHECK( vkCreateGraphicsPipelines( vkDevice, vkPipelineCache, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	return vkGfxPipeline;
}

// TODO: pipeline caputre representations blah blah ?
VkPipeline VkMakeComputePipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		cs,
	vk_specializations	consts,
	const char*			pEntryPointName = SHADER_ENTRY_POINT 
){
	std::vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cs;
	stage.pName = pEntryPointName;
	stage.pSpecializationInfo = &specInfo;

	VkComputePipelineCreateInfo compPipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	compPipelineInfo.stage = stage;
	compPipelineInfo.layout = vkPipelineLayout;

	VkPipeline pipeline = 0;
	VK_CHECK( vkCreateComputePipelines( vkDevice, vkPipelineCache, 1, &compPipelineInfo, 0, &pipeline ) );

	return pipeline;
}

// TODO: use VK_ATTACHMENT_UNUSED
// TODO: must make own assert thing
inline static VkRenderPass
VkMakeRenderPass(
	VkDevice vkDevice,
	u64      depthSlot,
	u64      colSlot,
	bool     depthLoadClear,
	bool     colLoadClear,
	VkFormat depthFormat,
	VkFormat colFormat
){
	assert( ( ( depthSlot != -1 ) && depthFormat ) || ( ( colSlot != -1 ) && colFormat ) );
	// NOTE: support any permutation of 0 and 1 ;
	assert( colSlot * depthSlot == 0 );

	VkAttachmentReference colorAttachement = { colSlot, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR };
	VkAttachmentReference depthAttachement = { depthSlot, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR };

	VkSubpassDescription subpassDescr = {};
	subpassDescr.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescr.colorAttachmentCount = ( colSlot != -1 ) ? 1 : 0;
	subpassDescr.pColorAttachments = ( colSlot != -1 ) ? &colorAttachement : 0;
	subpassDescr.pDepthStencilAttachment = ( depthSlot != -1 ) ? &depthAttachement : 0;

	VkAttachmentDescription attachmentDescriptions[ 2 ] = {};
	// NOTE: invariants for now
	attachmentDescriptions[ 0 ].samples = attachmentDescriptions[ 1 ].samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescriptions[ 0 ].storeOp = attachmentDescriptions[ 1 ].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachmentDescriptions[ 0 ].stencilLoadOp = attachmentDescriptions[ 1 ].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescriptions[ 0 ].stencilStoreOp = attachmentDescriptions[ 1 ].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescriptions[ 0 ].initialLayout = attachmentDescriptions[ 1 ].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
	attachmentDescriptions[ 0 ].finalLayout = attachmentDescriptions[ 1 ].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
	if( colSlot != -1 )
	{
		attachmentDescriptions[ colSlot ].format = colFormat;
		attachmentDescriptions[ colSlot ].loadOp = colLoadClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	if( depthSlot != -1 )
	{
		attachmentDescriptions[ depthSlot ].format = depthFormat;
		attachmentDescriptions[ depthSlot ].loadOp = depthLoadClear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	}
	VkRenderPassCreateInfo renderPassInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = std::max( i64( depthSlot ), i64( colSlot ) ) + 1;
	renderPassInfo.pAttachments = attachmentDescriptions;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescr;

	VkRenderPass rndPass;
	VK_CHECK( vkCreateRenderPass( vkDevice, &renderPassInfo, 0, &rndPass ) );
	return rndPass;
}

inline static VkFramebuffer
VkMakeFramebuffer(
	VkDevice		vkDevice,
	VkRenderPass	vkRndPass,
	VkImageView*    attachements,
	u32				attachementCount,
	u32				width,
	u32				height
){
	VkFramebufferCreateInfo fboInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	fboInfo.renderPass = vkRndPass;
	fboInfo.attachmentCount = attachementCount;
	fboInfo.pAttachments = attachements;
	fboInfo.width = width;
	fboInfo.height = height;
	fboInfo.layers = 1;

	VkFramebuffer fbo;
	VK_CHECK( vkCreateFramebuffer( vkDevice, &fboInfo, 0, &fbo ) );
	return fbo;
}

// TODO: rename to program 
// TODO: what about push descriptors ?
// TODO: what about descriptors ?
// TODO: what about spec consts
struct vk_pipeline
{
	VkPipeline pipeline;
	VkPipelineLayout layout;
	// TODO: store push consts data ?
};

#include "asset_compiler.h"


inline VkFormat VkGetFormat( texture_format t )
{
	switch( t )
	{
	case TEXTURE_FORMAT_RBGA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
	case TEXTURE_FORMAT_RBGA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	case TEXTURE_FORMAT_BC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
	case TEXTURE_FORMAT_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
	case TEXTURE_FORMAT_UNDEFINED: assert( 0 );
	}
}
inline VkImageType VkGetImageType( texture_type t )
{
	switch( t )
	{
	case TEXTURE_TYPE_1D: return VK_IMAGE_TYPE_1D;
	case TEXTURE_TYPE_2D: return VK_IMAGE_TYPE_2D;
	case TEXTURE_TYPE_3D: return VK_IMAGE_TYPE_3D;
	default: assert( 0 ); return VK_IMAGE_TYPE_MAX_ENUM;
	}
}
// TODO: default types ?
inline VkFilter VkGetFilterTypeFromGltf( gltf_sampler_filter f )
{
	switch( f )
	{
	case GLTF_SAMPLER_FILTER_NEAREST:
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	return VK_FILTER_NEAREST;

	case GLTF_SAMPLER_FILTER_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	default:
	return VK_FILTER_LINEAR;
	}
}
inline VkSamplerMipmapMode VkGetMipmapTypeFromGltf( gltf_sampler_filter m )
{
	switch( m )
	{
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	default:
	return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}
inline VkSamplerAddressMode VkGetAddressModeFromGltf( gltf_sampler_address_mode a )
{
	switch( a )
	{
	case GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case GLTF_SAMPLER_ADDRESS_MODE_REPEAT: default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}
// TODO: ensure mipmapMode in assetcmpl
// TODO: addrModeW ?
// TODO: more stuff ?
inline VkSamplerCreateInfo VkMakeSamplerInfo( sampler_config config )
{
	assert( 0 );
	VkSamplerCreateInfo vkSamplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	vkSamplerInfo.minFilter = VkGetFilterTypeFromGltf( config.min );
	vkSamplerInfo.magFilter = VkGetFilterTypeFromGltf( config.mag );
	vkSamplerInfo.mipmapMode =  VkGetMipmapTypeFromGltf( config.min );
	vkSamplerInfo.addressModeU = VkGetAddressModeFromGltf( config.addrU );
	vkSamplerInfo.addressModeV = VkGetAddressModeFromGltf( config.addrV );
	//vkSamplerInfo.addressModeW = 

	return vkSamplerInfo;
}

inline VkImageCreateInfo
VkMakeImageInfo(
	VkFormat			imgFormat,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			imgExtent,
	u32					mipLevels = 1,
	u32					layerCount = 1,
	VkImageType			imgType = VK_IMAGE_TYPE_2D
){
	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = imgType;
	imgInfo.format = imgFormat;
	imgInfo.extent = imgExtent;
	imgInfo.mipLevels = mipLevels;
	imgInfo.arrayLayers = layerCount;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return imgInfo;
}

inline VkImageCreateInfo VkGetImageInfoFromMetadata( const image_metadata& meta, VkImageUsageFlags usageFlags )
{
	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = VkGetImageType( meta.type );
	imgInfo.format = VkGetFormat( meta.format );
	imgInfo.extent = { u32( meta.width ),u32( meta.height ),1 };
	imgInfo.mipLevels = meta.mipCount;
	imgInfo.arrayLayers = meta.layerCount;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return imgInfo;
}


#define HTVK_NO_SAMPLER_REDUCTION VK_SAMPLER_REDUCTION_MODE_MAX_ENUM

// TODO: AddrMode ?
inline VkSampler VkMakeSampler(
	VkDevice				vkDevice,
	VkSamplerReductionMode	reductionMode = HTVK_NO_SAMPLER_REDUCTION,
	VkFilter				filter = VK_FILTER_LINEAR,
	VkSamplerAddressMode	addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	VkSamplerMipmapMode		mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
){
	VkSamplerReductionModeCreateInfo reduxInfo = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO };
	reduxInfo.reductionMode = reductionMode;

	VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.pNext = ( reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX_ENUM ) ? 0 : &reduxInfo;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipmapMode = mipmapMode;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	VkSampler sampler;
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


template<typename T>
struct hndl32
{
	u32 h = 0;

	hndl32() = default;
	inline hndl32( u32 magkIdx ) : h{ magkIdx }{}
	inline operator u32() const { return h; }
};

template <typename T>
inline u64 IdxFromHndl32( hndl32<T> h )
{
	constexpr u32 standardIndexMask = ( 1 << 16 ) - 1;
	return h & standardIndexMask;
}
template <typename T>
inline u64 MagicFromHndl32( hndl32<T> h )
{
	constexpr u32 standardIndexMask = ( 1 << 16 ) - 1;
	constexpr u32 standardMagicNumberMask = ~standardIndexMask;
	return ( h & standardMagicNumberMask ) >> 16;
}
template <typename T>
inline hndl32<T> Hndl32FromMagicAndIdx( u64 m, u64 i )
{
	return u32( ( m << 16 ) | i );
}

// TODO: handled container
// TODO: extract magicId from resources ?
template<typename T>
struct resource_vector
{
	struct resource { T data; u32 magicId; };
	std::vector<resource> rsc;
	u32 magicCounter;
};

template<typename T>
inline const T& GetResourceFromHndl( hndl32<T> h, const resource_vector<T>& buf )
{
	assert( std::size( buf ) );
	assert( h );

	const T& entry = buf[ IdxFromHndl32( h ) ];
	assert( entry.magicId == MagicFromHndl32( h ) );

	return entry.data;
}
template<typename T>
inline hndl32<T> PushResourceToContainer( T& rsc, resource_vector<T>& buf )
{
	u32 magicCounter = buf.magicCounter++;
	buf.rsc.push_back( { rsc, magicCounter } );

	return Hndl32FromMagicAndIdx<T>( magicCounter, std::size( buf.rsc ) );
}


static resource_vector<image> textures;

// TODO: recycle_queue for more objects
// TODO: rethink
// TODO: async 
// TODO: MT 
struct staging_manager
{
	struct upload_job
	{
		VkBuffer	buf;
		u64			frameId;
	};
	std::vector<upload_job>		pendingUploads;
	u64							semaSignalCounter;
};

inline static void
StagingManagerPushForRecycle( VkBuffer stagingBuf, staging_manager& stgMngr, u64 currentFrameId )
{
	stgMngr.pendingUploads.push_back( { stagingBuf,currentFrameId } );
}


static staging_manager stagingManager;


struct box_bounds
{
	DirectX::XMFLOAT3 min;
	DirectX::XMFLOAT3 max;
};

struct entities_data
{
	std::vector<DirectX::XMFLOAT4X4A> transforms;
	std::vector<box_bounds> instAabbs;
};


#include "imgui/imgui.h"

// TODO: better double buffer vert + idx
// TODO: move spv shaders into exe folder
// TODO: different mem pool ?
struct imgui_vk_context
{
	buffer_data                 vtxBuffs[ 2 ];
	buffer_data                 idxBuffs[ 2 ];
	image                       fontsImg;
	VkRenderPass                renderPass;
	VkDescriptorSetLayout       descSetLayout;
	VkPipelineLayout            pipelineLayout;
	VkDescriptorUpdateTemplate  descTemplate = {};
	VkPipeline	                pipeline;
	VkSampler                   fontSampler;
};

static imgui_vk_context imguiVkCtx;

// TODO: buffer resize ?
// TODO: vk formats 
// TODO: better paradigm ?
static inline imgui_vk_context ImguiMakeVkContext(
	VkDevice vkDevice,
	const VkPhysicalDeviceProperties& gpuProps,
	VkFormat colDstFormat
){
	VkRenderPass rndPass = VkMakeRenderPass( vkDevice, -1, 0, -1, 0, VkFormat( 0 ), colDstFormat );

	VkSampler fontSampler = VkMakeSampler( vkDevice, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );
	
	VkDescriptorSetLayoutBinding descSetBindings[ 2 ] = {};
	descSetBindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descSetBindings[ 0 ].descriptorCount = 1;
	descSetBindings[ 0 ].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	descSetBindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descSetBindings[ 1 ].descriptorCount = 1;
	descSetBindings[ 1 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	descSetBindings[ 1 ].pImmutableSamplers = &fontSampler;
	descSetBindings[ 1 ].binding = 1;

	VkDescriptorSetLayoutCreateInfo descSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	descSetInfo.bindingCount = std::size( descSetBindings );
	descSetInfo.pBindings = descSetBindings;
	VkDescriptorSetLayout descSetLayout = {};
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetInfo, 0, &descSetLayout ) );

	VkPushConstantRange pushConst = { VK_SHADER_STAGE_VERTEX_BIT, 0,sizeof( float ) * 4 };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConst;

	VkPipelineLayout pipelineLayout = {};
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipelineLayoutInfo, 0, &pipelineLayout ) );

	VkDescriptorUpdateTemplateEntry entries[ 2 ] = {};
	entries[ 0 ].descriptorCount = 1;
	entries[ 0 ].descriptorType = descSetBindings[ 0 ].descriptorType;
	entries[ 0 ].offset = 0;
	entries[ 0 ].stride = sizeof( vk_descriptor_info );
	entries[ 1 ].descriptorCount = 1;
	entries[ 1 ].descriptorType = descSetBindings[ 1 ].descriptorType;
	entries[ 1 ].offset = sizeof( vk_descriptor_info );
	entries[ 1 ].stride = sizeof( vk_descriptor_info );
	entries[ 1 ].dstBinding = descSetBindings[ 1 ].binding;

	VkDescriptorUpdateTemplateCreateInfo templateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };
	templateInfo.descriptorUpdateEntryCount = std::size( entries );
	templateInfo.pDescriptorUpdateEntries = std::data( entries );
	templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
	templateInfo.descriptorSetLayout = descSetLayout;
	templateInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	templateInfo.pipelineLayout = pipelineLayout;

	VkDescriptorUpdateTemplate descTemplate = {};
	VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &descTemplate ) );

	vk_shader vert = VkLoadShader( "Shaders/shader_imgui.vert.spv", vkDevice );
	vk_shader frag = VkLoadShader( "Shaders/shader_imgui.frag.spv", vkDevice );

	vk_gfx_pipeline_state guiState = {};
	guiState.blendCol = true; 
	guiState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	guiState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	guiState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	guiState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	guiState.depthWrite = false;
	guiState.depthTestEnable = false;
	guiState.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	guiState.polyMode = VK_POLYGON_MODE_FILL;
	guiState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	guiState.cullFlags = VK_CULL_MODE_NONE;
	VkPipeline pipeline = VkMakeGfxPipeline( vkDevice, 0, rndPass, pipelineLayout, vert.module, frag.module, guiState );


	
	imgui_vk_context ctx = {};
	ctx.descSetLayout = descSetLayout;
	ctx.pipelineLayout = pipelineLayout;
	ctx.pipeline = pipeline;
	ctx.descTemplate = descTemplate;
	ctx.fontSampler = fontSampler;
	ctx.renderPass = rndPass;
	ctx.vtxBuffs[ 0 ] = VkCreateAllocBindBuffer( 64 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena );
	ctx.idxBuffs[ 0 ] = VkCreateAllocBindBuffer( 64 * KB, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, vkHostComArena );
	ctx.vtxBuffs[ 1 ] = VkCreateAllocBindBuffer( 64 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena );
	ctx.idxBuffs[ 1 ] = VkCreateAllocBindBuffer( 64 * KB, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, vkHostComArena );
	return ctx;
}

__forceinline auto ImguiGetFontImage()
{
	struct retval
	{
		u8* pixels;
		u32 width;
		u32 heigh;
	};

	u8* pixels = 0;
	u32 width = 0, height = 0;
	ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &pixels, ( int* ) &width, ( int* ) &height );
	
	return retval{ pixels,width,height };
}

// TODO: overdraw more efficiently 
// TODO: no imgui dependency
static inline void ImguiDrawUiPass(
	const imgui_vk_context& ctx,
	VkCommandBuffer cmdBuff,
	VkFramebuffer uiFbo,
	u64 frameIdx
){
	static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
	static_assert( sizeof( ImDrawIdx ) == 2 );

	using namespace DirectX;

	const ImDrawData* guiDrawData = ImGui::GetDrawData();
	
	const buffer_data& vtxBuff = imguiVkCtx.vtxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	const buffer_data& idxBuff = imguiVkCtx.idxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];

	assert( guiDrawData->TotalVtxCount < u16( -1 ) );
	assert( guiDrawData->TotalVtxCount * sizeof( ImDrawVert ) < vtxBuff.size );

	ImDrawVert* vtxDst = ( ImDrawVert* ) vtxBuff.hostVisible;
	ImDrawIdx* idxDst = ( ImDrawIdx* ) idxBuff.hostVisible;
	for( u64 ci = 0; ci < guiDrawData->CmdListsCount; ++ci )
	{
		const ImDrawList* cmdList = guiDrawData->CmdLists[ ci ];
		std::memcpy( vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof( ImDrawVert ) );
		std::memcpy( idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof( ImDrawIdx ) );
		vtxDst += cmdList->VtxBuffer.Size;
		idxDst += cmdList->IdxBuffer.Size;
	}


	vk_label label = { cmdBuff,"Draw Imgui Pass",{} };

	VkClearValue clear = {};
	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = ctx.renderPass;
	rndPassBegInfo.framebuffer = uiFbo;
	rndPassBegInfo.renderArea = { 0,0,sc.width,sc.height };
	//rndPassBegInfo.clearValueCount = 1;
	//rndPassBegInfo.pClearValues = &clear;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline );

	vk_descriptor_info pushDescs[] = {
		Descriptor( vtxBuff ), {ctx.fontSampler, ctx.fontsImg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL} };

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, ctx.descTemplate, ctx.pipelineLayout, 0, pushDescs );

	float scale[ 2 ] = { 2.0f / guiDrawData->DisplaySize.x, 2.0f / guiDrawData->DisplaySize.y };
	float move[ 2 ] = { -1.0f - guiDrawData->DisplayPos.x * scale[ 0 ], -1.0f - guiDrawData->DisplayPos.y * scale[ 1 ] };
	XMFLOAT4 pushConst = { scale[ 0 ],scale[ 1 ],move[ 0 ],move[ 1 ] };
	vkCmdPushConstants( cmdBuff, ctx.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( pushConst ), &pushConst );
	vkCmdBindIndexBuffer( cmdBuff, idxBuff.hndl, 0, VK_INDEX_TYPE_UINT16 );


	// (0,0) unless using multi-viewports
	XMFLOAT2 clipOff = { guiDrawData->DisplayPos.x, guiDrawData->DisplayPos.y };
	// (1,1) unless using retina display which are often (2,2)
	XMFLOAT2 clipScale = { guiDrawData->FramebufferScale.x, guiDrawData->FramebufferScale.y };

	u32 vtxOffset = 0;
	u32 idxOffset = 0;
	for( u64 li = 0; li < guiDrawData->CmdListsCount; ++li )
	{
		const ImDrawList* cmdList = guiDrawData->CmdLists[ li ];
		for( u64 ci = 0; ci < cmdList->CmdBuffer.Size; ++ci )
		{
			const ImDrawCmd* pCmd = &cmdList->CmdBuffer[ ci ];
			// Project scissor/clipping rectangles into framebuffer space
			XMFLOAT2 clipMin = { ( pCmd->ClipRect.x - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.y - clipOff.y ) * clipScale.y };
			XMFLOAT2 clipMax = { ( pCmd->ClipRect.z - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.w - clipOff.y ) * clipScale.y };

			// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
			clipMin = { std::max( clipMin.x, 0.0f ), std::max( clipMin.y, 0.0f ) };
			clipMax = { std::min( clipMax.x, ( float ) sc.width ), std::min( clipMax.y, ( float ) sc.height ) };
			
			if( clipMax.x < clipMin.x || clipMax.y < clipMin.y ) continue;

			VkRect2D scissor = { i32( clipMin.x ), i32( clipMin.y ), u32( clipMax.x - clipMin.x ), u32( clipMax.y - clipMin.y ) };
			vkCmdSetScissor( cmdBuff, 0, 1, &scissor );
			
			vkCmdDrawIndexed( cmdBuff, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
		}
		idxOffset += cmdList->IdxBuffer.Size;
		vtxOffset += cmdList->VtxBuffer.Size;
	}
	vkCmdEndRenderPass( cmdBuff );
}

// TODO: use instancing 4 drawing ?
struct debug_context
{
	buffer_data dbgLinesBuff;
	buffer_data dbgTrisBuff;
	vk_program	pipeProg;
	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;
};

static debug_context vkDbgCtx;

// TODO: query for gpu props ?
// TODO: dbgGeom buffer size based on what ?
// TODO: shader rename
static inline debug_context VkMakeDebugContext( 
	VkDevice							vkDevice, 
	VkRenderPass						vkRndPass,
	const VkPhysicalDeviceProperties&	gpuProps 
){
	debug_context dbgCtx = {};

	vk_shader vert = VkLoadShader( "Shaders/v_cpu_dbg_draw.vert.spv", vkDevice );
	vk_shader frag = VkLoadShader( "Shaders/f_pass_col.frag.spv", vkDevice );

	dbgCtx.pipeProg = VkMakePipelineProgram( vkDevice, gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vert, &frag } );

	static_assert( worldLeftHanded );
	vk_gfx_pipeline_state lineDrawPipelineState = {};
	lineDrawPipelineState.blendCol = VK_FALSE;
	lineDrawPipelineState.depthWrite = VK_FALSE;
	lineDrawPipelineState.depthTestEnable = VK_FALSE;
	lineDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;
	lineDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	lineDrawPipelineState.polyMode = VK_POLYGON_MODE_LINE;
	lineDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	dbgCtx.drawAsLines = VkMakeGfxPipeline( 
		vkDevice, 0, vkRndPass, dbgCtx.pipeProg.pipeLayout, vert.module, frag.module, lineDrawPipelineState );

	vk_gfx_pipeline_state triDrawPipelineState = {};
	triDrawPipelineState.blendCol = VK_TRUE;
	triDrawPipelineState.depthWrite = VK_TRUE;
	triDrawPipelineState.depthTestEnable = VK_TRUE;
	triDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;// VK_CULL_MODE_FRONT_BIT;
	triDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	triDrawPipelineState.polyMode = VK_POLYGON_MODE_FILL;
	triDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	dbgCtx.drawAsTriangles = VkMakeGfxPipeline( 
		vkDevice, 0, vkRndPass, dbgCtx.pipeProg.pipeLayout, vert.module, frag.module, triDrawPipelineState );


	vkDestroyShaderModule( vkDevice, vert.module, 0 );
	vkDestroyShaderModule( vkDevice, frag.module, 0 );

	dbgCtx.dbgLinesBuff = VkCreateAllocBindBuffer( 512 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena );
	dbgCtx.dbgTrisBuff = VkCreateAllocBindBuffer( 128 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena );

	return dbgCtx;
}

// TODO: color depth toggle stuff
inline static void
DebugDrawPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			vkPipeline,
	VkRenderPass		vkRndPass,
	VkFramebuffer	    offscreenFbo,
	const buffer_data& drawBuff,
	const vk_program& program,
	const mat4& projView,
	range		        drawRange
){
	vk_label label = { cmdBuff,"Dbg Draw Pass",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vk_descriptor_info pushDescs[] = { Descriptor( drawBuff ) };
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, pushDescs );
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( mat4 ), &projView );

	vkCmdDraw( cmdBuff, drawRange.size, 1, drawRange.offset, 0 );

	vkCmdEndRenderPass( cmdBuff );
}

static constexpr void ReverseTriangleWinding( u32* indices, u64 count )
{
	assert( count % 3 == 0 );
	for( u64 t = 0; t < count; t += 3 ) std::swap( indices[ t ], indices[ t + 2 ] );
}
// TODO: improve ?
static void GenerateIcosphere( std::vector<DirectX::XMFLOAT3>& vtxData, std::vector<u32>& idxData, u64 numIters )
{
	using namespace DirectX;

	constexpr u64 ICOSAHEDRON_FACE_NUM = 20;
	constexpr u64 ICOSAHEDRON_VTX_NUM = 12;

	constexpr float X = 0.525731112119133606f;
	constexpr float Z = 0.850650808352039932f;
	constexpr float N = 0;

	constexpr XMFLOAT3 vertices[ ICOSAHEDRON_VTX_NUM ] =
	{
		{-X,N,Z}, {X,N,Z}, {-X,N,-Z}, {X,N,-Z},
		{N,Z,X}, {N,Z,-X}, {N,-Z,X}, {N,-Z,-X},
		{Z,X,N}, {-Z,X, N}, {Z,-X,N}, {-Z,-X, N}
	};

	u32 triangles[ 3 * ICOSAHEDRON_FACE_NUM ] =
	{
		0,4,1,	0,9,4,	9,5,4,	4,5,8,	4,8,1,
		8,10,1,	8,3,10,	5,3,8,	5,2,3,	2,7,3,
		7,10,3,	7,6,10,	7,11,6,	11,0,6,	0,1,6,
		6,1,10,	9,0,11,	9,11,2,	9,2,5,	7,2,11
	};

	if constexpr( worldLeftHanded ) ReverseTriangleWinding( triangles, std::size( triangles ) );

	std::vector<XMFLOAT3> vtxCache;
	std::vector<u32> idxCache;

	vtxCache = { std::begin( vertices ), std::end( vertices ) };
	idxData = { std::begin( triangles ),std::end( triangles ) };

	//vtxCache.reserve( ICOSAHEDRON_VTX_NUM * std::exp2( numIters ) );
	idxCache.reserve( 3 * ICOSAHEDRON_FACE_NUM * exp2( 2 * numIters ) );
	idxData.reserve( 3 * ICOSAHEDRON_FACE_NUM * exp2( 2 * numIters ) );


	for( u64 i = 0; i < numIters; ++i )
	{
		for( u64 t = 0; t < std::size( idxData ); t += 3 )
		{
			u32 i0 = idxData[ t ];
			u32 i1 = idxData[ t + 1 ];
			u32 i2 = idxData[ t + 2 ];

			XMVECTOR v0 = XMLoadFloat3( &vtxCache[ i0 ] );
			XMVECTOR v1 = XMLoadFloat3( &vtxCache[ i1 ] );
			XMVECTOR v2 = XMLoadFloat3( &vtxCache[ i2 ] );
			XMFLOAT3 m01, m12, m20;
			XMStoreFloat3( &m01, XMVector3Normalize( XMVectorAdd( v0, v1 ) ) );
			XMStoreFloat3( &m12, XMVector3Normalize( XMVectorAdd( v1, v2 ) ) );
			XMStoreFloat3( &m20, XMVector3Normalize( XMVectorAdd( v2, v0 ) ) );

			u32 idxOffset = std::size( vtxCache ) - 1;

			vtxCache.push_back( m01 );
			vtxCache.push_back( m12 );
			vtxCache.push_back( m20 );

			if constexpr( !worldLeftHanded )
			{
				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( i0 );

				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( i2 );
				idxCache.push_back( idxOffset + 3 );

				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 3 );

				idxCache.push_back( i1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 1 );
			}
			else
			{
				idxCache.push_back( i0 );
				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( idxOffset + 1 );

				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( i2 );
				idxCache.push_back( idxOffset + 2 );

				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 1 );

				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( i1 );
			}
		}

		idxData = idxCache;
	}

	vtxData = std::move( vtxCache );
}
// TODO: remove ?
static void GenerateBoxCube( std::vector<DirectX::XMFLOAT4>& vtx, std::vector<u32>& idx )
{
	using namespace DirectX;

	constexpr float w = 0.5f;
	constexpr float h = 0.5f;
	constexpr float t = 0.5f;

	XMFLOAT3 c0 = { w, h,-t };
	XMFLOAT3 c1 = { -w, h,-t };
	XMFLOAT3 c2 = { -w,-h,-t };
	XMFLOAT3 c3 = { w,-h,-t };

	XMFLOAT3 c4 = { w, h, t };
	XMFLOAT3 c5 = { -w, h, t };
	XMFLOAT3 c6 = { -w,-h, t };
	XMFLOAT3 c7 = { w,-h, t };

	constexpr XMFLOAT4 col = {};

	XMFLOAT4 vertices[] = {
		// Bottom
		{c0.x,c0.y,c0.z,1.0f},
		{c1.x,c1.y,c1.z,1.0f},
		{c2.x,c2.y,c2.z,1.0f},
		{c3.x,c3.y,c3.z,1.0f},
		// Left					
		{c7.x,c7.y,c7.z,1.0f },
		{c4.x,c4.y,c4.z,1.0f },
		{c0.x,c0.y,c0.z,1.0f },
		{c3.x,c3.y,c3.z,1.0f },
		// Front			
		{c4.x,c4.y,c4.z,1.0f },
		{c5.x,c5.y,c5.z,1.0f },
		{c1.x,c1.y,c1.z,1.0f },
		{c0.x,c0.y,c0.z,1.0f },
		// Back				
		{c6.x,c6.y,c6.z,1.0f },
		{c7.x,c7.y,c7.z,1.0f },
		{c3.x,c3.y,c3.z,1.0f },
		{c2.x,c2.y,c2.z,1.0f },
		// Right			
		{c5.x,c5.y,c5.z,1.0f },
		{c6.x,c6.y,c6.z,1.0f },
		{c2.x,c2.y,c2.z,1.0f },
		{c1.x,c1.y,c1.z,1.0f },
		// Top				
		{c7.x,c7.y,c7.z,1.0f },
		{c6.x,c6.y,c6.z,1.0f },
		{c5.x,c5.y,c5.z,1.0f },
		{c4.x,c4.y,c4.z,1.0f }
	};

	u32 indices[] = {
			0, 1, 3,        1, 2, 3,        // Bottom	
			4, 5, 7,        5, 6, 7,        // Left
			8, 9, 11,       9, 10, 11,      // Front
			12, 13, 15,     13, 14, 15,     // Back
			16, 17, 19,     17, 18, 19,	    // Right
			20, 21, 23,     21, 22, 23	    // Top
	};

	if constexpr( worldLeftHanded ) ReverseTriangleWinding( indices, std::size( indices ) );

	vtx.insert( std::end( vtx ), std::begin( vertices ), std::end( vertices ) );
	idx.insert( std::end( idx ), std::begin( indices ), std::end( indices ) );
}


constexpr u64 boxLineVertexCount = 24u;
constexpr u64 boxTrisVertexCount = 36u;
/*
// NOTE: corners are stored in this way:
//	   3---------7
//	  /|        /|
//	 / |       / |
//	1---------5  |
//	|  2- - - |- 6
//	| /       | /
//	|/        |/
//	0---------4
*/
constexpr u8 boxLineIndices[] = {
	0,1,	0,2,	1,3,	2,3,
	0,4,	1,5,	2,6,	3,7,
	4,5,	4,6,	5,7,	6,7
};
// NOTE: counter-clockwise
constexpr u8 boxTrisIndices[] = {
	0, 1, 2,    // side 1
	2, 1, 3,
	4, 0, 6,    // side 2
	6, 0, 2,
	7, 5, 6,    // side 3
	6, 5, 4,
	3, 1, 7,    // side 4
	7, 1, 5,
	4, 5, 0,    // side 5
	0, 5, 1,
	3, 7, 2,    // side 6
	2, 7, 6,
};

inline void XM_CALLCONV 
TrnasformBoxVertices(
	DirectX::XMMATRIX		transf,
	DirectX::XMFLOAT3		boxMin,
	DirectX::XMFLOAT3		boxMax,
	DirectX::XMFLOAT4*		boxCorners
){
	using namespace DirectX;

	boxCorners[ 0 ] = { boxMax.x, boxMax.y, boxMax.z, 1.0f };
	boxCorners[ 1 ] = { boxMax.x, boxMin.y, boxMax.z, 1.0f };
	boxCorners[ 2 ] = { boxMax.x, boxMax.y, boxMin.z, 1.0f };
	boxCorners[ 3 ] = { boxMax.x, boxMin.y, boxMin.z, 1.0f };
	boxCorners[ 4 ] = { boxMin.x, boxMax.y, boxMax.z, 1.0f };
	boxCorners[ 5 ] = { boxMin.x, boxMin.y, boxMax.z, 1.0f };
	boxCorners[ 6 ] = { boxMin.x, boxMax.y, boxMin.z, 1.0f };
	boxCorners[ 7 ] = { boxMin.x, boxMin.y, boxMin.z, 1.0f };

	for( u64 ci = 0; ci < 8; ++ci )
	{
		XMFLOAT4& outCorner = boxCorners[ ci ];
		XMVECTOR trnasformedCorner = XMVector4Transform( XMLoadFloat4( &outCorner ), transf );
		XMStoreFloat4( &outCorner, trnasformedCorner );
	}
}

// TODO: draw them filled ? 
// TODO: rethink ?

// TODO: remove ?
inline void WriteBoundingBoxLines(
	const DirectX::XMFLOAT4X4A& mat, 
	const DirectX::XMFLOAT3& min, 
	const DirectX::XMFLOAT3& max,
	const DirectX::XMFLOAT4& col,
	dbg_vertex* thisLineRange
){
	using namespace DirectX;

	XMFLOAT4 boxVertices[ 8 ] = {};
	TrnasformBoxVertices( XMLoadFloat4x4A( &mat ), min, max, boxVertices );

	for( u64 ii = 0; ii < boxLineVertexCount; ++ii )
	{
		thisLineRange[ ii ] = { boxVertices[ boxLineIndices[ ii ] ],col };
	}
}

inline std::vector<dbg_vertex>
ComputeSceneDebugBoundingBoxes(
	DirectX::XMMATRIX frustDrawMat,
	const entities_data& entities
){
	using namespace DirectX;

	assert( std::size( entities.instAabbs ) == std::size( entities.transforms ) );
	assert( boxLineVertexCount == std::size( boxLineIndices ) );

	u64 entitiesCount = std::size( entities.transforms );
	std::vector<dbg_vertex> dbgGeom;
	// NOTE: + boxLineVertexCount for frustum
	dbgGeom.resize( entitiesCount * boxLineVertexCount + boxLineVertexCount );

	for( u64 i = 0; i < entitiesCount; ++i )
	{
		const box_bounds& aabb = entities.instAabbs[ i ];
		const XMFLOAT4X4A& mat = entities.transforms[ i ];

		XMFLOAT4 boxVertices[ 8 ] = {};
		TrnasformBoxVertices( XMLoadFloat4x4A( &mat ), aabb.min, aabb.max, boxVertices );

		dbg_vertex* boxLineRange = std::data( dbgGeom ) + i * boxLineVertexCount;
		constexpr XMFLOAT4 col = { 255.0f,255.0f,0.0f,1.0f };
		for( u64 ii = 0; ii < boxLineVertexCount; ++ii )
		{
			boxLineRange[ ii ] = { boxVertices[ boxLineIndices[ ii ] ],col };
		}
	}

	u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;

	XMFLOAT4 boxVertices[ 8 ] = {};
	TrnasformBoxVertices( frustDrawMat, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f }, boxVertices );

	dbg_vertex* boxLineRange = std::data( dbgGeom ) + frustBoxOffset;
	constexpr XMFLOAT4 frustCol = { 0.0f, 255.0f, 255.0f, 1.0f };
	for( u64 i = 0; i < boxLineVertexCount; ++i )
	{
		boxLineRange[ i ] = { boxVertices[ boxLineIndices[ i ] ],frustCol };
	}

	return dbgGeom;
}


static entities_data entities;
static std::vector<dbg_vertex> dbgLineGeomCache;


static buffer_data proxyGeomBuff;
static buffer_data proxyIdxBuff;

static buffer_data screenspaceBoxBuff;

static buffer_data globVertexBuff;
// TODO: use indirect merged index buffer
static buffer_data indexBuff;
static buffer_data meshBuff;

static buffer_data meshletBuff;
static buffer_data meshletDataBuff;

// TODO:
static buffer_data transformsBuff;

static buffer_data materialsBuff;
static buffer_data instDescBuff;
static buffer_data lightsBuff;

static buffer_data indirectMergedIndexBuff;

static buffer_data visibleInstsBuff;
static buffer_data meshletIdBuff;
static buffer_data visibleMeshletsBuff;

static buffer_data drawCmdBuff;
static buffer_data drawCmdAabbsBuff;
static buffer_data drawCmdDbgBuff;
static buffer_data drawVisibilityBuff;

static buffer_data drawMergedCmd;

constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "Assets/cyberbaron.drak";
//constexpr char glbPath[] = "D:\\3d models\\WaterBottle.glb";
//constexpr char drakPath[] = "Assets/WaterBottle.drak";
constexpr double RAND_MAX_SCALE = 1.0 / double( RAND_MAX );
// TODO: remove ?
inline static std::vector<instance_desc>
SpawnRandomInstances( const std::span<mesh_desc> meshes, u64 drawCount, u64 mtrlCount, float sceneRadius )
{
	using namespace DirectX;

	std::vector<instance_desc> insts( drawCount );
	float scale = 1.0f;
	for( instance_desc& i : insts )
	{
		i.meshIdx = rand() % std::size( meshes );
		i.mtrlIdx = 0;
		i.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.scale = scale * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		XMVECTOR axis = XMVector3Normalize(
			XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 0 ) );
		float angle = XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		//XMVECTOR quat = XMQuaternionRotationNormal( axis, angle );
		XMVECTOR quat = XMQuaternionIdentity();
		XMStoreFloat4( &i.rot, quat );

		XMMATRIX scaleM = XMMatrixScaling( i.scale, i.scale, i.scale );
		XMMATRIX rotM = XMMatrixRotationQuaternion( quat );
		XMMATRIX moveM = XMMatrixTranslation( i.pos.x, i.pos.y, i.pos.z );

		XMMATRIX localToWorld = XMMatrixMultiply( scaleM, XMMatrixMultiply( rotM, moveM ) );

		XMStoreFloat4x4A( &i.localToWorld, localToWorld );
	}

	return insts;
}
inline static std::vector<DirectX::XMFLOAT4X4A> SpawmRandomTransforms( u64 instCount, float sceneRadius, float globalScale )
{
	using namespace DirectX;

	std::vector<XMFLOAT4X4A> transf( instCount );
	for( XMFLOAT4X4A& t : transf )
	{
		float posX = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		float posY = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		float posZ = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		float scale = globalScale * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		XMVECTOR axis = XMVector3Normalize(
			XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 0 ) );
		float angle = XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		//XMVECTOR quat = XMQuaternionRotationNormal( axis, angle );
		XMVECTOR quat = XMQuaternionIdentity();
		
		XMMATRIX scaleM = XMMatrixScaling( scale, scale, scale );
		XMMATRIX rotM = XMMatrixRotationQuaternion( quat );
		XMMATRIX moveM = XMMatrixTranslation( posX, posY, posZ );

		XMMATRIX localToWorld = XMMatrixMultiply( scaleM, XMMatrixMultiply( rotM, moveM ) );

		XMStoreFloat4x4A( &t, localToWorld );
	}

	return transf;
}
inline static std::vector<light_data> SpawnRandomLights( u64 lightCount, float sceneRadius )
{
	std::vector<light_data> lights( lightCount );
	for( light_data& l : lights )
	{
		l.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.radius = 100.0f * float( rand() * RAND_MAX_SCALE ) + 2.0f;
		l.col = { 600.0f,200.0f,100.0f };
	}

	return lights;
}

constexpr u64 randSeed = 42;
constexpr u64 drawCount = 5;
constexpr u64 lightCount = 100;
constexpr float sceneRad = 40.0f;

constexpr u64 tileSize = 8u;
constexpr u64 tileRowSize = ( SCREEN_WIDTH + tileSize - 1 ) / tileSize;
constexpr u64 tileCount = tileRowSize * ( SCREEN_HEIGHT + tileSize - 1 ) / tileSize;
constexpr u64 wordsPerTile = ( lightCount + 31 ) / 32;

static buffer_data tileBuff;
// TODO: staging clean up
// TODO: re make offsets, ranges, etc
static inline void VkUploadResources( VkCommandBuffer cmdBuff, entities_data& entities, u64 currentFrameId )
{
	std::vector<u8> binaryData;
	// TODO: add renderable_instances
	// TODO: extra checks and stuff ?
	// TODO: ensure resources of the same type are contiguous ?
	{
		binaryData = SysReadFile( drakPath );
		if( std::size( binaryData ) == 0 )
		{
			std::vector<u8> fileData = SysReadFile( glbPath );
			CompileGlbAssetToBinary( fileData, binaryData );
			// TODO: does this override ?
			SysWriteToFile( drakPath, std::data( binaryData ), std::size( binaryData ) );
		}
	}

	const drak_file_footer& fileFooter =
		*( drak_file_footer* ) ( std::data( binaryData ) + std::size( binaryData ) - sizeof( drak_file_footer ) );
	{
		using namespace std;
		assert( "DRK"sv == fileFooter.magik  );
	}
	const std::span<mesh_desc> meshes = { 
		(mesh_desc*) ( std::data( binaryData ) + fileFooter.meshesByteRange.offset ),
		fileFooter.meshesByteRange.size / sizeof( mesh_desc ) };

	const std::span<material_data> mtrlDesc = {
		( material_data* ) ( std::data( binaryData ) + fileFooter.mtrlsByteRange.offset ),
		fileFooter.mtrlsByteRange.size / sizeof( material_data ) };
	
	const std::span<image_metadata> imgDesc = {
		( image_metadata* ) ( std::data( binaryData ) + fileFooter.imgsByteRange.offset ),
		fileFooter.imgsByteRange.size / sizeof( image_metadata ) };

	assert( std::size( textures.rsc ) == 0 );
	std::vector<material_data> mtrls = {};
	for( const material_data& m : mtrlDesc )
	{
		mtrls.push_back( m );
		material_data& refM = mtrls[ std::size( mtrls ) - 1 ];
		refM.baseColIdx += std::size( textures.rsc );
		refM.normalMapIdx += std::size( textures.rsc );
		refM.occRoughMetalIdx += std::size( textures.rsc );
	}

	std::srand( randSeed );

	assert( std::size( mtrls ) == 1 );
	std::vector<instance_desc> instDesc = SpawnRandomInstances( { std::data( meshes ),std::size( meshes ) }, drawCount, 1, sceneRad );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRad * 0.75f );

	assert( std::size( instDesc ) < u16( -1 ) );


	for( const instance_desc& ii : instDesc )
	{
		const mesh_desc& m = meshes[ ii.meshIdx ];
		entities.transforms.push_back( ii.localToWorld );
		entities.instAabbs.push_back( { m.aabbMin, m.aabbMax } );
	}


	std::vector<DirectX::XMFLOAT3> proxyVtx;
	std::vector<u32> proxyIdx;
	{
		GenerateIcosphere( proxyVtx, proxyIdx, 1 );
		// TODO: stupid templates
		u64 uniqueVtxCount = MeshoptReindexMesh( std::span<DirectX::XMFLOAT3>{ proxyVtx }, proxyIdx );
		proxyVtx.resize( uniqueVtxCount );
		MeshoptOptimizeMesh( std::span<DirectX::XMFLOAT3>{ proxyVtx }, proxyIdx );
		
		assert( std::size( lights ) < u16( -1 ) );
		assert( std::size( proxyVtx ) < u16( -1 ) );
		// NOTE: becaue there's only one type of light
		u64 initialSize = std::size( proxyIdx );
		proxyIdx.resize( initialSize * std::size( lights ) );
		for( u64 li = 0; li < std::size( lights ); ++li )
		{
			u64 idxBuffOffset = initialSize * li;
			for( u64 ii = 0; ii < initialSize; ++ii )
			{
				proxyIdx[ idxBuffOffset + ii ] = u32( proxyIdx[ ii ] & u16( -1 ) ) | ( u32( li ) << 16 );
			}
		}
	}


	// TODO: make easier to use 
	std::vector<VkBufferMemoryBarrier2KHR> buffBarriers;
	{
		const std::span<u8> vtxView = { std::data( binaryData ) + fileFooter.vtxByteRange.offset, fileFooter.vtxByteRange.size };
		
		globVertexBuff = VkCreateAllocBindBuffer( std::size( vtxView ),
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												  vkRscArena );
		VkDbgNameObj( globVertexBuff.hndl, dc.device, "Buff_Vtx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( std::size( vtxView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( vtxView ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, globVertexBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			globVertexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
	}
	{
		const std::span<u8> idxSpan = { std::data( binaryData ) + fileFooter.idxByteRange.offset, fileFooter.idxByteRange.size };

		indexBuff = VkCreateAllocBindBuffer(
			std::size( idxSpan ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
		VkDbgNameObj( indexBuff.hndl, dc.device, "Buff_Idx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( std::size( idxSpan ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( idxSpan ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, indexBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier2( 
			indexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR, 
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ) );
	}
	{
		meshBuff = VkCreateAllocBindBuffer( BYTE_COUNT( meshes ),
											VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											vkRscArena );
		VkDbgNameObj( meshBuff.hndl, dc.device, "Buff_Mesh_Desc" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( meshes ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( meshes ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
	}
	{
		lightsBuff = VkCreateAllocBindBuffer( BYTE_COUNT( lights ),
											  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											  vkRscArena );
		VkDbgNameObj( lightsBuff.hndl, dc.device, "Buff_Lights" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( lights ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( lights ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, lightsBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			lightsBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR ) );
	}
	{
		instDescBuff = VkCreateAllocBindBuffer( BYTE_COUNT( instDesc ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												vkRscArena );
		VkDbgNameObj( instDescBuff.hndl, dc.device, "Buff_Inst_Descs" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( instDesc ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( instDesc ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, instDescBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			instDescBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );

	}
	{
		materialsBuff = VkCreateAllocBindBuffer( BYTE_COUNT( mtrls ),
												 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												 vkRscArena );
		VkDbgNameObj( materialsBuff.hndl, dc.device, "Buff_Mtrls" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( mtrls ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( mtrls ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, materialsBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			materialsBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR ) );
	}
	{
		const std::span<u8> mletView = { std::data( binaryData ) + fileFooter.mletsByteRange.offset,fileFooter.mletsByteRange.size };
		
		assert( fileFooter.mletsByteRange.size < u16( -1 ) * sizeof( meshlet ) );

		meshletBuff = VkCreateAllocBindBuffer( std::size( mletView ),
											   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											   vkRscArena );
		VkDbgNameObj( meshletBuff.hndl, dc.device, "Buff_Meshlets" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( std::size( mletView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( mletView ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshletBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ) );
	}
	{
		const std::span<u8> mletDataView = { 
			std::data( binaryData ) + fileFooter.mletsDataByteRange.offset,
			fileFooter.mletsDataByteRange.size };

		meshletDataBuff = VkCreateAllocBindBuffer( std::size( mletDataView ),
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												  vkRscArena );
		VkDbgNameObj( meshletDataBuff.hndl, dc.device, "Buff_Meshlet_Data" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( std::size( mletDataView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( mletDataView ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshletDataBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletDataBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ) );
	}


	{
		proxyGeomBuff = VkCreateAllocBindBuffer(
			BYTE_COUNT( proxyVtx ), 
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
			vkRscArena );
		VkDbgNameObj( proxyGeomBuff.hndl, dc.device, "Buff_Proxy_Vtx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( proxyVtx ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( proxyVtx ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, proxyGeomBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			proxyGeomBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
	}
	{
		proxyIdxBuff = VkCreateAllocBindBuffer(
			BYTE_COUNT( proxyIdx ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
		VkDbgNameObj( proxyIdxBuff.hndl, dc.device, "Buff_Proxy_Idx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( proxyIdx ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( proxyIdx ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager, currentFrameId );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, proxyIdxBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			proxyIdxBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ) );
	}

	tileBuff = VkCreateAllocBindBuffer( tileCount * wordsPerTile * sizeof( u32 ),
										VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
										VK_BUFFER_USAGE_TRANSFER_DST_BIT |
										VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
										vkRscArena );
	VkDbgNameObj( tileBuff.hndl, dc.device, "Buff_Tiles" );

	drawCmdBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( draw_command ),
										   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
										   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
										   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
										   vkRscArena );
	VkDbgNameObj( drawCmdBuff.hndl, dc.device, "Buff_Indirect_Draw_Cmds" );

	drawCmdDbgBuff = VkCreateAllocBindBuffer( 
		( fileFooter.mletsByteRange.size / sizeof( meshlet ) )* sizeof( draw_command ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena );
	VkDbgNameObj( drawCmdDbgBuff.hndl, dc.device, "Buff_Indirect_Dbg_Draw_Cmds" );

	// TODO: use per wave stuff ?
	drawVisibilityBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( u32 ),
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												  vkRscArena );
	VkDbgNameObj( drawVisibilityBuff.hndl, dc.device, "Buff_Draw_Vis" );


	screenspaceBoxBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( dbg_vertex ) * 8,
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
												  vkRscArena );
	VkDbgNameObj( screenspaceBoxBuff.hndl, dc.device, "Buff_Screenspace_Boxes" );

	visibleInstsBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * 3 * sizeof( u32 ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
												vkRscArena );
	visibleMeshletsBuff = VkCreateAllocBindBuffer( 10 * MB,//std::size( mletView ) * 3 * sizeof( u32 ),
												   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
												   vkRscArena );

	meshletIdBuff = VkCreateAllocBindBuffer( 10'000 * sizeof( u64 ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkRscArena );
	VkDbgNameObj( meshletIdBuff.hndl, dc.device, "Buff_Meshlet_Dispatch_IDs" );


	indirectMergedIndexBuff = VkCreateAllocBindBuffer( 
		10 * MB,
		//std::size( instDesc ) * fileDesc.idxRange.size, 
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		vkRscArena );

	drawCmdAabbsBuff = VkCreateAllocBindBuffer( 10'000 * sizeof( draw_indirect ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
												vkRscArena );


	drawMergedCmd = VkCreateAllocBindBuffer(
		sizeof( draw_command ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena );

	// NOTE: create and texture uploads
	std::vector<VkImageMemoryBarrier2KHR> imageBarriers;
	{
		imageBarriers.reserve( std::size( imgDesc ) );
		
		u64 newTexturesOffset = std::size( textures.rsc );

		for( const image_metadata& meta : imgDesc )
		{
			VkImageCreateInfo info = VkGetImageInfoFromMetadata( meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
			image img = VkCreateAllocBindImage( info, vkAlbumArena );

			imageBarriers.push_back( VkMakeImageBarrier2(
				img.hndl,
				0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );


			hndl32<image> hImg = PushResourceToContainer( img, textures );
		}

		VkDependencyInfoKHR imitImagesDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		imitImagesDependency.imageMemoryBarrierCount = std::size( imageBarriers );
		imitImagesDependency.pImageMemoryBarriers = std::data( imageBarriers );
		vkCmdPipelineBarrier2KHR( cmdBuff, &imitImagesDependency );

		imageBarriers.resize( 0 );

		const u8* pTexBinData = std::data( binaryData ) + fileFooter.texBinByteRange.offset;

		buffer_data stagingBuff = VkCreateAllocBindBuffer( 
			fileFooter.texBinByteRange.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuff.hostVisible, pTexBinData, stagingBuff.size );
		StagingManagerPushForRecycle( stagingBuff.hndl, stagingManager, currentFrameId );

		for( u64 i = 0; i < std::size( imgDesc ); ++i )
		{
			const image& dst = textures.rsc[ i + newTexturesOffset ].data;

			VkBufferImageCopy imgCopyRegion = {};
			imgCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imgCopyRegion.bufferOffset = imgDesc[ i ].texBinRange.offset;
			imgCopyRegion.imageSubresource.mipLevel = 0;
			imgCopyRegion.imageSubresource.baseArrayLayer = 0;
			imgCopyRegion.imageSubresource.layerCount = 1;
			imgCopyRegion.imageOffset = VkOffset3D{};
			imgCopyRegion.imageExtent = { u32( dst.width ),u32( dst.height ),1 };
			vkCmdCopyBufferToImage( cmdBuff, stagingBuff.hndl, dst.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );

			imageBarriers.push_back( VkMakeImageBarrier2(
				dst.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
				VK_ACCESS_2_SHADER_READ_BIT_KHR,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}
	}

	VkDependencyInfoKHR uploadDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	uploadDependency.bufferMemoryBarrierCount = std::size( buffBarriers );
	uploadDependency.pBufferMemoryBarriers = std::data( buffBarriers );
	uploadDependency.imageMemoryBarrierCount = std::size( imageBarriers );
	uploadDependency.pImageMemoryBarriers = std::data( imageBarriers );
	vkCmdPipelineBarrier2KHR( cmdBuff, &uploadDependency );
}

static buffer_data drawCountBuff;
static buffer_data drawCountDbgBuff;

static buffer_data avgLumBuff;

static buffer_data shaderGlobalsBuff;
static buffer_data shaderGlobalSyncCounterBuff;

static buffer_data depthAtomicCounterBuff;

static buffer_data atomicCounterBuff;

static buffer_data bdasUboBuff;

static buffer_data dispatchCmdBuff0;
static buffer_data dispatchCmdBuff1;

static buffer_data meshletCountBuff;

static buffer_data mergedIndexCountBuff;

static buffer_data drawMergedCountBuff;
// TODO:
inline static void VkInitInternalBuffers()
{
	avgLumBuff = VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, vkRscArena );
	VkDbgNameObj( avgLumBuff.hndl, dc.device, "Buff_AvgLum" );

	shaderGlobalsBuff = VkCreateAllocBindBuffer( 64,
												 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												 vkRscArena );

	shaderGlobalSyncCounterBuff = VkCreateAllocBindBuffer( 4,
														   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
														   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
														   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
														   vkRscArena );

	drawCountBuff = VkCreateAllocBindBuffer( 4,
											 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
											 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											 vkRscArena );
	VkDbgNameObj( drawCountBuff.hndl, dc.device, "Buff_Draw_Count" );

	drawCountDbgBuff = VkCreateAllocBindBuffer( 4, 
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
												vkRscArena );
	VkDbgNameObj( drawCountDbgBuff.hndl, dc.device, "Buff_Dbg_Draw_Count" );
	// TODO: no transfer bit ?
	depthAtomicCounterBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );

	dispatchCmdBuff0 = VkCreateAllocBindBuffer( sizeof( dispatch_command ), 
											   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
											   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
											   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											   vkRscArena );
	dispatchCmdBuff1 = VkCreateAllocBindBuffer( sizeof( dispatch_command ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												vkRscArena );
	
	bdasUboBuff = VkCreateAllocBindBuffer( sizeof( global_bdas ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, vkHostComArena );

	meshletCountBuff = 
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
	VkDbgNameObj( meshletCountBuff.hndl, dc.device, "Buff_Mlet_Dispatch_Count" );

	atomicCounterBuff = VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
	VkDbgNameObj( atomicCounterBuff.hndl, dc.device, "Buff_Atomic_Counter" );

	mergedIndexCountBuff = 
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );

	drawMergedCountBuff = VkCreateAllocBindBuffer( 
		4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
	VkDbgNameObj( drawMergedCountBuff.hndl, dc.device, "Buff_Draw_Merged_Count" );
}

// TODO: move out of global/static
static vk_program	gfxMergedProgram = {};
static vk_program	gfxOpaqueProgram = {};
static vk_program	gfxMeshletProgram = {};
static vk_program	cullCompProgram = {};
static vk_program	depthPyramidCompProgram = {};
static vk_program	avgLumCompProgram = {};
static vk_program	tonemapCompProgram = {};
static vk_program	depthPyramidMultiProgram = {};
static vk_program   expanderCompProgram = {};
static vk_program   clusterCullCompProgram = {};
static vk_program   expMergeCompProgram = {};

static vk_program   dbgDrawProgram = {};
static VkPipeline   gfxDrawIndirDbg = {};

static vk_program   zPrepassProgram = {};
static VkPipeline   gfxZPrepass = {};
static VkRenderPass zRndPass = {};
static VkRenderPass depthReadRndPass = {};


static vk_pipeline  lighCullProgam = {};

static VkDescriptorPool vkDescPool = {};
static VkDescriptorSet frameDesc[ 3 ] = {};
// TODO: no structured binding
void VkBackendInit()
{
	auto [vkInst, vkDbgMsg] = VkMakeInstance();

	vkSurf = VkMakeWinSurface( vkInst, hInst, hWnd );
	dc = VkMakeDeviceContext( vkInst, vkSurf );

	VkStartGfxMemory( dc.gpu, dc.device );

	sc = VkMakeSwapchain( dc.device, dc.gpu, vkSurf, dc.gfxQueueIdx, VK_FORMAT_B8G8R8A8_UNORM );

	VkInitInternalBuffers();

	for( u64 vfi = 0; vfi < rndCtx.framesInFlight; ++vfi )
	{
		// NOTE: push desc doesn't like bigger sizes ?
		rndCtx.vrtFrames[ vfi ] = VkCreateVirtualFrame( dc.device, dc.gfxQueueIdx, u16( -1 ), vkHostComArena );
	}
	VkSemaphoreTypeCreateInfo timelineInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
	timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	timelineInfo.initialValue = rndCtx.vFrameIdx = 0;
	VkSemaphoreCreateInfo timelineSemaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineInfo };
	VK_CHECK( vkCreateSemaphore( dc.device, &timelineSemaInfo, 0, &rndCtx.timelineSema ) );


	rndCtx.quadMinSampler =
		VkMakeSampler( dc.device, VK_SAMPLER_REDUCTION_MODE_MIN, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE );

	// TODO: add into virtual frame
	vkGpuTimer[ 0 ] = VkMakeGpuTimer( dc.device, 1, dc.timestampPeriod );
	vkGpuTimer[ 1 ] = VkMakeGpuTimer( dc.device, 1, dc.timestampPeriod );


	VkDescriptorPoolSize sizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dc.gpuProps.limits.maxDescriptorSetStorageBuffers / 16 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, dc.gpuProps.limits.maxDescriptorSetUniformBuffers },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, dc.gpuProps.limits.maxDescriptorSetSampledImages / 16 },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, std::min( 4u, dc.gpuProps.limits.maxDescriptorSetSamplers ) },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, dc.gpuProps.limits.maxDescriptorSetStorageImages / 16 }
	};

	VkDescriptorPoolCreateInfo descPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	descPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	descPoolInfo.maxSets = 4;
	descPoolInfo.poolSizeCount = std::size( sizes );
	descPoolInfo.pPoolSizes = sizes;
	VK_CHECK( vkCreateDescriptorPool( dc.device, &descPoolInfo, 0, &vkDescPool ) );

	vk_binding_list bindingList = {
		{VK_GLOBAL_SLOT_UNIFORM_BUFFER, 8},
		{VK_GLOBAL_SLOT_SAMPLED_IMAGE, 1024},
		{VK_GLOBAL_SLOT_SAMPLER, 2}
	};
	globBindlessDesc = VkMakeBindlessGlobalDescriptor( dc.device, vkDescPool, bindingList );

	
	// TODO: separate resources into slots ?
	vk_slot_list bindlessSlots = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, 256 }, 
		// TODO: place mtrls and lights into uniforms ?
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, 256 },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 2, 4 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3, 128 },
	};

	// TODO: must keep for the lifetime of the prog
	VkDescriptorSetLayout frameDescLayout = VkMakeDescriptorSetLayout( dc.device, { {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, 1} } );
	VkDescriptorSetLayout bindlessLayout = VkMakeDescriptorSetLayout( dc.device, bindlessSlots );
	VkDescriptorSetLayout allocDescLayouts[] = { frameDescLayout, frameDescLayout, bindlessLayout };

	VkDescriptorSetAllocateInfo descSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descSetInfo.descriptorPool = vkDescPool;
	descSetInfo.descriptorSetCount = std::size( allocDescLayouts );
	descSetInfo.pSetLayouts = allocDescLayouts;
	VK_CHECK( vkAllocateDescriptorSets( dc.device, &descSetInfo, frameDesc ) );


	rndCtx.renderPass = VkMakeRenderPass( dc.device, 0, 1, 1, 1, rndCtx.desiredDepthFormat, rndCtx.desiredColorFormat );
	rndCtx.render2ndPass = VkMakeRenderPass( dc.device, 0, 1, 0, 0, rndCtx.desiredDepthFormat, rndCtx.desiredColorFormat );
	zRndPass = VkMakeRenderPass( dc.device, 0, -1, 1, -1, rndCtx.desiredDepthFormat, VkFormat( 0 ) );
	depthReadRndPass = VkMakeRenderPass( dc.device, 0, -1, 0, -1, rndCtx.desiredDepthFormat, VkFormat( 0 ) );


	// TODO: remove .type. from shader use type prefix instead
	{
		vk_shader vertZPre = VkLoadShader( "Shaders/v_z_prepass.vert.spv", dc.device );

		vk_gfx_pipeline_state zPrepass = {};
		zPrepass.conservativeRasterEnable = true;
		zPrepassProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertZPre } );
		gfxZPrepass = VkMakeGfxPipeline( dc.device, 0, zRndPass, zPrepassProgram.pipeLayout, vertZPre.module, 0, zPrepass );

		vkDestroyShaderModule( dc.device, vertZPre.module, 0 );
	}
	{
		vk_shader vertBox = VkLoadShader( "Shaders/box_meshlet_draw.vert.spv", dc.device );
		vk_shader normalCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );

		vk_gfx_pipeline_state lineDrawPipelineState = {};
		lineDrawPipelineState.blendCol = VK_FALSE;
		lineDrawPipelineState.depthWrite = VK_FALSE;
		lineDrawPipelineState.depthTestEnable = VK_FALSE;
		lineDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;
		lineDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		lineDrawPipelineState.polyMode = VK_POLYGON_MODE_LINE;
		lineDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

		dbgDrawProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertBox, &normalCol } );
		gfxDrawIndirDbg = VkMakeGfxPipeline(
			dc.device, 0, rndCtx.renderPass, dbgDrawProgram.pipeLayout, vertBox.module, normalCol.module, lineDrawPipelineState );

		vkDestroyShaderModule( dc.device, vertBox.module, 0 );
		vkDestroyShaderModule( dc.device, normalCol.module, 0 );
	}
	{
		vk_shader drawCull = VkLoadShader( "Shaders/draw_cull.comp.spv", dc.device );
		cullCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &drawCull } );
		rndCtx.compPipeline = VkMakeComputePipeline( dc.device, 0, cullCompProgram.pipeLayout, drawCull.module, { OBJ_CULL_WORKSIZE } );
		VkDbgNameObj( rndCtx.compPipeline, dc.device, "Pipeline_Comp_DrawCull" );

		vk_shader expansionComp = VkLoadShader( "Shaders/id_expander.comp.spv", dc.device );
		expanderCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &expansionComp } );
		rndCtx.compExpanderPipe = VkMakeComputePipeline( dc.device, 0, expanderCompProgram.pipeLayout, expansionComp.module, {} );
		VkDbgNameObj( rndCtx.compExpanderPipe, dc.device, "Pipeline_Comp_iD_Expander" );

		vk_shader clusterCull = VkLoadShader( "Shaders/c_meshlet_cull.comp.spv", dc.device );
		clusterCullCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &clusterCull } );
		rndCtx.compClusterCullPipe = VkMakeComputePipeline( dc.device, 0, clusterCullCompProgram.pipeLayout, clusterCull.module, {} );
		VkDbgNameObj( rndCtx.compClusterCullPipe, dc.device, "Pipeline_Comp_ClusterCull" );

		vk_shader expMerge = VkLoadShader( "Shaders/comp_expand_merge.comp.spv", dc.device );
		expMergeCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &expMerge } );
		rndCtx.compExpMergePipe = VkMakeComputePipeline( dc.device, 0, expMergeCompProgram.pipeLayout, expMerge.module, {} );
		VkDbgNameObj( rndCtx.compExpMergePipe, dc.device, "Pipeline_Comp_ExpMerge" );

		vkDestroyShaderModule( dc.device, drawCull.module, 0 );
		vkDestroyShaderModule( dc.device, expansionComp.module, 0 );
		vkDestroyShaderModule( dc.device, clusterCull.module, 0 );
		vkDestroyShaderModule( dc.device, expMerge.module, 0 );
	}
	{
		vk_shader vertPBR = VkLoadShader( "Shaders/shdr.vert.spv", dc.device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", dc.device );
		vk_gfx_pipeline_state opaqueState = {};
		gfxOpaqueProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertPBR, &fragPBR } );
		rndCtx.gfxPipeline =
			VkMakeGfxPipeline( dc.device, 0, rndCtx.renderPass, gfxOpaqueProgram.pipeLayout, vertPBR.module, fragPBR.module, opaqueState );
		VkDbgNameObj( rndCtx.gfxPipeline, dc.device, "Pipeline_Gfx_Opaque" );

		vkDestroyShaderModule( dc.device, vertPBR.module, 0 );
		vkDestroyShaderModule( dc.device, fragPBR.module, 0 );
	}
	{
		vk_shader vtxMerged = VkLoadShader( "Shaders/vtx_merged.vert.spv", dc.device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", dc.device );
		vk_gfx_pipeline_state opaqueState = {};
		//gfxMergedProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vtxMerged, &fragPBR } );

		VkDescriptorSetLayout setLayouts[] = { frameDescLayout, globBindlessDesc.setLayout };

		VkPushConstantRange pushConstRanges[ 1 ] = { VK_SHADER_STAGE_VERTEX_BIT,0,3 * sizeof( u64 ) };

		VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipeLayoutInfo.setLayoutCount = std::size( setLayouts );
		pipeLayoutInfo.pSetLayouts = setLayouts;
		pipeLayoutInfo.pushConstantRangeCount = std::size( pushConstRanges );
		pipeLayoutInfo.pPushConstantRanges = pushConstRanges;
		VK_CHECK( vkCreatePipelineLayout( dc.device, &pipeLayoutInfo, 0, &gfxMergedProgram.pipeLayout ) );


		rndCtx.gfxMergedPipeline = VkMakeGfxPipeline(
			dc.device, 0, rndCtx.renderPass, gfxMergedProgram.pipeLayout, vtxMerged.module, fragPBR.module, opaqueState );
		VkDbgNameObj( rndCtx.gfxMergedPipeline, dc.device, "Pipeline_Gfx_Merged" );

		vkDestroyShaderModule( dc.device, vtxMerged.module, 0 );
		vkDestroyShaderModule( dc.device, fragPBR.module, 0 );
	}
	{
		vk_shader vertMeshlet = VkLoadShader( "Shaders/meshlet.vert.spv", dc.device );
		vk_shader fragCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );
		vk_gfx_pipeline_state meshletState = {};
		gfxMeshletProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertMeshlet, &fragCol } );
		rndCtx.gfxMeshletPipeline = VkMakeGfxPipeline(
			dc.device, 0, rndCtx.renderPass, gfxMeshletProgram.pipeLayout, vertMeshlet.module, fragCol.module, meshletState );
		VkDbgNameObj( rndCtx.gfxMeshletPipeline, dc.device, "Pipeline_Gfx_MeshletDraw" );

		vkDestroyShaderModule( dc.device, vertMeshlet.module, 0 );
		vkDestroyShaderModule( dc.device, fragCol.module, 0 );
	}
	{
		vk_shader avgLum = VkLoadShader( "Shaders/avg_luminance.comp.spv", dc.device );
		avgLumCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &avgLum } );
		rndCtx.compAvgLumPipe =
			VkMakeComputePipeline( dc.device, 0, avgLumCompProgram.pipeLayout, avgLum.module, { dc.waveSize } );
		VkDbgNameObj( rndCtx.compAvgLumPipe, dc.device, "Pipeline_Comp_AvgLum" );

		vk_shader toneMapper = VkLoadShader( "Shaders/tonemap_gamma.comp.spv", dc.device );
		tonemapCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &toneMapper } );
		rndCtx.compTonemapPipe = VkMakeComputePipeline( dc.device, 0, tonemapCompProgram.pipeLayout, toneMapper.module, {} );
		VkDbgNameObj( rndCtx.compTonemapPipe, dc.device, "Pipeline_Comp_Tonemap" );

		vkDestroyShaderModule( dc.device, avgLum.module, 0 );
		vkDestroyShaderModule( dc.device, toneMapper.module, 0 );
	}
	{
		static_assert( multiPassDepthPyramid );
		vk_shader downsampler = VkLoadShader( "Shaders/pow2_downsampler.comp.spv", dc.device );
		depthPyramidMultiProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &downsampler } );
		rndCtx.compHiZPipeline = VkMakeComputePipeline( dc.device, 0, depthPyramidMultiProgram.pipeLayout, downsampler.module, {} );
		VkDbgNameObj( rndCtx.compHiZPipeline, dc.device, "Pipeline_Comp_DepthPyr" );

		vkDestroyShaderModule( dc.device, downsampler.module, 0 );
	}

	{
		std::vector<u8> vtxSpv = SysReadFile( "Shaders/v_light_cull.vert.spv" );
		std::vector<u8> fragSpv = SysReadFile( "Shaders/f_light_cull.frag.spv" );

		VkShaderModule vtx = VkMakeShaderModule( dc.device, ( const u32* ) std::data( vtxSpv ), std::size( vtxSpv ) );
		VkShaderModule frag = VkMakeShaderModule( dc.device, ( const u32* ) std::data( fragSpv ), std::size( fragSpv ) );

		VkPipelineLayout layout = {};

		VkPushConstantRange pushConstRanges[ 2 ] = {};
		pushConstRanges[ 0 ] = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( mat4 ) + 2 * sizeof( u64 ) };
		pushConstRanges[ 1 ] = { VK_SHADER_STAGE_FRAGMENT_BIT, pushConstRanges[ 0 ].size, sizeof( u64 ) + 4 * sizeof( u32 ) };
		VkPipelineLayoutCreateInfo pipelineLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
		pipelineLayoutInfo.pushConstantRangeCount = std::size( pushConstRanges );
		pipelineLayoutInfo.pPushConstantRanges = pushConstRanges;
		VK_CHECK( vkCreatePipelineLayout( dc.device, &pipelineLayoutInfo, 0, &layout ) );

		vk_gfx_pipeline_state state = { .conservativeRasterEnable = true, .depthWrite = false, .blendCol = false };
		VkPipeline pipeline = VkMakeGfxPipeline( dc.device, 0, depthReadRndPass, layout, vtx, frag, state );

		vk_pipeline program = { pipeline, layout };

		lighCullProgam = program;

		vkDestroyShaderModule( dc.device, vtx, 0 );
		vkDestroyShaderModule( dc.device, frag, 0 );
	}

	vkDbgCtx = VkMakeDebugContext( dc.device, rndCtx.renderPass, dc.gpuProps );

	imguiVkCtx = ImguiMakeVkContext( dc.device, dc.gpuProps, VK_FORMAT_B8G8R8A8_UNORM );

}


inline u64 VkGetGroupCount( u64 invocationCount, u64 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}

// TODO: math_uitl file
inline u64 FloorPowOf2( u64 size )
{
	// NOTE: use Hacker's Delight for bit-tickery
	constexpr u64 ONE_LEFT_MOST = u64( 1ULL << ( sizeof( u64 ) * 8 - 1 ) );
	return ( size ) ? ONE_LEFT_MOST >> __lzcnt64( size ) : 0;
}
inline u64 GetImgMipCountForPow2( u64 width, u64 height )
{
	// NOTE: log2 == position of the highest bit set (or most significant bit set, MSB)
	// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogObvious
	constexpr u64 TYPE_BIT_COUNT = sizeof( u64 ) * 8 - 1;

	u64 maxDim = std::max( width, height );
	assert( IsPowOf2( maxDim ) );
	u64 log2MaxDim = TYPE_BIT_COUNT - __lzcnt64( maxDim );

	return std::min( log2MaxDim, MAX_MIP_LEVELS );
}
inline u64 GetImgMipCount( u64 width, u64 height )
{
	assert( width && height );
	u64 maxDim = std::max( width, height );

	return std::min( (u64) floor( log2( maxDim ) ), MAX_MIP_LEVELS );
}

inline void VkDebugSyncBarrierEverything( VkCommandBuffer cmdBuff )
{
	VkMemoryBarrier2KHR everythingBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
	everythingBarrier.srcStageMask = everythingBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
	everythingBarrier.srcAccessMask = everythingBarrier.dstAccessMask = 
		VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
	
	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &everythingBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );
}

// TODO: visibility buffer ( not the VBuffer ) check Aaltonen
// TODO: revisit triangle culling ?
// TODO: optimize expansion shader
// TODO: "supply line" descriptor instead of push
// TODO: meshlet cone culling 
// TODO: must pass vertexOffset around somehow
inline static void 
CullPass( 
	VkCommandBuffer			cmdBuff, 
	VkPipeline				vkPipeline, 
	const vk_program&		program,
	const image&			depthPyramid,
	const VkSampler&		minQuadSampler,
	const buffer_data&      frameData
){
	// NOTE: wtf Vulkan ?
	constexpr u64 VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;

	vk_label label = { cmdBuff,"Cull Pass",{} };

	
	vkCmdFillBuffer( cmdBuff, drawCountBuff.hndl, 0, drawCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, drawCountDbgBuff.hndl, 0, drawCountDbgBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, meshletCountBuff.hndl, 0, meshletCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, mergedIndexCountBuff.hndl, 0, mergedIndexCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, drawMergedCountBuff.hndl, 0, drawMergedCountBuff.size, 0u );

	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff0.hndl, 0, dispatchCmdBuff0.size, 0u );
	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff1.hndl, 0, dispatchCmdBuff1.size, 0u );
	
	VkBufferMemoryBarrier2KHR beginCullBarriers[] = {
		VkMakeBufferBarrier2( 
			drawCmdBuff.hndl,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

	    VkMakeBufferBarrier2( 
			meshletCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountDbgBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			mergedIndexCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

		VkMakeBufferBarrier2( 
			dispatchCmdBuff0.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			dispatchCmdBuff1.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		
		VkMakeBufferBarrier2(
			indirectMergedIndexBuff.hndl,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR )
	};

	VkImageMemoryBarrier2KHR hiZReadBarrier = VkMakeImageBarrier2(
		rndCtx.depthPyramid.hndl,
		VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_ACCESS_2_SHADER_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT );

	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.bufferMemoryBarrierCount = std::size( beginCullBarriers );
	dependency.pBufferMemoryBarriers = beginCullBarriers;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &hiZReadBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );

	VkDescriptorImageInfo depthPyramidInfo = { minQuadSampler, depthPyramid.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	
	{
		vk_descriptor_info pushDescs[] = {
			Descriptor( frameData ),
			Descriptor( visibleInstsBuff ),
			depthPyramidInfo,
			Descriptor( atomicCounterBuff ),
			Descriptor( dispatchCmdBuff0 ),
			Descriptor( drawCountBuff ),
			Descriptor( drawCmdBuff )
		};

		u32 instCount = instDescBuff.size / sizeof( instance_desc );
		struct { u64 instDescAddr; u64 meshDescAddr; u32 instCount; } pushConst = {
			instDescBuff.devicePointer, meshBuff.devicePointer, instCount };

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );
		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, pushDescs );
		vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatch( cmdBuff, VkGetGroupCount( instCount, program.groupSize.x ), 1, 1 );
	}
	
	{
		VkBufferMemoryBarrier2KHR dispatchBarrier =
			VkMakeBufferBarrier2( dispatchCmdBuff0.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH );

		// TODO: write to read and write to write separately ?
		VkMemoryBarrier2KHR writeToReadWriteBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
		writeToReadWriteBarrier.srcStageMask = writeToReadWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
		writeToReadWriteBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
		writeToReadWriteBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

		VkDependencyInfoKHR execDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		execDependency.bufferMemoryBarrierCount = 1;
		execDependency.pBufferMemoryBarriers = &dispatchBarrier;
		execDependency.memoryBarrierCount = 1;
		execDependency.pMemoryBarriers = &writeToReadWriteBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &execDependency );



		vk_descriptor_info pushDesc[] = {
			Descriptor( visibleInstsBuff ),
			Descriptor( drawCountBuff ),
			Descriptor( meshletIdBuff ),
			Descriptor( meshletCountBuff ),
			Descriptor( atomicCounterBuff ),
			Descriptor( dispatchCmdBuff1 )
		};

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpanderPipe );
		vkCmdPushDescriptorSetWithTemplateKHR( 
			cmdBuff, expanderCompProgram.descUpdateTemplate, expanderCompProgram.pipeLayout, 0, pushDesc );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff0.hndl, 0 );
	}
	
	{
		VkBufferMemoryBarrier2KHR dispatchBarrier =
			VkMakeBufferBarrier2( dispatchCmdBuff1.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH );

		// TODO: write to read and write to write separately ?
		VkMemoryBarrier2KHR writeToReadWriteBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
		writeToReadWriteBarrier.srcStageMask = writeToReadWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
		writeToReadWriteBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
		writeToReadWriteBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

		VkDependencyInfoKHR execCullDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		execCullDependency.bufferMemoryBarrierCount = 1;
		execCullDependency.pBufferMemoryBarriers = &dispatchBarrier;
		execCullDependency.memoryBarrierCount = 1;
		execCullDependency.pMemoryBarriers = &writeToReadWriteBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &execCullDependency );



		vk_descriptor_info ccd[] = {
			Descriptor( frameData ),
			Descriptor( meshletIdBuff ),
			Descriptor( meshletCountBuff ),
			Descriptor( drawCountDbgBuff ),
			Descriptor( visibleMeshletsBuff ),
			depthPyramidInfo,
			Descriptor( atomicCounterBuff ),
			Descriptor( dispatchCmdBuff0 ),
			Descriptor( drawCmdAabbsBuff )
		};

		struct { u64 instDescAddr; u64 meshletDescAddr; } pushConst = { instDescBuff.devicePointer, meshletBuff.devicePointer };

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compClusterCullPipe );
		vkCmdPushDescriptorSetWithTemplateKHR(
			cmdBuff, clusterCullCompProgram.descUpdateTemplate, clusterCullCompProgram.pipeLayout, 0, ccd );
		vkCmdPushConstants( cmdBuff, clusterCullCompProgram.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff1.hndl, 0 );
	}
	
	{
		VkBufferMemoryBarrier2KHR dispatchBarrier =
			VkMakeBufferBarrier2( dispatchCmdBuff0.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH );

		// TODO: write to read and write to write separately ?
		VkMemoryBarrier2KHR writeToReadWriteBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
		writeToReadWriteBarrier.srcStageMask = writeToReadWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
		writeToReadWriteBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
		writeToReadWriteBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

		VkDependencyInfoKHR execTriExpDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		execTriExpDependency.bufferMemoryBarrierCount = 1;
		execTriExpDependency.pBufferMemoryBarriers = &dispatchBarrier;
		execTriExpDependency.memoryBarrierCount = 1;
		execTriExpDependency.pMemoryBarriers = &writeToReadWriteBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &execTriExpDependency );



		vk_descriptor_info pushDesc[] = {
			Descriptor( visibleMeshletsBuff ),
			Descriptor( drawCountDbgBuff ),
			Descriptor( indirectMergedIndexBuff ),
			Descriptor( mergedIndexCountBuff ),
			Descriptor( drawMergedCmd ),
			Descriptor( drawMergedCountBuff ),
			Descriptor( atomicCounterBuff )
		};

		struct { u64 meshletDataAddr; } pushConst = { meshletDataBuff.devicePointer };

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpMergePipe );
		vkCmdPushDescriptorSetWithTemplateKHR( 
			cmdBuff, expMergeCompProgram.descUpdateTemplate, expMergeCompProgram.pipeLayout, 0, pushDesc );
		vkCmdPushConstants( cmdBuff, expMergeCompProgram.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff0.hndl, 0 );
	}


	VkBufferMemoryBarrier2KHR endCullBarriers[] = {
		VkMakeBufferBarrier2( 
			drawCmdBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountBuff.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCmdDbgBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountDbgBuff.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCmdAabbsBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawMergedCmd.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeBufferBarrier2( 
			indirectMergedIndexBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ),
	};

	VkDependencyInfoKHR dependencyEnd = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyEnd.bufferMemoryBarrierCount = std::size( endCullBarriers );
	dependencyEnd.pBufferMemoryBarriers = endCullBarriers;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependencyEnd );
}

// TODO: pass more params
inline static void
CullRasterizeLightProxy(
	VkCommandBuffer cmdBuff,
	const vk_pipeline& program,
	VkRenderPass vkRndPass,
	VkFramebuffer fbo,
	const DirectX::XMFLOAT4X4A& viewProjMat
){
	vk_label label = { cmdBuff,"Gfx_Cull_Rast_Lights",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = fbo;
	rndPassBegInfo.renderArea = scissor;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );


	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeline );

	u64 tileMaxLightsLog2 = std::log2( lightCount );

	assert( proxyGeomBuff.devicePointer && lightsBuff.devicePointer && tileBuff.devicePointer );

	struct { mat4 viewProj; u64 geomAddr; u64 lightAddr; } vertPush = {
		viewProjMat, proxyGeomBuff.devicePointer, lightsBuff.devicePointer };
	vkCmdPushConstants( cmdBuff, program.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( vertPush ), &vertPush );

	struct { u64 tileBuffAddr; u32 tileSize; u32 tileRowLen; u32 tileWordCount; u32 tileMaxLightsLog2; } fragPush = {
		tileBuff.devicePointer, tileSize, tileRowSize, wordsPerTile, tileMaxLightsLog2 };
	vkCmdPushConstants( cmdBuff, program.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof( vertPush ), sizeof( fragPush ), &fragPush );


	vkCmdBindIndexBuffer( cmdBuff, proxyIdxBuff.hndl, 0, VK_INDEX_TYPE_UINT32 );
	vkCmdDrawIndexed( cmdBuff, proxyIdxBuff.size / sizeof( u32 ), 1, 0, 0, 0 );


	vkCmdEndRenderPass( cmdBuff );
}

// TODO: redesign
inline static void
DrawIndexedIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&		drawCmds,
	const buffer_data&		camData,
	VkBuffer				drawCmdCount,
	VkBuffer                indexBuff,
	VkIndexType             indexType,
	u32                     maxDrawCallCount,
	const VkClearValue*		clearVals,
	const vk_program&		program,
	bool                    fullPass
){
	vk_label label = { cmdBuff,"Draw Indexed Indirect Pass",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;
	rndPassBegInfo.clearValueCount = clearVals ? 2 : 0;
	rndPassBegInfo.pClearValues = clearVals;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );
	
	if( fullPass )
	{
		vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );
	}

	struct { u64 vtxAddr, transfAddr, drawCmdAddr, camAddr; } push = {
		globVertexBuff.devicePointer, instDescBuff.devicePointer, drawCmds.devicePointer, camData.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push ), &push );


	vkCmdBindIndexBuffer( cmdBuff, indexBuff, 0, indexType );

	vkCmdDrawIndexedIndirectCount( 
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCmdCount, 0, maxDrawCallCount, sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}

// TODO: sexier
inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&      drawCmds,
	VkBuffer				drawCmdCount,
	const vk_program&       program,
	const mat4&             viewProjMat
){
	vk_label label = { cmdBuff,"Draw Indirect Pass",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	//vk_descriptor_info descriptors[] = { Descriptor( drawCmds ), Descriptor( instDescBuff ), Descriptor( meshletBuff ) };
	//vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

	//struct { mat4 viewProj; vec4 color; } push = { viewProjMat, { 255,0,0,0 } };
	struct { mat4 viewProj; vec4 color; u64 cmdAddr; u64 transfAddr; u64 meshletAddr; } push = {
		viewProjMat, { 255,0,0,0 }, drawCmds.devicePointer, instDescBuff.devicePointer, meshletBuff.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( push ), &push );

	u32 maxDrawCnt = drawCmds.size / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCmdCount, 0, maxDrawCnt, sizeof( draw_indirect ) );

	vkCmdEndRenderPass( cmdBuff );
}

// TODO: adjust for more draws ?
inline static void
DrawIndexedIndirectMerged(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&      indexBuff,
	const buffer_data&      drawCmds,
	const buffer_data&      drawCount,
	const buffer_data&      camData,
	const VkClearValue*     clearVals,
	const vk_program&       program
){
	vk_label label = { cmdBuff,"Draw Indexed Indirect Merged Pass",{} };

	constexpr u32 maxDrawCount = 1;

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;
	rndPassBegInfo.clearValueCount = clearVals ? 2 : 0;
	rndPassBegInfo.pClearValues = clearVals;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );
	
	struct { u64 vtxAddr, transfAddr, camAddr; } push = {
		globVertexBuff.devicePointer, instDescBuff.devicePointer, camData.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push ), &push );

	vkCmdBindIndexBuffer( cmdBuff, indexBuff.hndl, 0, VK_INDEX_TYPE_UINT32 );

	vkCmdDrawIndexedIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}


// TODO: must remake single pass
inline static void
DepthPyramidPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	u64						mipLevelsCount,
	VkSampler				quadMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const vk_program&		program 
){
	assert( 0 );
	u32 dispatchGroupX = ( ( depthTarget.width + 63 ) >> 6 );
	u32 dispatchGroupY = ( ( depthTarget.height + 63 ) >> 6 );

	downsample_info dsInfo = {};
	dsInfo.mips = mipLevelsCount;
	dsInfo.invRes.x = 1.0f / float( depthTarget.width );
	dsInfo.invRes.y = 1.0f / float( depthTarget.height );
	dsInfo.workGroupCount = dispatchGroupX * dispatchGroupY;


	VkImageMemoryBarrier depthReadBarrier = VkMakeImgBarrier( depthTarget.hndl,
															  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
															  VK_ACCESS_SHADER_READ_BIT,
															  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
															  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															  VK_IMAGE_ASPECT_DEPTH_BIT,
															  0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,  
						  1, &depthReadBarrier );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	std::vector<vk_descriptor_info> depthPyramidDescs( MAX_MIP_LEVELS + 3 );
	depthPyramidDescs[ 0 ] = { 0, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };;
	depthPyramidDescs[ 1 ] = { quadMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 2 ] = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	for( u64 i = 0; i < rndCtx.depthPyramid.mipCount; ++i )
	{
		depthPyramidDescs[ i + 3 ] = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
	}

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, std::data( depthPyramidDescs ) );

	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( dsInfo ), &dsInfo );

	vkCmdDispatch( cmdBuff, dispatchGroupX, dispatchGroupY, 1 );

	VkImageMemoryBarrier depthWriteBarrier = VkMakeImgBarrier( depthTarget.hndl,
															   VK_ACCESS_SHADER_READ_BIT,
															   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
															   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
															   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
															   VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
						  1, &depthWriteBarrier );
}


inline static void
DepthPyramidMultiPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkSampler				pointMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const image&			depthPyramid,
	const vk_program&		program 
){
	vk_label label = { cmdBuff,"HiZ Multi Pass",{} };

	VkImageMemoryBarrier2KHR hizBeginBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR,
			// TODO: all gfx commands ?
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2( 
			depthPyramid.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.imageMemoryBarrierCount = std::size( hizBeginBarriers );
	dependency.pImageMemoryBarriers = hizBeginBarriers;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );


	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	VkDescriptorImageInfo sourceDepth = { pointMinSampler, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };



	VkMemoryBarrier2KHR executionBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
	executionBarrier.srcStageMask = executionBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
	executionBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR;
	executionBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
	{
		if( i != 0 ) sourceDepth = { pointMinSampler, depthMips[ i - 1 ], VK_IMAGE_LAYOUT_GENERAL };

		VkDescriptorImageInfo destDepth = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
		vk_descriptor_info descriptors[] = { destDepth, sourceDepth };

		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

		u32 levelWidth = std::max( 1u, u32( depthPyramid.width ) >> i );
		u32 levelHeight = std::max( 1u, u32( depthPyramid.height ) >> i );

		vec2 reduceData = {};
		reduceData.x = levelWidth;
		reduceData.y = levelHeight;

		vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( reduceData ), &reduceData );

		u32 dispatchX = VkGetGroupCount( levelWidth, program.groupSize.x );
		u32 dispatchY = VkGetGroupCount( levelHeight, program.groupSize.y );
		vkCmdDispatch( cmdBuff, dispatchX, dispatchY, 1 );

		// TODO: use memory barriers ?
		VkImageMemoryBarrier2KHR reduceBarrier =
			VkMakeImageBarrier2( depthPyramid.hndl,
								 VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 VK_ACCESS_2_SHADER_READ_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_ASPECT_COLOR_BIT );

		VkDependencyInfoKHR passDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		//passDependency.imageMemoryBarrierCount = 1;
		//passDependency.pImageMemoryBarriers = &reduceBarrier;
		passDependency.memoryBarrierCount = 1;
		passDependency.pMemoryBarriers = &executionBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &passDependency );

	}

	// TODO: do we need ?
	VkImageMemoryBarrier2KHR hizEndBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2(
			depthPyramid.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkDependencyInfoKHR dependencyEnd = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyEnd.imageMemoryBarrierCount = std::size( hizEndBarriers );
	dependencyEnd.pImageMemoryBarriers = hizEndBarriers;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependencyEnd );
}

// TODO: optimize
inline static void
AverageLuminancePass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			avgPipe,
	const vk_program&   avgProg,
	const image&        fboHdrColTrg,
	float				dt
){
	vk_label label = { cmdBuff,"Averge Lum Pass",{} };
	// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
	avg_luminance_info avgLumInfo = {};
	avgLumInfo.minLogLum = -10.0f;
	avgLumInfo.invLogLumRange = 1.0f / 12.0f;
	avgLumInfo.dt = dt;

	vkCmdFillBuffer( cmdBuff, shaderGlobalsBuff.hndl, 0, shaderGlobalsBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, shaderGlobalSyncCounterBuff.hndl, 0, shaderGlobalSyncCounterBuff.size, 0u );

	VkBufferMemoryBarrier2KHR zeroInitGlobals[] = {
		VkMakeBufferBarrier2( shaderGlobalsBuff.hndl,
								VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
								VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( shaderGlobalSyncCounterBuff.hndl,
								VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
								VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR )
	};
	VkImageMemoryBarrier2KHR hrdColTargetAcquire = VkMakeImageBarrier2( fboHdrColTrg.hndl,
																		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
																		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
																		VK_ACCESS_2_SHADER_READ_BIT_KHR,
																		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
																		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
																		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
																		VK_IMAGE_ASPECT_COLOR_BIT );

	VkDependencyInfoKHR dependencyAcquire = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyAcquire.imageMemoryBarrierCount = 1;
	dependencyAcquire.pImageMemoryBarriers = &hrdColTargetAcquire;
	dependencyAcquire.bufferMemoryBarrierCount = std::size( zeroInitGlobals );
	dependencyAcquire.pBufferMemoryBarriers = zeroInitGlobals;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependencyAcquire );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, avgPipe );

	vk_descriptor_info avgLumDescs[] = {
		{ 0, fboHdrColTrg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		Descriptor( avgLumBuff ),
		Descriptor( shaderGlobalsBuff ),
		Descriptor( shaderGlobalSyncCounterBuff )
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, avgProg.descUpdateTemplate, avgProg.pipeLayout, 0, &avgLumDescs[ 0 ] );

	vkCmdPushConstants( cmdBuff, avgProg.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( avgLumInfo ), &avgLumInfo );

	vkCmdDispatch( cmdBuff,
				   VkGetGroupCount( fboHdrColTrg.width, avgProg.groupSize.x ),
				   VkGetGroupCount( fboHdrColTrg.height, avgProg.groupSize.y ), 1 );
}

// TODO: optimize
inline static void
FinalCompositionPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			tonePipe,
	const image&		fboHdrColTrg,
	const vk_program&	tonemapProg,
	VkImage				scImg,
	VkImageView			scView
){
	vk_label label = { cmdBuff,"Final Composition Pass",{} };
	
	VkImageMemoryBarrier2KHR scWriteBarrier =
		VkMakeImageBarrier2( scImg,
							 0, 0,
							 VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
							 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
							 VK_IMAGE_LAYOUT_UNDEFINED,
							 VK_IMAGE_LAYOUT_GENERAL,
							 VK_IMAGE_ASPECT_COLOR_BIT );

	VkBufferMemoryBarrier2KHR avgLumReadBarrier =
		VkMakeBufferBarrier2( avgLumBuff.hndl,
							  VK_ACCESS_2_SHADER_WRITE_BIT_KHR, 
							  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
							  VK_ACCESS_2_SHADER_READ_BIT_KHR, 
							  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR );

	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &avgLumReadBarrier;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &scWriteBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, tonePipe );

	vk_descriptor_info tonemapDescs[] = {
		{ 0, fboHdrColTrg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		{ 0, scView, VK_IMAGE_LAYOUT_GENERAL },
		Descriptor( avgLumBuff )
	};

	vkCmdPushDescriptorSetWithTemplateKHR( 
		cmdBuff, tonemapProg.descUpdateTemplate, tonemapProg.pipeLayout, 0, &tonemapDescs[ 0 ] );

	assert( ( fboHdrColTrg.width == sc.width ) && ( fboHdrColTrg.height == sc.height ) );
	vkCmdDispatch( cmdBuff,
				   VkGetGroupCount( fboHdrColTrg.width, tonemapProg.groupSize.x ),
				   VkGetGroupCount( fboHdrColTrg.height, tonemapProg.groupSize.y ), 1 );

}


// TODO: recycle framebuffers better
static std::vector<VkFramebuffer> recycleFboList;

// TODO: pass cam data via push const
void HostFrames( const frame_data& frameData, gpu_data& gpuData )
{
	using namespace DirectX;
	//u64 timestamp = SysGetFileTimestamp( "D:\\EichenRepos\\QiY\\QiY\\Shaders\\pbr.frag.glsl" );

	u64 currentFrameIdx = rndCtx.vFrameIdx++;
	u64 frameBufferedIdx = currentFrameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
	const virtual_frame& thisVFrame = rndCtx.vrtFrames[ frameBufferedIdx ];

	VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
	waitInfo.semaphoreCount = 1;
	waitInfo.pSemaphores = &rndCtx.timelineSema;
	waitInfo.pValues = &currentFrameIdx;
	VK_CHECK( VK_INTERNAL_ERROR( vkWaitSemaphores( dc.device, &waitInfo, UINT64_MAX ) > VK_TIMEOUT ) );

	VK_CHECK( vkResetCommandPool( dc.device, thisVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

	// TODO: don't wait on this ? place somewhere else ?
	u32 imgIdx;
	VK_CHECK( vkAcquireNextImageKHR( dc.device, sc.swapchain, UINT64_MAX, thisVFrame.canGetImgSema, 0, &imgIdx ) );

	// TODO: no copy
	global_data globs = {};
	globs.proj = frameData.proj;
	globs.mainView = frameData.mainView;
	globs.activeView = frameData.activeView;
	globs.worldPos = frameData.worldPos;
	globs.camViewDir = frameData.camViewDir;
	std::memcpy( thisVFrame.frameData.hostVisible, &globs, sizeof( globs ) );

	if( currentFrameIdx < 2 )
	{
		VkDescriptorBufferInfo uboInfo = { thisVFrame.frameData.hndl, 0, sizeof( globs ) };

		VkWriteDescriptorSet globalDataUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		globalDataUpdate.dstSet = frameDesc[ frameBufferedIdx ];
		globalDataUpdate.dstBinding = 0;
		globalDataUpdate.dstArrayElement = 0;
		globalDataUpdate.descriptorCount = 1;
		globalDataUpdate.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		globalDataUpdate.pBufferInfo = &uboInfo;
		vkUpdateDescriptorSets( dc.device, 1, &globalDataUpdate, 0, 0 );
	}

	VkCommandBufferBeginInfo cmdBufBegInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	cmdBufBegInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( thisVFrame.cmdBuff, &cmdBufBegInfo );
	
	// TODO: swapchain resize ?
	if( !rndCtx.depthTarget.hndl )
	{
		constexpr VkFormat format = VK_FORMAT_D32_SFLOAT;
		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		rndCtx.depthTarget = VkCreateAllocBindImage( format, usgFlags, { sc.width,sc.height,1 }, 1, vkAlbumArena );
		VkDbgNameObj( rndCtx.depthTarget.hndl, dc.device, "Img_Render_Target_Depth" );

		u32 hiZWidth = 0;
		u32 hiZHeight = 0;
		u32 hiZMipCount = 0;
		// TODO: place depth pyr elsewhere 
		if constexpr( !multiPassDepthPyramid )
		{
			// TODO: make conservative ?
			hiZWidth = ( sc.width ) / 2;
			hiZHeight = ( sc.height ) / 2;
			hiZMipCount = GetImgMipCount( hiZWidth, hiZHeight );
		}
		else
		{
			hiZWidth = FloorPowOf2( sc.width );
			hiZHeight = FloorPowOf2( sc.height );
			u32 squareDim = std::min( hiZWidth, hiZHeight );
			hiZWidth = hiZHeight = squareDim;
			hiZMipCount = GetImgMipCountForPow2( hiZWidth, hiZHeight );
		}
		VK_CHECK( VK_INTERNAL_ERROR( !( hiZMipCount < MAX_MIP_LEVELS ) ) );
		constexpr VkImageUsageFlags hiZUsg =
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		constexpr VkFormat depthFormat = VK_FORMAT_R32_SFLOAT;
		rndCtx.depthPyramid = VkCreateAllocBindImage( depthFormat, hiZUsg, { hiZWidth, hiZHeight, 1 }, hiZMipCount, vkAlbumArena );
		VkDbgNameObj( rndCtx.depthPyramid.hndl, dc.device, "Img_Depth_Pyramid" );

		for( u64 i = 0; i < rndCtx.depthPyramid.mipCount; ++i )
		{
			rndCtx.depthPyramidChain[ i ] =
				VkMakeImgView( dc.device, rndCtx.depthPyramid.hndl, rndCtx.depthPyramid.nativeFormat, i, 1 );
		}


		VkImageMemoryBarrier2KHR initBarriers[] = {
		VkMakeImageBarrier2(
			rndCtx.depthTarget.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),
		VkMakeImageBarrier2(
			rndCtx.depthPyramid.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
		};
		VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependency.imageMemoryBarrierCount = std::size( initBarriers );
		dependency.pImageMemoryBarriers = initBarriers;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );
	}
	if( !rndCtx.colorTarget.hndl )
	{
		constexpr VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		rndCtx.colorTarget = VkCreateAllocBindImage( format, usgFlags, { sc.width,sc.height,1 }, 1, vkAlbumArena );
		VkDbgNameObj( rndCtx.colorTarget.hndl, dc.device, "Img_Render_Target_Color" );

		VkImageMemoryBarrier2KHR initBarrier = VkMakeImageBarrier2(
			rndCtx.colorTarget.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT );
		VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &initBarrier;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );
	}

	// TODO: creating too many fboss per frame recording might have some perf impact
	// TODO: if perf impact then store inside a hashmap/cache map
	// TODO: deffer delete these
	VkImageView attachments[] = { rndCtx.depthTarget.view,rndCtx.colorTarget.view };
	u32 fboWidth = rndCtx.depthTarget.width;
	u32 fboHeight = rndCtx.depthTarget.height;
	assert( rndCtx.depthTarget.width == rndCtx.colorTarget.width );
	assert( rndCtx.depthTarget.height == rndCtx.colorTarget.height );
	// TODO: re design
	if( std::size( recycleFboList ) )
	{
		for( VkFramebuffer& fbo : recycleFboList )
		{
			vkDestroyFramebuffer( dc.device, fbo, 0 );
		}
		recycleFboList.resize( 0 );
	}

	VkFramebuffer depthFbo = VkMakeFramebuffer( dc.device, zRndPass, attachments, 1, fboWidth, fboHeight );
	VkFramebuffer depthColFbo = VkMakeFramebuffer( dc.device, rndCtx.renderPass, attachments, 2, fboWidth, fboHeight );
	recycleFboList.push_back( depthFbo );
	recycleFboList.push_back( depthColFbo );

	// TODO: async, multi-threaded, etc
	// TODO: distinction between streamed and persistent resources
	static bool rescUploaded = 0;
	if( !rescUploaded )
	{
		if( !imguiVkCtx.fontsImg.hndl )
		{
			auto [pixels, width, height] = ImguiGetFontImage();
			u64 uploadSize = width * height * sizeof( u32 );

			constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
			constexpr VkImageUsageFlags flags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			image fonts = VkCreateAllocBindImage( format, flags, { width,height,1 }, 1, vkAlbumArena );

			{
				auto fontsBarrier = VkMakeImageBarrier2( fonts.hndl, 0, 0,
														 VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
														 VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
														 VK_IMAGE_LAYOUT_UNDEFINED,
														 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
														 VK_IMAGE_ASPECT_COLOR_BIT );
				VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &fontsBarrier;
				vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );
			}

			buffer_data upload = VkCreateAllocBindBuffer( uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
			std::memcpy( upload.hostVisible, pixels, uploadSize );
			StagingManagerPushForRecycle( upload.hndl, stagingManager, currentFrameIdx );

			VkBufferImageCopy imgCopyRegion = {};
			imgCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imgCopyRegion.imageSubresource.layerCount = 1;
			imgCopyRegion.imageExtent = { width,height,1 };
			vkCmdCopyBufferToImage(
				thisVFrame.cmdBuff, upload.hndl, fonts.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );

			{
				auto fontsBarrier = VkMakeImageBarrier2( fonts.hndl,
														 VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
														 VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
														 VK_ACCESS_2_SHADER_READ_BIT_KHR,
														 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
														 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
														 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
														 VK_IMAGE_ASPECT_COLOR_BIT );
				VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &fontsBarrier;
				vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );
			}

			VkDbgNameObj( fonts.hndl, dc.device, "Img_Fonts" );
			imguiVkCtx.fontsImg = fonts;
		}

		VkUploadResources( thisVFrame.cmdBuff, entities, currentFrameIdx );
		rescUploaded = 1;

		global_bdas bdas = {};
		bdas.vtxAddr = globVertexBuff.devicePointer;
		bdas.idxAddr = indexBuff.devicePointer;
		bdas.meshDescAddr = meshBuff.devicePointer;
		bdas.lightsDescAddr = lightsBuff.devicePointer;
		bdas.meshletsAddr = meshletBuff.devicePointer;
		bdas.mtrlsAddr = materialsBuff.devicePointer;
		bdas.instDescAddr = instDescBuff.devicePointer;

		assert( bdasUboBuff.hostVisible );
		std::memcpy( bdasUboBuff.hostVisible, &bdas, sizeof( bdas ) );


		// NOTE: UpdateDescriptors
		std::vector<VkWriteDescriptorSet> descUpdates;

		VkDescriptorBufferInfo bdaDesc = Descriptor( bdasUboBuff );
		VkWriteDescriptorSet bdaUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		bdaUpdate.dstSet = globBindlessDesc.set;
		bdaUpdate.dstBinding = VK_GLOBAL_SLOT_UNIFORM_BUFFER;
		//bdaUpdate.dstArrayElement = 1;
		bdaUpdate.dstArrayElement = 0;
		bdaUpdate.descriptorCount = 1;
		bdaUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_UNIFORM_BUFFER ];
		bdaUpdate.pBufferInfo = &bdaDesc;

		descUpdates.push_back( bdaUpdate );


		rndCtx.pbrTexSampler = VkMakeSampler( dc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );
		VkDescriptorImageInfo samplerDesc = { rndCtx.pbrTexSampler };
		VkWriteDescriptorSet samplerUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		samplerUpdate.dstSet = globBindlessDesc.set;
		samplerUpdate.dstBinding = VK_GLOBAL_SLOT_SAMPLER;
		samplerUpdate.dstArrayElement = 0;
		samplerUpdate.descriptorCount = 1;
		samplerUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLER ];
		samplerUpdate.pImageInfo = &samplerDesc;

		descUpdates.push_back( samplerUpdate );

		std::vector<VkDescriptorImageInfo> texDescs;
		texDescs.reserve( std::size( textures.rsc ) );
		for( const auto& i : textures.rsc )
		{
			texDescs.push_back( { 0, i.data.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } );
		}

		VkWriteDescriptorSet texUpdates = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		texUpdates.dstSet = globBindlessDesc.set;
		texUpdates.dstBinding = VK_GLOBAL_SLOT_SAMPLED_IMAGE;
		texUpdates.dstArrayElement = 0;
		texUpdates.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];
		texUpdates.descriptorCount = std::size( texDescs );
		texUpdates.pImageInfo = std::data( texDescs );

		descUpdates.push_back( texUpdates );

		vkUpdateDescriptorSets( dc.device, std::size( descUpdates ), std::data( descUpdates ), 0, 0 );
		sizeof( material_data );

		// TODO: remove from here
		XMMATRIX t = XMMatrixMultiply( XMMatrixScaling( 100.0f, 60.0f, 20.0f ), XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
		XMFLOAT4 boxVertices[ 8 ] = {};
		TrnasformBoxVertices( t, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f }, boxVertices );
		std::span<dbg_vertex> occlusionBoxSpan = { ( dbg_vertex* ) vkDbgCtx.dbgTrisBuff.hostVisible,boxTrisVertexCount };
		assert( std::size( occlusionBoxSpan ) == std::size( boxTrisIndices ) );
		for( u64 i = 0; i < std::size( occlusionBoxSpan ); ++i )
		{
			occlusionBoxSpan[ i ] = { boxVertices[ boxTrisIndices[ i ] ],{0.000004f,0.000250f,0.000123f,1.0f} };
		}
	}

	static bool initBuffers = 0;
	if( !initBuffers )
	{
		vkCmdFillBuffer( thisVFrame.cmdBuff, drawVisibilityBuff.hndl, 0, drawVisibilityBuff.size, 1U );
		vkCmdFillBuffer( thisVFrame.cmdBuff, depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size, 0u );
		// TODO: rename 
		vkCmdFillBuffer( thisVFrame.cmdBuff, atomicCounterBuff.hndl, 0, atomicCounterBuff.size, 0u );
		vkCmdFillBuffer( thisVFrame.cmdBuff, tileBuff.hndl, 0, tileBuff.size, 0u );

		VkBufferMemoryBarrier2KHR initBuffersBarriers[] = {
			VkMakeBufferBarrier2( drawVisibilityBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
			VkMakeBufferBarrier2( depthAtomicCounterBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
			VkMakeBufferBarrier2( atomicCounterBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
			VkMakeBufferBarrier2( tileBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR ),
		};

		VkDependencyInfoKHR initBuffsDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		initBuffsDependency.bufferMemoryBarrierCount = std::size( initBuffersBarriers );
		initBuffsDependency.pBufferMemoryBarriers = initBuffersBarriers;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &initBuffsDependency );

		initBuffers = 1;
	}

	// TODO: destroy buffers
	// TODO: reclaim memory
	if( std::size( stagingManager.pendingUploads ) ) {}



	gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, vkGpuTimer[ frameBufferedIdx ] );
	vkCmdResetQueryPool( thisVFrame.cmdBuff, vkGpuTimer[ frameBufferedIdx ].queryPool, 0, vkGpuTimer[ frameBufferedIdx ].queryCount );
	{
		vk_time_section timePipeline = { thisVFrame.cmdBuff, vkGpuTimer[ frameBufferedIdx ].queryPool, 0 };

		VkViewport viewport = { 0, ( float ) sc.height, ( float ) sc.width, -( float ) sc.height, 0, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &viewport );

		VkClearValue clearVals[ 2 ] = {};

		//DrawIndexedIndirectPass( thisVFrame.cmdBuff,
		//						 gfxZPrepass,
		//						 rndCtx.renderPass,
		//						 rndCtx.offscreenFbo,
		//						 drawCmdBuff,
		//						 thisVFrame.frameData,
		//						 drawCountBuff.hndl,
		//						 indexBuff.hndl,
		//						 VK_INDEX_TYPE_UINT32,
		//						 instDescBuff.size / sizeof( instance_desc ),
		//						 clearVals,
		//						 zPrepassProgram,
		//						 false );

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			gfxZPrepass,
			zRndPass,
			depthFbo,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			thisVFrame.frameData,
			clearVals,
			zPrepassProgram );

		// TODO: pass active view
		DebugDrawPass( thisVFrame.cmdBuff,
					   vkDbgCtx.drawAsTriangles,
					   rndCtx.render2ndPass,
					   depthColFbo,
					   vkDbgCtx.dbgTrisBuff,
					   vkDbgCtx.pipeProg,
					   frameData.mainProjView,
					   { 0,boxTrisVertexCount } );

		DepthPyramidMultiPass(
			thisVFrame.cmdBuff,
			rndCtx.compHiZPipeline,
			rndCtx.quadMinSampler,
			rndCtx.depthPyramidChain,
			rndCtx.depthTarget,
			rndCtx.depthPyramid,
			depthPyramidMultiProgram );


		VkBufferMemoryBarrier2KHR clearDrawCountBarrier = VkMakeBufferBarrier2(
			//drawCountBuff.hndl,
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR );

		VkDependencyInfoKHR dependencyClear = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependencyClear.bufferMemoryBarrierCount = 1;
		dependencyClear.pBufferMemoryBarriers = &clearDrawCountBarrier;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependencyClear );

		// TODO: Aaltonen double draw ? ( not double culling )
		// TODO: merge up to 256 meshes 
		CullPass( 
			thisVFrame.cmdBuff, 
			rndCtx.compPipeline, 
			cullCompProgram, 
			rndCtx.depthPyramid, 
			rndCtx.quadMinSampler, 
			thisVFrame.frameData );

		// Emit depth + HzB

		//DrawIndexedIndirectPass( thisVFrame.cmdBuff,
		//						 rndCtx.gfxPipeline,
		//						 rndCtx.renderPass,
		//						 rndCtx.offscreenFbo,
		//						 drawCmdBuff,
		//						 thisVFrame.frameData,
		//						 drawCountBuff.hndl,
		//						 indexBuff.hndl,
		//						 VK_INDEX_TYPE_UINT32,
		//						 instDescBuff.size / sizeof( instance_desc ),
		//						 clearVals,
		//						 gfxOpaqueProgram,
		//						 true );

		//vkCmdBindDescriptorSets(
		//	thisVFrame.cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxMergedProgram.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

		VkDescriptorSet descSets[] = { frameDesc[ frameBufferedIdx ],globBindlessDesc.set };

		vkCmdBindDescriptorSets(
			thisVFrame.cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxMergedProgram.pipeLayout, 0, std::size( descSets ), descSets, 0, 0 );

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			rndCtx.gfxMergedPipeline,
			rndCtx.renderPass,
			depthColFbo,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			thisVFrame.frameData,
			clearVals,
			gfxMergedProgram );

		DebugDrawPass( thisVFrame.cmdBuff,
					   vkDbgCtx.drawAsTriangles,
					   rndCtx.render2ndPass,
					   depthColFbo,
					   vkDbgCtx.dbgTrisBuff,
					   vkDbgCtx.pipeProg,
					   frameData.activeProjView,
					   { 0,boxTrisVertexCount } );



		// TODO: might need to double buffer
		dbgLineGeomCache = ComputeSceneDebugBoundingBoxes( XMLoadFloat4x4A( &frameData.frustTransf ), entities );
		std::memcpy( vkDbgCtx.dbgLinesBuff.hostVisible, std::data( dbgLineGeomCache ), BYTE_COUNT( dbgLineGeomCache ) );

		// TODO: remove the depth target from these ?
		// TODO: rethink
		if( dbgDraw && ( frameData.freezeMainView || frameData.dbgDraw ) )
		{
			u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;

			range drawRange = {};
			drawRange.offset = frameData.dbgDraw ? 0 : frustBoxOffset;
			drawRange.size = ( frameData.freezeMainView && frameData.dbgDraw ) ?
				std::size( dbgLineGeomCache ) : ( frameData.freezeMainView ? boxLineVertexCount : frustBoxOffset );


			DebugDrawPass( thisVFrame.cmdBuff,
						   vkDbgCtx.drawAsLines,
						   rndCtx.render2ndPass,
						   depthColFbo,
						   vkDbgCtx.dbgLinesBuff,
						   vkDbgCtx.pipeProg,
						   frameData.activeProjView,
						   drawRange );

			if( frameData.dbgDraw )
			{
				DrawIndirectPass( thisVFrame.cmdBuff,
								  gfxDrawIndirDbg,
								  rndCtx.render2ndPass,
								  depthColFbo,
								  drawCmdAabbsBuff,
								  drawCountDbgBuff.hndl,
								  dbgDrawProgram,
								  frameData.activeProjView );
			}

		}

		AverageLuminancePass( thisVFrame.cmdBuff,
							  rndCtx.compAvgLumPipe,
							  avgLumCompProgram,
							  rndCtx.colorTarget,
							  frameData.elapsedSeconds );

		DepthPyramidMultiPass(
			thisVFrame.cmdBuff,
			rndCtx.compHiZPipeline,
			rndCtx.quadMinSampler,
			rndCtx.depthPyramidChain,
			rndCtx.depthTarget,
			rndCtx.depthPyramid,
			depthPyramidMultiProgram );

		FinalCompositionPass( thisVFrame.cmdBuff,
							  rndCtx.compTonemapPipe,
							  rndCtx.colorTarget,
							  tonemapCompProgram,
							  sc.imgs[ imgIdx ],
							  sc.imgViews[ imgIdx ] );

		VkImageMemoryBarrier2KHR compositionEndBarriers[] = {
			VkMakeImageBarrier2( rndCtx.colorTarget.hndl,
								 VK_ACCESS_2_SHADER_READ_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 0, 0,
								 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
								 VK_IMAGE_ASPECT_COLOR_BIT ),
			VkMakeImageBarrier2( sc.imgs[ imgIdx ],
								 VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR ,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependency.imageMemoryBarrierCount = std::size( compositionEndBarriers );
		dependency.pImageMemoryBarriers = compositionEndBarriers;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );

		VkViewport uiViewport = { 0, 0, ( float ) sc.width, ( float ) sc.height, 0, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &uiViewport );

		VkFramebuffer uiFbo = VkMakeFramebuffer( dc.device, imguiVkCtx.renderPass, &sc.imgViews[ imgIdx ], 1, sc.width, sc.height );
		recycleFboList.push_back( uiFbo );
		ImguiDrawUiPass( imguiVkCtx, thisVFrame.cmdBuff, uiFbo, currentFrameIdx );


		VkImageMemoryBarrier2KHR presentWaitBarrier = VkMakeImageBarrier2(
			sc.imgs[ imgIdx ],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
			0, 0,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT );

		VkDependencyInfoKHR dependencyPresent = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependencyPresent.imageMemoryBarrierCount = 1;
		dependencyPresent.pImageMemoryBarriers = &presentWaitBarrier;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependencyPresent );
	}
	VK_CHECK( vkEndCommandBuffer( thisVFrame.cmdBuff ) );


	VkSemaphore signalSemas[] = { thisVFrame.canPresentSema, rndCtx.timelineSema };
	u64 signalValues[] = { 0, rndCtx.vFrameIdx };
	
	VkTimelineSemaphoreSubmitInfo timelineInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
	timelineInfo.signalSemaphoreValueCount = std::size( signalValues );
	timelineInfo.pSignalSemaphoreValues = signalValues;

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.pNext = &timelineInfo;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &thisVFrame.canGetImgSema;
	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;// VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	submitInfo.pWaitDstStageMask = &waitDstStageMsk;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &thisVFrame.cmdBuff;
	submitInfo.signalSemaphoreCount = std::size( signalSemas );
	submitInfo.pSignalSemaphores = signalSemas;
	// NOTE: queue submit has implicit host sync for trivial stuff
	VK_CHECK( vkQueueSubmit( dc.gfxQueue, 1, &submitInfo, 0 ) );

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &thisVFrame.canPresentSema;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &sc.swapchain;
	presentInfo.pImageIndices = &imgIdx;
	VK_CHECK( vkQueuePresentKHR( dc.gfxQueue, &presentInfo ) );
}

void VkBackendKill()
{
	// NOTE: SHOULDN'T need to check if( VkObj ). Can't create -> app fail
	assert( dc.device );
	vkDeviceWaitIdle( dc.device );
	//for( auto& queued : deviceGlobalDeletionQueue ) queued();
	//deviceGlobalDeletionQueue.clear();
	

	vkDestroyDevice( dc.device, 0 );
#ifdef _VK_DEBUG_
	vkDestroyDebugUtilsMessengerEXT( vkInst, vkDbgMsg, 0 );
#endif
	vkDestroySurfaceKHR( vkInst, vkSurf, 0 );
	vkDestroyInstance( vkInst, 0 );

	//SysDllUnload( VK_DLL );
}

#undef HTVK_NO_SAMPLER_REDUCTION
#undef VK_APPEND_DESTROYER
#undef VK_CHECK

