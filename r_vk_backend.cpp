#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string_view>
#include <charconv>
#include <span>
#include <format>
#include <memory>

#include "vk_memory.h"
#include "vk_resources.h"
#include "vk_timer.h"
#include "vk_error.h"

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


//====================CONSTS====================//
constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;

constexpr u32 NOT_USED_IDX = -1;
constexpr u32 OBJ_CULL_WORKSIZE = 64;
constexpr u32 MLET_CULL_WORKSIZE = 256;
//==============================================//
// TODO: cvars
//====================CVARS====================//

//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//
constexpr bool multiPassDepthPyramid = 1;
static_assert( multiPassDepthPyramid );
// TODO: enable gfx debug outside of VS Debug
constexpr bool vkValidationLayerFeatures = 1;

constexpr bool dbgDraw = true;
//==============================================//


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

	auto formattedMsg = std::format( "{}\n{}\n", callbackData->pMessageIdName, callbackData->pMessage );
	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
	{
		std::cout << ">>> VK_WARNING <<<\n" << formattedMsg << "\n";
	}
	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
	{
		//const char* pVkObjName = callbackData->pObjects ? callbackData->pObjects[ 0 ].pObjectName : "";
		std::cout << ">>> VK_ERROR <<<\n" << formattedMsg << "\n";// << pVkObjName << "\n";
		abort();
	} 

	return VK_FALSE;
}


constexpr VkValidationFeatureEnableEXT enabledValidationFeats[] = {
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
		VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
};

struct vk_instance
{
	u64 dll;
	VkInstance hndl;
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

	u64 VK_DLL;
	VK_CHECK( VK_INTERNAL_ERROR( !( VK_DLL = SysDllLoad( "vulkan-1.dll" ) ) ) );

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
	VK_CHECK( VK_INTERNAL_ERROR( appInfo.apiVersion < VK_API_VERSION_1_4 ) );

	VkInstanceCreateInfo instInfo = { 
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = std::size( LAYERS ),
		.ppEnabledLayerNames = LAYERS,
		.enabledExtensionCount = std::size( ENABLED_INST_EXTS ),
		.ppEnabledExtensionNames = ENABLED_INST_EXTS,
	};
#ifdef _VK_DEBUG_

	VkValidationFeaturesEXT vkValidationFeatures = { 
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = std::size( enabledValidationFeats ),
		.pEnabledValidationFeatures = enabledValidationFeats,
	};

	VkDebugUtilsMessengerCreateInfoEXT vkDbgExt = { 
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.pNext = vkValidationLayerFeatures ? &vkValidationFeatures : 0,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = VkDbgUtilsMsgCallback,
	};

	instInfo.pNext = &vkDbgExt;
#endif
	VK_CHECK( vkCreateInstance( &instInfo, 0, &vkInstance ) );

	VkLoadInstanceProcs( vkInstance, *vkGetInstanceProcAddr );

#ifdef _VK_DEBUG_
	VK_CHECK( vkCreateDebugUtilsMessengerEXT( vkInstance, &vkDbgExt, 0, &vkDbgUtilsMsgExt ) );
#endif

	return { .dll = VK_DLL, .hndl = vkInstance, .dbgMsg = vkDbgUtilsMsgExt };
}

#include "vk_device.h"

static vk_device_ctx dc;

#include "vk_pso.h"

struct virtual_frame
{
	vk_gpu_timer gpuTimer;
	std::shared_ptr<vk_buffer>		frameData;
	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuff;
	VkSemaphore		canGetImgSema;
	u16 frameDescIdx;
};

inline virtual_frame VkCreateVirtualFrame( vk_device_ctx& dc, u32 bufferSize )
{
	virtual_frame vrtFrame = {};

	VkCommandPoolCreateInfo cmdPoolInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = dc.gfxQueueIdx,
	};
	VK_CHECK( vkCreateCommandPool( dc.device, &cmdPoolInfo, 0, &vrtFrame.cmdPool ) );

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vrtFrame.cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK( vkAllocateCommandBuffers( dc.device, &cmdBuffAllocInfo, &vrtFrame.cmdBuff ) );

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK( vkCreateSemaphore( dc.device, &semaInfo, 0, &vrtFrame.canGetImgSema ) );

	buffer_info info = {
		.name = "Buff_VirtualFrame",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.elemCount = bufferSize,
		.stride = 1,
		.usage = buffer_usage::HOST_VISIBLE
	};
	vrtFrame.frameData = std::make_shared<vk_buffer>( VkCreateAllocBindBuffer( &dc, info ) );

	vrtFrame.gpuTimer = VkMakeGpuTimer( dc.device, 1, dc.timestampPeriod );

	buffer_info queryBuff = {
		.name = "Buff_TimestampQueries",
		.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = vrtFrame.gpuTimer.queryCount,
		.stride = sizeof( u64 ),
		.usage = buffer_usage::HOST_VISIBLE
	};
	vrtFrame.gpuTimer.resultBuff = VkCreateAllocBindBuffer( &dc, queryBuff );
	

	return vrtFrame;
}

struct vk_swapchain
{
	VkSwapchainKHR	swapchain;
	VkImageView		imgViews[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkImage			imgs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkSemaphore		canPresentSemas[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
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
	VkWin32SurfaceCreateInfoKHR surfInfo = { 
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.hinstance = hInst,
		.hwnd = hWnd,
	};

	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;

#else
#error Must provide OS specific Surface
#endif // VK_USE_PLATFORM_WIN32_KHR
}

// TODO: sep initial validation form sc creation when resize ?
inline static vk_swapchain
VkMakeSwapchain(
	VkDevice			vkDevice,
	VkPhysicalDevice	vkPhysicalDevice,
	VkSurfaceKHR		vkSurf,
	u32					queueFamIdx,
	VkFormat			scDesiredFormat,
	u32                 numImages
) {
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkPhysicalDevice, vkSurf, &surfaceCaps ) );
	assert( surfaceCaps.maxImageArrayLayers >= 1 );

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
		{
			if( formats[ i ].format == scDesiredFormat )
			{
				scFormatAndColSpace = formats[ i ];
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !scFormatAndColSpace.format ) );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, 0 ) );
		std::vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( 
			vkPhysicalDevice, vkSurf, &numPresentModes, std::data( presentModes ) ) );

		constexpr VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		for( u32 j = 0; j < numPresentModes; ++j )
		{
			if( presentModes[ j ] == desiredPresentMode )
			{
				presentMode = desiredPresentMode;
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !presentMode ) );
	}


	u32 scImgCount = numImages;
	assert( ( scImgCount > surfaceCaps.minImageCount ) && ( scImgCount < surfaceCaps.maxImageCount ) );
	assert( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) &&
			( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkImageUsageFlags scImgUsage =
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	VK_CHECK( VK_INTERNAL_ERROR( ( surfaceCaps.supportedUsageFlags & scImgUsage ) != scImgUsage ) );


	VkSwapchainCreateInfoKHR scInfo = { 
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = vkSurf,
		.minImageCount = scImgCount,
		.imageFormat = scFormatAndColSpace.format,
		.imageColorSpace = scFormatAndColSpace.colorSpace,
		.imageExtent = surfaceCaps.currentExtent,
		.imageArrayLayers = 1,
		.imageUsage = scImgUsage,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &queueFamIdx,
		.preTransform = surfaceCaps.currentTransform,
		.compositeAlpha = surfaceComposite,
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = 0
	};

	VkImageFormatProperties scImageProps = {};
	VK_CHECK( vkGetPhysicalDeviceImageFormatProperties( 
		vkPhysicalDevice, scInfo.imageFormat, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, scInfo.imageUsage,
		scInfo.flags, &scImageProps ) );

	vk_swapchain sc = {};
	VK_CHECK( vkCreateSwapchainKHR( vkDevice, &scInfo, 0, &sc.swapchain ) );

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, 0 ) );
	VK_CHECK( VK_INTERNAL_ERROR( !( scImgsNum == scInfo.minImageCount ) ) );
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, sc.imgs ) );

	VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( scInfo.imageFormat );
	char name[ 32 ];
	for( u64 i = 0; i < scImgsNum; ++i )
	{
		snprintf( name, sizeof( name ), "Img_Swapchain%d", ( u32 ) i );
		VkDbgNameObj( sc.imgs[ i ], vkDevice, name );
		sc.imgViews[ i ] = VkMakeImgView( 
			vkDevice, sc.imgs[ i ], aspectFlags, scInfo.imageFormat, 0, 1, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );
		VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &sc.canPresentSemas[ i ] ) );
	}

	sc.width = scInfo.imageExtent.width;
	sc.height = scInfo.imageExtent.height;
	sc.imgCount = scInfo.minImageCount;
	sc.imgFormat = scInfo.imageFormat;

	return sc;
}


#include "r_data_structs.h"

// TODO:
struct renderer_config
{
	static constexpr u8 MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;

	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	VkFormat		desiredHiZFormat = VK_FORMAT_R32_SFLOAT;
	VkFormat        desiredSwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	u16             renderWidth;
	u16             rednerHeight;
	u8              maxAllowedFramesInFlight = 2;
};
static renderer_config renderCfg = {};




// TODO: should make obsolete
struct vk_program
{
	VkPipelineLayout			pipeLayout;
	VkDescriptorUpdateTemplate	descUpdateTemplate;
	VkShaderStageFlags			pushConstStages;
	VkDescriptorSetLayout       descSetLayout;
	group_size					groupSize;
};

enum class vk_descriptor_resource_type : u8
{
	BUFFER,
	IMAGE
};

struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo buff;
		VkDescriptorImageInfo img;
	};
	VkDescriptorType descriptorType;
	vk_descriptor_resource_type rscType;

	vk_descriptor_info( VkBuffer buff, u64 offset, u64 range ) : buff{ buff, offset, range }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		rscType = vk_descriptor_resource_type::BUFFER;
		
	}
	vk_descriptor_info( VkSampler sampler ) : img{ .sampler = sampler, .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED }
	{
		descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		rscType = vk_descriptor_resource_type::IMAGE;
	}
	vk_descriptor_info( VkImageView view, VkImageLayout imgLayout ) : img{ .imageView = view, .imageLayout = imgLayout }
	{
		descriptorType = ( imgLayout == VK_IMAGE_LAYOUT_GENERAL ) ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE :
			VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		rscType = vk_descriptor_resource_type::IMAGE;
	}

	// TODO: remove
	vk_descriptor_info( VkSampler sampler, VkImageView view, VkImageLayout imgLayout ) : img{ sampler, view, imgLayout }{}
	vk_descriptor_info( VkDescriptorBufferInfo buffInfo ) : buff{ buffInfo }{}
	vk_descriptor_info( VkDescriptorImageInfo imgInfo ) : img{ imgInfo }{}
};

constexpr VkDescriptorType bindingToTypeMap[] = {
	VK_DESCRIPTOR_TYPE_SAMPLER,
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
};

constexpr VkDescriptorType VkDescBindingToType( u32 binding )
{
	switch( binding )
	{
	case 0: return VK_DESCRIPTOR_TYPE_SAMPLER;
	case 1: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case 2: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case 3: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	}
}

constexpr u32 VkDescTypeToBinding( VkDescriptorType type )
{
	switch( type )
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER: return 0;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return 1;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return 2;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return 3;
	default: return INVALID_IDX;
	}
}

struct vk_descriptor_write
{
	vk_descriptor_info descInfo;
	u16 descIdx;
};

struct vk_descriptor_manager
{
	struct vk_table_entry
	{
		std::vector<u16> freeSlots;
		u16 slotsCount;
		u16 usedSlots;
	};

	vk_table_entry table[ std::size( bindingToTypeMap ) ];
	std::vector<vk_descriptor_write> updateCache;

	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
};

inline vk_descriptor_manager VkMakeDescriptorManager( VkDevice vkDevice, const VkPhysicalDeviceProperties& gpuProps )
{
	constexpr u32 maxSize = u16( -1 );
	constexpr u32 maxSetCount = 1;

	u32 descCount[ std::size( bindingToTypeMap ) ] = {
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSamplers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageBuffers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageImages ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSampledImages )
	};
	
	VkDescriptorPoolSize poolSizes[ std::size( bindingToTypeMap ) ] = {};
	for( u32 i = 0; i < std::size( bindingToTypeMap ); ++i )
	{
		poolSizes[ i ] = { .type = bindingToTypeMap[ i ],.descriptorCount = descCount[ i ] };
	}

	VkDescriptorPool vkDescPool;
	VkDescriptorPoolCreateInfo descPoolInfo = { 
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = maxSetCount,
		.poolSizeCount = std::size( poolSizes ),
		.pPoolSizes = poolSizes
	};
	VK_CHECK( vkCreateDescriptorPool( vkDevice, &descPoolInfo, 0, &vkDescPool ) );


	constexpr VkDescriptorBindingFlags flag =
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

	VkDescriptorBindingFlags bindingFlags[ std::size( bindingToTypeMap ) ] = {};
	VkDescriptorSetLayoutBinding descSetLayout[ std::size( bindingToTypeMap ) ] = {};
	for( u32 i = 0; i < std::size( bindingToTypeMap ); ++i )
	{
		descSetLayout[ i ] = {
			.binding = i,
			.descriptorType = bindingToTypeMap[ i ],
			.descriptorCount = descCount[ i ],
			.stageFlags = VK_SHADER_STAGE_ALL 
		};
		bindingFlags[ i ] = flag;
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = std::size( bindingFlags ),
		.pBindingFlags = bindingFlags
	};
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descSetFalgs,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = std::size( descSetLayout ),
		.pBindings = descSetLayout
	};

	VkDescriptorSetLayout vkDescSetLayout;
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &vkDescSetLayout ) );

	VkDescriptorSetAllocateInfo descSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = vkDescPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &vkDescSetLayout
	};

	VkDescriptorSet vkDescSet;
	VK_CHECK( vkAllocateDescriptorSets( vkDevice, &descSetInfo, &vkDescSet ) );

	vk_descriptor_manager manager =  {
		.pool = vkDescPool,
		.setLayout = vkDescSetLayout,
		.set = vkDescSet
	};

	for( u32 i = 0; i < std::size( manager.table ); ++i )
	{
		manager.table[ i ] = { .slotsCount = maxSize, .usedSlots = 0 };
	}

	return manager;
}

inline u16 VkAllocDescriptorIdx( vk_descriptor_manager& manager, const vk_descriptor_info& rscDescInfo )
{
	u32 bindingSlotIdx = VkDescTypeToBinding( rscDescInfo.descriptorType );

	assert( bindingSlotIdx != INVALID_IDX );
	assert( rscDescInfo.descriptorType == VkDescBindingToType( bindingSlotIdx ) );

	u16 destIndex = INVALID_IDX;
	auto& binding = manager.table[ bindingSlotIdx ];

	if( u64 sz = std::size( binding.freeSlots ); sz )
	{
		destIndex = binding.freeSlots[ sz - 1 ];
		binding.freeSlots.pop_back();
	}
	else if( binding.usedSlots + 1 < binding.slotsCount )
	{
		destIndex = binding.usedSlots++;
	}
	else assert( 0 && "Desc table overflow" );

	assert( destIndex != INVALID_IDX );

	manager.updateCache.push_back( { rscDescInfo, destIndex } );

	return destIndex;
}

inline void VkDescriptorManagerFlushUpdates( vk_descriptor_manager& manager, VkDevice vkDevice )
{
	if( std::size( manager.updateCache ) )
	{
		std::vector<VkWriteDescriptorSet> writes;
		for( const auto& update : manager.updateCache )
		{
			VkDescriptorType descType = update.descInfo.descriptorType;
			u16 updateIdx = update.descIdx;
			u32 bindingSlotIdx = VkDescTypeToBinding( descType );

			const VkDescriptorImageInfo* pImageInfo = 0;
			const VkDescriptorBufferInfo* pBufferInfo = 0;

			if( update.descInfo.rscType == vk_descriptor_resource_type::BUFFER )
			{
				pBufferInfo = &update.descInfo.buff;
			}
			else if( update.descInfo.rscType == vk_descriptor_resource_type::IMAGE )
			{
				pImageInfo = &update.descInfo.img;
			}

			VkWriteDescriptorSet writeEntryInfo = {
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = manager.set,
				.dstBinding = bindingSlotIdx,
				.dstArrayElement = updateIdx,
				.descriptorCount = 1,
				.descriptorType = descType,
				.pImageInfo = pImageInfo,
				.pBufferInfo = pBufferInfo
			};
			writes.push_back( writeEntryInfo );
		}

		vkUpdateDescriptorSets( vkDevice, std::size( writes ), std::data( writes ), 0, 0 );
	}
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

inline static vk_program VkMakePipelineProgram(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties&	gpuProps,
	VkPipelineBindPoint					bindPoint,
	vk_shader_list						shaders,
	VkDescriptorSetLayout				bindlessLayout 
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
	VkSamplerCreateInfo vkSamplerInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VkGetFilterTypeFromGltf( config.mag ),
		.minFilter = VkGetFilterTypeFromGltf( config.min ),
		.mipmapMode =  VkGetMipmapTypeFromGltf( config.min ),
		.addressModeU = VkGetAddressModeFromGltf( config.addrU ),
		.addressModeV = VkGetAddressModeFromGltf( config.addrV )
	};

	return vkSamplerInfo;
}

inline image_info GetImageInfoFromMetadata( const image_metadata& meta, VkImageUsageFlags usageFlags )
{
	return { 
		.format =  VkGetFormat( meta.format ),
		.usg = usageFlags,
		.width = meta.width,
		.height = meta.height,
		.layerCount = meta.layerCount,
		.mipCount =  meta.mipCount,
	};
}

#define HTVK_NO_SAMPLER_REDUCTION VK_SAMPLER_REDUCTION_MODE_MAX_ENUM

// TODO: AddrMode ?
inline VkSampler VkMakeSampler(
	VkDevice				vkDevice,
	VkSamplerReductionMode	reductionMode = HTVK_NO_SAMPLER_REDUCTION,
	VkFilter				filter = VK_FILTER_LINEAR,
	VkSamplerAddressMode	addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	VkSamplerMipmapMode		mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
) {
	VkSamplerReductionModeCreateInfo reduxInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO,
		.reductionMode = reductionMode,
	};

	VkSamplerCreateInfo samplerInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = ( reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX_ENUM ) ? 0 : &reduxInfo,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = mipmapMode,
		.addressModeU = addressMode,
		.addressModeV = addressMode,
		.addressModeW = addressMode,
		.maxAnisotropy = 1.0f,
		.minLod = 0,
		.maxLod = VK_LOD_CLAMP_NONE,
		.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
		.unnormalizedCoordinates = VK_FALSE,
	};
	
	VkSampler sampler;
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


#include "ht_geometry.h"


#include "imgui/imgui.h"

// TODO: better double buffer vert + idx
// TODO: move spv shaders into exe folder
struct imgui_vk_context
{
	vk_buffer                   vtxBuffs[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	vk_buffer                   idxBuffs[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	vk_image                    fontsImg;
	VkImageView                 fontsView;
	VkSampler                   fontSampler;

	VkDescriptorSetLayout       descSetLayout;
	VkPipelineLayout            pipelineLayout;
	VkDescriptorUpdateTemplate  descTemplate = {};
	VkPipeline	                pipeline;
	
};

static imgui_vk_context imguiVkCtx;

// TODO: buffer resize ?
// TODO: vk formats 
static inline imgui_vk_context ImguiMakeVkContext(
	VkDevice vkDevice,
	VkFormat colDstFormat
){
	VkSampler fontSampler = VkMakeSampler( 
		vkDevice, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );
	
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

	VkPushConstantRange pushConst = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( float ) * 4 };
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

	vk_shader2 vert = VkLoadShader2( "bin/SpirV/vertex_ImGuiVsMain.spv", vkDevice );
	vk_shader2 frag = VkLoadShader2( "bin/SpirV/pixel_ImGuiPsMain.spv", vkDevice );

	vk_gfx_pipeline_state guiState = {};
	guiState.blendCol = VK_TRUE; 
	guiState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	guiState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	guiState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	guiState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	guiState.depthWrite = VK_FALSE;
	guiState.depthTestEnable = VK_FALSE;
	guiState.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	guiState.polyMode = VK_POLYGON_MODE_FILL;
	guiState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	guiState.cullFlags = VK_CULL_MODE_NONE;

	VkPipelineShaderStageCreateInfo shaderStages[] = { VkMakePipelineShaderInfo( vert ), VkMakePipelineShaderInfo( frag ) };
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipeline pipeline = VkMakeGfxPipeline(
		vkDevice, shaderStages, dynamicStates, 
		&renderCfg.desiredSwapchainFormat, 1, VK_FORMAT_UNDEFINED, pipelineLayout, guiState );


	imgui_vk_context ctx = {};
	ctx.descSetLayout = descSetLayout;
	ctx.pipelineLayout = pipelineLayout;
	ctx.pipeline = pipeline;
	ctx.descTemplate = descTemplate;
	ctx.fontSampler = fontSampler;
	ctx.vtxBuffs[ 0 ] = VkCreateAllocBindBuffer( &dc, { 
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
		
	ctx.idxBuffs[ 0 ] = VkCreateAllocBindBuffer( &dc, { 
		.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
	ctx.vtxBuffs[ 1 ] = VkCreateAllocBindBuffer( &dc, { 
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
	ctx.idxBuffs[ 1 ] = VkCreateAllocBindBuffer(  &dc, { 
		.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		.elemCount = 64 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );

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


// TODO: use instancing 4 drawing ?
struct debug_context
{
	vk_buffer dbgLinesBuff;
	vk_buffer dbgTrisBuff;
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
	const VkPhysicalDeviceProperties&	gpuProps,
	VkDescriptorSetLayout setLayout
){
	debug_context dbgCtx = {};

	vk_shader vert = VkLoadShader( "Shaders/v_cpu_dbg_draw.vert.spv", vkDevice );
	vk_shader frag = VkLoadShader( "Shaders/f_pass_col.frag.spv", vkDevice );

	dbgCtx.pipeProg = VkMakePipelineProgram( vkDevice, gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vert, &frag }, setLayout );

	static_assert( worldLeftHanded );
	vk_gfx_pipeline_state lineDrawPipelineState = {
		.polyMode = VK_POLYGON_MODE_LINE,
		.cullFlags = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		.depthWrite = VK_FALSE,
		.depthTestEnable = VK_FALSE,
		.blendCol = VK_FALSE,
	};
	dbgCtx.drawAsLines = VkMakeGfxPipeline( 
		vkDevice, 0, dbgCtx.pipeProg.pipeLayout, 
		vert.module, frag.module, 
		&renderCfg.desiredColorFormat, 1, 
		VK_FORMAT_UNDEFINED, lineDrawPipelineState );

	vk_gfx_pipeline_state triDrawPipelineState = {};
	triDrawPipelineState.blendCol = VK_TRUE;
	triDrawPipelineState.depthWrite = VK_TRUE;
	triDrawPipelineState.depthTestEnable = VK_TRUE;
	triDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;// VK_CULL_MODE_FRONT_BIT;
	triDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	triDrawPipelineState.polyMode = VK_POLYGON_MODE_FILL;
	triDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	dbgCtx.drawAsTriangles = VkMakeGfxPipeline( 
		vkDevice, 0, dbgCtx.pipeProg.pipeLayout, 
		vert.module, frag.module, 
		&renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat, triDrawPipelineState );


	vkDestroyShaderModule( vkDevice, vert.module, 0 );
	vkDestroyShaderModule( vkDevice, frag.module, 0 );

	dbgCtx.dbgLinesBuff = VkCreateAllocBindBuffer( &dc, { 
		.name = "Buff_DbgLines",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		.elemCount = 128 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );
	dbgCtx.dbgTrisBuff = VkCreateAllocBindBuffer(  &dc, { 
		.name = "Buff_DbgTris",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		.elemCount = 128 * KB, 
		.stride = 1, 
		.usage = buffer_usage::HOST_VISIBLE } );

	return dbgCtx;
}



static entities_data entities;
static std::vector<dbg_vertex> dbgLineGeomCache;


static vk_buffer proxyGeomBuff;
static vk_buffer proxyIdxBuff;

static vk_buffer screenspaceBoxBuff;

static vk_buffer globVertexBuff;
static vk_buffer indexBuff;
static vk_buffer meshBuff;

static vk_buffer meshletBuff;
static vk_buffer meshletDataBuff;

// TODO:
static vk_buffer transformsBuff;

static vk_buffer materialsBuff;
static vk_buffer instDescBuff;
static vk_buffer lightsBuff;


static vk_buffer intermediateIndexBuff;
static vk_buffer indirectMergedIndexBuff;

//static vk_buffer visibleInstsBuff;
//static vk_buffer meshletIdBuff;
//static vk_buffer visibleMeshletsBuff;

static vk_buffer drawCmdBuff;
static vk_buffer drawCmdAabbsBuff;
static vk_buffer drawCmdDbgBuff;
static vk_buffer drawVisibilityBuff;

static vk_buffer drawMergedCmd;

constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "Assets/cyberbaron.drak";


// TODO: recycle_queue for more objects
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

struct buffer_copy
{
	VkBuffer dst;
	u32 size;
	const u8* pData;
};
inline static void
StagingManagerBufferCopy( 
	staging_manager& stgMngr, 
	VkCommandBuffer cmdBuff, 
	const buffer_copy& buffCpy, 
	u64 currentFrameId 
) {
	VkBufferUsageFlags usg = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	vk_buffer stagingBuff = VkCreateAllocBindBuffer( &dc, {
		.usageFlags = usg, .elemCount = buffCpy.size, .stride = 1, .usage = buffer_usage::STAGING } );
	std::memcpy( stagingBuff.hostVisible, buffCpy.pData, stagingBuff.size );
	StagingManagerPushForRecycle( stagingBuff.hndl, stgMngr, currentFrameId );

	VkBufferCopy copyRegion = { 0,0,stagingBuff.size };
	vkCmdCopyBuffer( cmdBuff, stagingBuff.hndl, buffCpy.dst, 1, &copyRegion );
}

static staging_manager stagingManager;

static vk_buffer drawCountBuff;
static vk_buffer drawCountDbgBuff;

static vk_buffer shaderGlobalsBuff;
static vk_buffer shaderGlobalSyncCounterBuff;

static vk_buffer depthAtomicCounterBuff;

static vk_buffer atomicCounterBuff;

static vk_buffer dispatchCmdBuff0;
static vk_buffer dispatchCmdBuff1;

static vk_buffer meshletCountBuff;

static vk_buffer mergedIndexCountBuff;

static vk_buffer drawMergedCountBuff;
// TODO:
inline static void VkInitInternalBuffers()
{
	shaderGlobalsBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_ShaderGlobals",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = 64,
		.stride = 1,
		.usage = buffer_usage::GPU_ONLY } ); 

	shaderGlobalSyncCounterBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_ShaderGlobalSyncCounter",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } );  

	drawCountBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_DrawCount",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } );   

	drawCountDbgBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_DbgDrawCount",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } );  
	
	depthAtomicCounterBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_DepthPyramidDepthAtomicCounter",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } );  

	dispatchCmdBuff0 = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_DispatchCmd0",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( dispatch_command ),
		.usage = buffer_usage::GPU_ONLY } );  

	dispatchCmdBuff1 = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_DispatchCmd1",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
												.elemCount = 1,
												.stride = sizeof( dispatch_command ),
												.usage = buffer_usage::GPU_ONLY } );  
	
	meshletCountBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_MletDispatchCounter",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } ); 

	atomicCounterBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_AtomicCounter",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } ); 

	mergedIndexCountBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_MergedIdxCounter",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( u32 ),
		.usage = buffer_usage::GPU_ONLY } ); 

	drawMergedCountBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_DrawMergedCount",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
												  .elemCount = 1,
												  .stride = sizeof( u32 ),
												  .usage = buffer_usage::GPU_ONLY } ); 
}

// TODO: move out of global/static
static vk_program	gfxMeshletProgram = {};
static vk_program	avgLumCompProgram = {};

static vk_program   dbgDrawProgram = {};
static VkPipeline   gfxDrawIndirDbg = {};


// TODO: remake
struct render_context
{
	VkPipeline      gfxZPrepass;
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

	virtual_frame	vrtFrames[ renderer_config::MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	VkSemaphore     timelineSema;
	u64				vFrameIdx = 0;
	u8				framesInFlight;
};

struct render_path
{
	std::shared_ptr<vk_buffer> pAvgLumBuff;
	std::shared_ptr<vk_image> pColorTarget;
	std::shared_ptr<vk_image> pDepthTarget;
	std::shared_ptr<vk_image> pHiZTarget;

	VkImageView colorView;
	VkImageView depthView;
	VkImageView hiZView;
	VkImageView hiZMipViews[ MAX_MIP_LEVELS ];

	VkSampler quadMinSampler;
	VkSampler pbrSampler;

	u16 avgLumIdx;
	u16 depthSrv;
	u16 hizSrv;
	u16 colSrv;
	u16 hizMipUavs[ MAX_MIP_LEVELS ];
	u16 quadMinSamplerIdx;
	u16 pbrSamplerIdx;

	u16 swapchainUavs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
};

static render_path renderPath;

static render_context rndCtx;


struct vk_backend
{
	vk_descriptor_manager descManager;
	vk_swapchain sc;
	vk_instance inst;
	VkSurfaceKHR surf;
	VkPipelineLayout globalLayout;
};

static vk_backend vk;

inline VkPipelineLayout VkMakeGlobalPipelineLayout( 
	VkDevice vkDevice, 
	const VkPhysicalDeviceProperties& props, 
	const vk_descriptor_manager& vkDescDealer
) {
	VkPushConstantRange pushConstRange = { VK_SHADER_STAGE_ALL, 0, props.limits.maxPushConstantsSize };
	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = 1;
	pipeLayoutInfo.pSetLayouts = &vkDescDealer.setLayout;
	pipeLayoutInfo.pushConstantRangeCount = 1;
	pipeLayoutInfo.pPushConstantRanges = &pushConstRange;

	VkPipelineLayout pipelineLayout = {};
	VK_CHECK( vkCreatePipelineLayout( dc.device, &pipeLayoutInfo, 0, &pipelineLayout ) );

	return pipelineLayout;
}

void VkBackendInit()
{
	vk.inst = VkMakeInstance();

	vk.surf = VkMakeWinSurface( vk.inst.hndl, hInst, hWnd );
	dc = VkMakeDeviceContext( vk.inst.hndl, vk.surf );

	vk.sc = VkMakeSwapchain( dc.device, dc.gpu, vk.surf, dc.gfxQueueIdx, renderCfg.desiredSwapchainFormat, 3 );

	VkInitInternalBuffers();

	rndCtx.framesInFlight = renderCfg.maxAllowedFramesInFlight;
	for( u64 vfi = 0; vfi < rndCtx.framesInFlight; ++vfi )
	{
		rndCtx.vrtFrames[ vfi ] = VkCreateVirtualFrame( dc, u16( -1 ) );
	}
	VkSemaphoreTypeCreateInfo timelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = rndCtx.vFrameIdx = 0,
	};
	VkSemaphoreCreateInfo timelineSemaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineInfo };
	VK_CHECK( vkCreateSemaphore( dc.device, &timelineSemaInfo, 0, &rndCtx.timelineSema ) );

	vk.descManager = VkMakeDescriptorManager( dc.device, dc.gpuProps );
	vk.globalLayout = VkMakeGlobalPipelineLayout( dc.device, dc.gpuProps, vk.descManager );
	VkDbgNameObj( vk.globalLayout, dc.device, "Vk_Pipeline_Layout_Global" );

	{
		vk_shader vertZPre = VkLoadShader( "Shaders/v_z_prepass.vert.spv", dc.device );
		rndCtx.gfxZPrepass = VkMakeGfxPipeline( 
			dc.device, 0, vk.globalLayout, vertZPre.module, 0, 0, 0, renderCfg.desiredDepthFormat, {} );

		vkDestroyShaderModule( dc.device, vertZPre.module, 0 );
	}
	{
		vk_shader vertBox = VkLoadShader( "Shaders/box_meshlet_draw.vert.spv", dc.device );
		vk_shader normalCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );

		vk_gfx_pipeline_state lineDrawPipelineState = {
			.polyMode = VK_POLYGON_MODE_LINE,
			.cullFlags = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			.depthWrite = VK_FALSE,
			.depthTestEnable = VK_FALSE,
			.blendCol = VK_FALSE
		};

		dbgDrawProgram = VkMakePipelineProgram( 
			dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertBox, &normalCol }, vk.descManager.setLayout );
		gfxDrawIndirDbg = VkMakeGfxPipeline(
			dc.device, 0, dbgDrawProgram.pipeLayout, 
			vertBox.module, normalCol.module, 
			&renderCfg.desiredColorFormat, 1, VK_FORMAT_UNDEFINED,
			lineDrawPipelineState );

		vkDestroyShaderModule( dc.device, vertBox.module, 0 );
		vkDestroyShaderModule( dc.device, normalCol.module, 0 );
	}
	{
		vk_shader drawCull = VkLoadShader( "Shaders/c_draw_cull.comp.spv", dc.device );
		rndCtx.compPipeline = VkMakeComputePipeline( dc.device, 0, vk.globalLayout, drawCull.module, { 32u } );
		VkDbgNameObj( rndCtx.compPipeline, dc.device, "Pipeline_Comp_DrawCull" );
		
		vk_shader clusterCull = VkLoadShader( "Shaders/c_meshlet_cull.comp.spv", dc.device );
		rndCtx.compClusterCullPipe = VkMakeComputePipeline( dc.device, 0, vk.globalLayout, clusterCull.module, {} );
		VkDbgNameObj( rndCtx.compClusterCullPipe, dc.device, "Pipeline_Comp_ClusterCull" );



		vk_shader expansionComp = VkLoadShader( "Shaders/c_id_expander.comp.spv", dc.device );
		rndCtx.compExpanderPipe = VkMakeComputePipeline( dc.device, 0, vk.globalLayout, expansionComp.module, {} );
		VkDbgNameObj( rndCtx.compExpanderPipe, dc.device, "Pipeline_Comp_iD_Expander" );
		
		vk_shader expMerge = VkLoadShader( "Shaders/comp_expand_merge.comp.spv", dc.device );
		rndCtx.compExpMergePipe = VkMakeComputePipeline( dc.device, 0, vk.globalLayout, expMerge.module, {} );
		VkDbgNameObj( rndCtx.compExpMergePipe, dc.device, "Pipeline_Comp_ExpMerge" );

		vkDestroyShaderModule( dc.device, drawCull.module, 0 );
		vkDestroyShaderModule( dc.device, expansionComp.module, 0 );
		vkDestroyShaderModule( dc.device, clusterCull.module, 0 );
		vkDestroyShaderModule( dc.device, expMerge.module, 0 );
	}
	{
		vk_shader vtxMerged = VkLoadShader( "Shaders/vtx_merged.vert.spv", dc.device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", dc.device );
		vk_gfx_pipeline_state opaqueState = {};

		rndCtx.gfxMergedPipeline = VkMakeGfxPipeline(
			dc.device, 0, vk.globalLayout, 
			vtxMerged.module, fragPBR.module, 
			&renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat, 
			opaqueState );
		VkDbgNameObj( rndCtx.gfxMergedPipeline, dc.device, "Pipeline_Gfx_Merged" );

		vkDestroyShaderModule( dc.device, vtxMerged.module, 0 );
		vkDestroyShaderModule( dc.device, fragPBR.module, 0 );
	}
	if(0){
		vk_shader vertMeshlet = VkLoadShader( "Shaders/meshlet.vert.spv", dc.device );
		vk_shader fragCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );
		vk_gfx_pipeline_state meshletState = {};
		gfxMeshletProgram = VkMakePipelineProgram( 
			dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertMeshlet, &fragCol }, vk.descManager.setLayout );
		rndCtx.gfxMeshletPipeline = VkMakeGfxPipeline(
			dc.device, 0, gfxMeshletProgram.pipeLayout, 
			vertMeshlet.module, fragCol.module, 
			&renderCfg.desiredColorFormat, 1, renderCfg.desiredDepthFormat,
			meshletState );
		VkDbgNameObj( rndCtx.gfxMeshletPipeline, dc.device, "Pipeline_Gfx_MeshletDraw" );

		vkDestroyShaderModule( dc.device, vertMeshlet.module, 0 );
		vkDestroyShaderModule( dc.device, fragCol.module, 0 );
	}
	{
		vk_shader avgLum = VkLoadShader( "Shaders/avg_luminance.comp.spv", dc.device );
		avgLumCompProgram = VkMakePipelineProgram( 
			dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &avgLum }, vk.descManager.setLayout );
		rndCtx.compAvgLumPipe =
			VkMakeComputePipeline( dc.device, 0, avgLumCompProgram.pipeLayout, avgLum.module, { dc.waveSize } );
		VkDbgNameObj( rndCtx.compAvgLumPipe, dc.device, "Pipeline_Comp_AvgLum" );

		vk_shader2 toneMapper = VkLoadShader2( "bin/SpirV/compute_TonemappingGammaCsMain.spv", dc.device );
		rndCtx.compTonemapPipe = VkMakeComputePipeline( 
			dc.device, 0, vk.globalLayout, toneMapper.shaderModule, {}, toneMapper.entryPoint, "Pipeline_Comp_Tonemapping");

		vkDestroyShaderModule( dc.device, avgLum.module, 0 );
	}
	{
		vk_shader2 downsampler = VkLoadShader2( "bin/SpirV/compute_Pow2DownSamplerCsMain.spv", dc.device );
		rndCtx.compHiZPipeline = VkMakeComputePipeline( 
			dc.device, 0, vk.globalLayout, downsampler.shaderModule, {}, downsampler.entryPoint, "Pipeline_Comp_HiZ" );
	}

	vkDbgCtx = VkMakeDebugContext( dc.device, dc.gpuProps, vk.descManager.setLayout );

	imguiVkCtx = ImguiMakeVkContext( dc.device, renderCfg.desiredSwapchainFormat );
}

// TODO: add dynamic state params too 
struct vk_dynamic_scoped_render_pass
{
	VkCommandBuffer cmdBuff;

	vk_dynamic_scoped_render_pass( VkCommandBuffer _cmdBuff, const VkRenderingInfo& renderInfo ) 
	{
		this->cmdBuff = _cmdBuff;
		vkCmdBeginRendering( cmdBuff, &renderInfo );
	}
	~vk_dynamic_scoped_render_pass()
	{
		vkCmdEndRendering( cmdBuff );
	}
};

struct vk_command_buffer
{
	VkCommandBuffer hndl;
	VkPipelineLayout bindlessPipelineLayout;
	VkDescriptorSet bindlessDescriptorSet;

	vk_command_buffer( VkCommandBuffer cmdBuff, VkPipelineLayout pipelineLayout, VkDescriptorSet descriptorSet )
	{
		this->hndl = cmdBuff;
		this->bindlessPipelineLayout = pipelineLayout;
		this->bindlessDescriptorSet = descriptorSet;

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

	vk_dynamic_scoped_render_pass CmdIssueDynamicScopedRenderPass(
		const VkRenderingAttachmentInfo* pColInfos,
		u32 colAttachmentCount,
		const VkRenderingAttachmentInfo* pDepthInfo,
		const VkRect2D& renderArea,
		u32 layerCount = 1
	) {
		VkRenderingInfo renderInfo = {
			.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
			.renderArea = renderArea,
			.layerCount = layerCount,
			.colorAttachmentCount = colAttachmentCount,
			.pColorAttachments = pColInfos,
			.pDepthAttachment = pDepthInfo
		};
		return { hndl, renderInfo };
	}

	void CmdBindBindlessDescriptorSet( VkPipelineBindPoint bindPoint )
	{
		vkCmdBindDescriptorSets( hndl, bindPoint, bindlessPipelineLayout,0, 1, &bindlessDescriptorSet, 0, 0 );
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

	void CmdPipelineBarriers( 
		std::span<VkBufferMemoryBarrier2> buffBarriers, 
		std::span<VkImageMemoryBarrier2> imgBarriers 
	) {
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = ( u32 ) std::size( buffBarriers ),
			.pBufferMemoryBarriers = std::data( buffBarriers ),
			.imageMemoryBarrierCount = ( u32 ) std::size( imgBarriers ),
			.pImageMemoryBarriers = std::data( imgBarriers ),
		};
		vkCmdPipelineBarrier2( hndl, &dependency );
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

	void CmdPipelineBufferBarriers( std::span<VkBufferMemoryBarrier2> buffBarriers ) 
	{
		VkDependencyInfo dependency = {
			.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
			.bufferMemoryBarrierCount = ( u32 ) std::size( buffBarriers ),
			.pBufferMemoryBarriers = std::data( buffBarriers ),
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
		vkCmdFillBuffer( hndl, vkBuffer.hndl, 0, vkBuffer.size, fillValue );
	}

	void CmdEndCmbBuffer()
	{
		VK_CHECK( vkEndCommandBuffer( hndl ) );
	}

	//~vk_command_buffer()
	//{
	//	VK_CHECK( vkEndCommandBuffer( cmdBuff ) );
	//}
};

inline void VkCmdBeginRendering(
	VkCommandBuffer		cmdBuff,
	const VkRenderingAttachmentInfo* pColInfos,
	u32 colAttachmentCount,
	const VkRenderingAttachmentInfo* pDepthInfo,
	const VkRect2D& scissor,
	u32 layerCount = 1
) {
	VkRenderingInfo renderInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = scissor,
		.layerCount = layerCount,
		.colorAttachmentCount = colAttachmentCount,
		.pColorAttachments = pColInfos,
		.pDepthAttachment = pDepthInfo
	};
	vkCmdBeginRendering( cmdBuff, &renderInfo );
}

inline u64 GroupCount( u64 invocationCount, u64 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}


inline static void
FrameClearDataPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline
) {

}

// TODO: alloc buffers here ?
// TODO: reduce the ammount of counter buffers
// TODO: visibility buffer ( not the VBuffer ) check Aaltonen
// TODO: optimize expansion shader
inline static void 
CullPass( 
	vk_command_buffer&  cmdBuff, 
	VkPipeline				vkPipeline, 
	group_size		        grSz,
	const vk_image&			depthPyramid,
	const VkSampler&		minQuadSampler,
	u16 _camIdx,
	u16 _hizBuffIdx,
	u16 samplerIdx
){
	// NOTE: wtf Vulkan ?
	constexpr u64 VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "Cull Pass",{} );

	cmdBuff.CmdFillVkBuffer( drawCountBuff, 0u );
	cmdBuff.CmdFillVkBuffer( drawCountDbgBuff, 0u );
	cmdBuff.CmdFillVkBuffer( meshletCountBuff, 0u );
	cmdBuff.CmdFillVkBuffer( mergedIndexCountBuff, 0u );
	cmdBuff.CmdFillVkBuffer( drawMergedCountBuff, 0u );
	cmdBuff.CmdFillVkBuffer( dispatchCmdBuff0, 0u );
	cmdBuff.CmdFillVkBuffer( dispatchCmdBuff1, 0u );
	// TODO: rename 
	cmdBuff.CmdFillVkBuffer( atomicCounterBuff, 0u );

	VkBufferMemoryBarrier2 beginCullBarriers[] = {
		VkMakeBufferBarrier2( 
			drawCmdBuff.hndl,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		VkMakeBufferBarrier2( 
			drawCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),

	    VkMakeBufferBarrier2( 
			meshletCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
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
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),

		VkMakeBufferBarrier2( 
			dispatchCmdBuff0.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		VkMakeBufferBarrier2( 
			dispatchCmdBuff1.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		
		VkMakeBufferBarrier2(
			indirectMergedIndexBuff.hndl,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		VkMakeBufferBarrier2( 
			atomicCounterBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
	};

	VkImageMemoryBarrier2 hiZReadBarrier[] = { VkMakeImageBarrier2(
		depthPyramid.hndl,
		VK_ACCESS_2_SHADER_WRITE_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT ) };

	cmdBuff.CmdPipelineBarriers( beginCullBarriers, hiZReadBarrier );

	VkMemoryBarrier2 computeToComputeExecDependency[] = { 
		VkMemoryBarrier2{
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT,
		},
	};

	u32 instCount = instDescBuff.size / sizeof( instance_desc );
	{
		
		struct { 
			u64 instDescAddr = instDescBuff.devicePointer;
			u64 meshDescAddr = meshBuff.devicePointer;
			u64 visInstsAddr = intermediateIndexBuff.devicePointer;
			u64 atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
			u64 drawCounterAddr = drawCountBuff.devicePointer;
			u64 dispatchCmdAddr = dispatchCmdBuff0.devicePointer;
			u32	hizBuffIdx = _hizBuffIdx;
			u32	hizSamplerIdx = samplerIdx;
			u32 instanceCount = instCount;
			u32 camIdx = _camIdx;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		vkCmdDispatch( cmdBuff.hndl, GroupCount( instCount, grSz.x ), 1, 1 );
	}
	
	{
		VkBufferMemoryBarrier2 readCmdIndirect[] = {
			VkMakeBufferBarrier2( dispatchCmdBuff0.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH )
		};
		cmdBuff.CmdPipelineBufferBarriers( readCmdIndirect );
		cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );

		struct
		{
			u64 visInstAddr = intermediateIndexBuff.devicePointer;
			u64 visInstCountAddr = drawCountBuff.devicePointer;
			u64 expandeeAddr = indirectMergedIndexBuff.devicePointer;
			u64 expandeeCountAddr = meshletCountBuff.devicePointer;
			u64 atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
			u64 dispatchCmdAddr = dispatchCmdBuff1.devicePointer;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpanderPipe );
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		vkCmdDispatchIndirect( cmdBuff.hndl, dispatchCmdBuff0.hndl, 0 );
	}
	
	{
		VkBufferMemoryBarrier2 readCmdIndirect[] = {
			VkMakeBufferBarrier2( dispatchCmdBuff1.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH )
		};
		cmdBuff.CmdPipelineBufferBarriers( readCmdIndirect );
		cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );

		struct { 
			u64 instDescAddr = instDescBuff.devicePointer;
			u64 meshletDescAddr = meshletBuff.devicePointer;
			u64	inMeshletsIdAddr = indirectMergedIndexBuff.devicePointer;
			u64	inMeshletsCountAddr = meshletCountBuff.devicePointer;
			u64	outMeshletsIdAddr = intermediateIndexBuff.devicePointer;
			u64	outMeshletsCountAddr = drawCountDbgBuff.devicePointer;
			u64	atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
			u64	dispatchCmdAddr = dispatchCmdBuff0.devicePointer;
			u64	dbgDrawCmdsAddr = drawCmdAabbsBuff.devicePointer;
			u32 hizBuffIdx = _hizBuffIdx;
			u32	hizSamplerIdx = samplerIdx;
			u32 camIdx = _camIdx;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compClusterCullPipe );
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		vkCmdDispatchIndirect( cmdBuff.hndl, dispatchCmdBuff1.hndl, 0 );
	}
	
	{
		VkBufferMemoryBarrier2 readCmdIndirect[] = {
			VkMakeBufferBarrier2( dispatchCmdBuff0.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH )
		};
		cmdBuff.CmdPipelineBufferBarriers( readCmdIndirect );
		cmdBuff.CmdPipelineMemoryBarriers( computeToComputeExecDependency );

		struct { 
			u64 meshletDataAddr = meshletDataBuff.devicePointer;
			u64 visMeshletsAddr = intermediateIndexBuff.devicePointer;
			u64 visMeshletsCountAddr = drawCountDbgBuff.devicePointer;
			u64 mergedIdxBuffAddr = indirectMergedIndexBuff.devicePointer;
			u64 mergedIdxCountAddr = mergedIndexCountBuff.devicePointer;
			u64 drawCmdsAddr = drawMergedCmd.devicePointer;
			u64 drawCmdCountAddr = drawMergedCountBuff.devicePointer;
			u64 atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpMergePipe );
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		vkCmdDispatchIndirect( cmdBuff.hndl, dispatchCmdBuff0.hndl, 0 );
	}


	VkBufferMemoryBarrier2 endCullBarriers[] = {
		VkMakeBufferBarrier2( 
			drawCmdBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ),
		VkMakeBufferBarrier2( 
			drawCountBuff.hndl,
			VK_ACCESS_2_SHADER_READ_BIT,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ),
		VkMakeBufferBarrier2( 
			drawCmdDbgBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ),
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

	cmdBuff.CmdPipelineBufferBarriers( endCullBarriers );
}


// TODO: overdraw more efficiently 
static inline void ImguiDrawUiPass(
	const imgui_vk_context& ctx,
	VkCommandBuffer cmdBuff,
	const VkRenderingAttachmentInfo* pColInfo,
	const VkRenderingAttachmentInfo* pDepthInfo,
	const VkRect2D& scissor,
	u64 frameIdx
) {
	static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
	static_assert( sizeof( ImDrawIdx ) == 2 );

	using namespace DirectX;

	const ImDrawData* guiDrawData = ImGui::GetDrawData();

	const vk_buffer& vtxBuff = imguiVkCtx.vtxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	const vk_buffer& idxBuff = imguiVkCtx.idxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];

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

	float scale[ 2 ] = { 2.0f / guiDrawData->DisplaySize.x, 2.0f / guiDrawData->DisplaySize.y };
	float move[ 2 ] = { -1.0f - guiDrawData->DisplayPos.x * scale[ 0 ], -1.0f - guiDrawData->DisplayPos.y * scale[ 1 ] };
	XMFLOAT4 pushConst = { scale[ 0 ],scale[ 1 ],move[ 0 ],move[ 1 ] };


	vk_scoped_label label = { cmdBuff,"Draw Imgui Pass",{} };

	VkCmdBeginRendering( cmdBuff, pColInfo, pColInfo ? 1 : 0, pDepthInfo, scissor, 1 );
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline );

	vk_descriptor_info pushDescs[] = {
		Descriptor( vtxBuff ), {ctx.fontSampler, ctx.fontsView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL} };

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, ctx.descTemplate, ctx.pipelineLayout, 0, pushDescs );
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
			clipMax = { std::min( clipMax.x, ( float ) scissor.extent.width ), 
				std::min( clipMax.y, ( float ) scissor.extent.height ) };

			if( clipMax.x < clipMin.x || clipMax.y < clipMin.y ) continue;

			VkRect2D scissor = { i32( clipMin.x ), i32( clipMin.y ), u32( clipMax.x - clipMin.x ), u32( clipMax.y - clipMin.y ) };
			vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

			vkCmdDrawIndexed( cmdBuff, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
		}
		idxOffset += cmdList->IdxBuffer.Size;
		vtxOffset += cmdList->VtxBuffer.Size;
	}

	vkCmdEndRendering( cmdBuff );
}



// TODO: color depth toggle stuff
inline static void
DebugDrawPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			vkPipeline,
	const VkRenderingAttachmentInfo* pColInfo,
	const VkRenderingAttachmentInfo* pDepthInfo,
	const vk_buffer& drawBuff,
	const vk_program& program,
	const VkRect2D& scissor,
	const mat4& projView,
	range		        drawRange
) {
	vk_scoped_label label = { cmdBuff,"Dbg Draw Pass",{} };

	VkCmdBeginRendering( cmdBuff, pColInfo, pColInfo ? 1 : 0, pDepthInfo, scissor, 1 );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vk_descriptor_info pushDescs[] = { Descriptor( drawBuff ) };
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, pushDescs );
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( mat4 ), &projView );

	vkCmdDraw( cmdBuff, drawRange.size, 1, drawRange.offset, 0 );

	vkCmdEndRendering( cmdBuff );
}

inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	const VkRenderingAttachmentInfo* pColInfo,
	const VkRenderingAttachmentInfo* pDepthInfo,
	const vk_buffer&      drawCmds,
	VkBuffer				drawCmdCount,
	const vk_program&       program,
	const mat4&             viewProjMat,
	const VkRect2D& scissor
){
	vk_scoped_label label = { cmdBuff,"Draw Indirect Pass",{} };

	VkCmdBeginRendering( cmdBuff, pColInfo, pColInfo ? 1 : 0, pDepthInfo, scissor, 1 );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	struct { mat4 viewProj; vec4 color; u64 cmdAddr; u64 transfAddr; u64 meshletAddr; } push = {
		viewProjMat, { 255,0,0,0 }, drawCmds.devicePointer, instDescBuff.devicePointer, meshletBuff.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( push ), &push );

	u32 maxDrawCnt = drawCmds.size / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCmdCount, 0, maxDrawCnt, sizeof( draw_indirect ) );

	vkCmdEndRendering( cmdBuff );
}



// TODO: adjust for more draws ?
inline static void
DrawIndexedIndirectMerged(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	const VkRenderingAttachmentInfo* pColInfo,
	const VkRenderingAttachmentInfo* pDepthInfo,
	VkPipelineLayout       pipelineLayout,
	const vk_buffer&      indexBuff,
	const vk_buffer&      drawCmds,
	const vk_buffer&      drawCount,
	const void* pPushData,
	u64 pushDataSize,
	const VkRect2D& scissor
){
	vk_scoped_label label = { cmdBuff,"Draw Indexed Indirect Merged Pass",{} };

	constexpr u32 maxDrawCount = 1;

	VkCmdBeginRendering( cmdBuff, pColInfo, pColInfo ? 1 : 0, pDepthInfo, scissor, 1 );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );
	
	
	vkCmdPushConstants( cmdBuff, pipelineLayout, VK_SHADER_STAGE_ALL, 0, pushDataSize, pPushData );
	

	vkCmdBindIndexBuffer( cmdBuff, indexBuff.hndl, 0, VK_INDEX_TYPE_UINT32 );

	vkCmdDrawIndexedIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );

	vkCmdEndRendering( cmdBuff );
}

#if 0
// TODO: must remake single pass
inline static void
DepthPyramidPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	u64						mipLevelsCount,
	VkSampler				quadMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const vk_image&			depthTarget,
	const vk_image&			depthPyramid,
	const vk_program&		program 
){
	static_assert( 0 );
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
	depthPyramidDescs[ 0 ] = { 0, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	depthPyramidDescs[ 1 ] = { quadMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 2 ] = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
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
#endif

inline static void
DepthPyramidMultiPass(
	vk_command_buffer		cmdBuff,
	VkPipeline				vkPipeline,
	const vk_image&			depthTarget,
	u16                     depthIdx,
	const vk_image&			depthPyramid,
	u16                     hiZReadIdx,
	const u16*              hiZMipWriteIndices,
	u16                     samplerIdx,
	VkPipelineLayout        pipelineLayout,
	group_size              grSz
) {
	vk_scoped_label label = cmdBuff.CmdIssueScopedLabel( "HiZ Multi Pass", {} );

	VkImageMemoryBarrier2 hizBeginBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2( 
			depthPyramid.hndl,
			0,0,
			VK_ACCESS_2_SHADER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	cmdBuff.CmdPipelineImageBarriers( hizBeginBarriers );


	vkCmdBindPipeline( cmdBuff.hndl, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	VkMemoryBarrier2 executionBarrier[] = { {
			.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
			.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
	} };

	uint mipLevel = 0;
	uint srcImg = depthIdx;
	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
	{
		if( i > 0 )
		{
			mipLevel = i - 1;
			srcImg = hiZReadIdx;
		}
		uint dstImg = hiZMipWriteIndices[ i ];

		u32 levelWidth = std::max( 1u, u32( depthPyramid.width ) >> i );
		u32 levelHeight = std::max( 1u, u32( depthPyramid.height ) >> i );

		vec2 reduceData{ ( float ) levelWidth, ( float ) levelHeight };

		struct push_const
		{
			vec2 reduce;
			uint samplerIdx;
			uint srcImgIdx;
			uint mipLevel;
			uint dstImgIdx;

			push_const(vec2 r, uint s, uint src, uint mip, uint dst)
				: reduce(r), samplerIdx(s), srcImgIdx(src), mipLevel(mip), dstImgIdx(dst) {}
		};
		push_const pushConst{ vec2{(float)levelWidth, (float)levelHeight}, samplerIdx, srcImg, mipLevel, dstImg };
		cmdBuff.CmdPushConstants( &pushConst, sizeof( pushConst ) );
		
		u32 dispatchX = GroupCount( levelWidth, grSz.x );
		u32 dispatchY = GroupCount( levelHeight, grSz.y );
		vkCmdDispatch( cmdBuff.hndl, dispatchX, dispatchY, 1 );

		cmdBuff.CmdPipelineMemoryBarriers( executionBarrier );
	}

	// TODO: do we need ?
	VkImageMemoryBarrier2 hizEndBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT ),
			//VkMakeImageBarrier2(
			//	depthPyramid.hndl,
			//	VK_ACCESS_2_SHADER_WRITE_BIT,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//	VK_ACCESS_2_SHADER_READ_BIT,
			//	VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			//	VK_IMAGE_LAYOUT_GENERAL,
			//	VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
			//	VK_IMAGE_ASPECT_COLOR_BIT )
	};

	cmdBuff.CmdPipelineImageBarriers( hizEndBarriers );
}

// TODO: optimize
inline static void
AverageLuminancePass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			avgPipe,
	const vk_program&   avgProg,
	const vk_buffer&    avgLumBuff,
	const vk_image&     fboHdrColTrg,
	VkImageView         fboHdrColView,
	float				dt
) {
	vk_scoped_label label = { cmdBuff,"Averge Lum Pass",{} };
	// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
	avg_luminance_info avgLumInfo = {
		.minLogLum = -10.0f,
		.invLogLumRange = 1.0f / 12.0f,
		.dt = dt
	};

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, avgPipe );

	vk_descriptor_info avgLumDescs[] = {
		{ 0, fboHdrColView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL },
		Descriptor( avgLumBuff ),
		Descriptor( shaderGlobalsBuff ),
		Descriptor( shaderGlobalSyncCounterBuff )
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, avgProg.descUpdateTemplate, avgProg.pipeLayout, 0, &avgLumDescs[ 0 ] );

	vkCmdPushConstants( cmdBuff, avgProg.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( avgLumInfo ), &avgLumInfo );

	vkCmdDispatch( 
		cmdBuff, 
		GroupCount( fboHdrColTrg.width, avgProg.groupSize.x ), 
		GroupCount( fboHdrColTrg.height, avgProg.groupSize.y ), 1 );
}

// TODO: optimize
inline static void
TonemappingGammaPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			tonePipe,
	u16                 hdrColIdx,
	u16                 sdrColIdx,
	u16                 avgLumIdx,
	VkPipelineLayout    pipelineLayout,
	const vk_image&		fboHdrColTrg,
	group_size          groupSize
) {
	vk_scoped_label label = { cmdBuff,"Tonemapping Gamma Pass",{} };
	
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, tonePipe );
	
	struct push_const
	{
		uint hdrColIdx;
		uint sdrColIdx;
		uint avgLumIdx;

		push_const( uint hdr, uint sdr, uint lum )
			: hdrColIdx(hdr), sdrColIdx(sdr), avgLumIdx(lum){}
	};
	push_const pushConst{ hdrColIdx, sdrColIdx, avgLumIdx };
	vkCmdPushConstants( cmdBuff, pipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof( pushConst ), &pushConst );
	vkCmdDispatch( 
		cmdBuff, GroupCount( fboHdrColTrg.width, groupSize.x ), GroupCount( fboHdrColTrg.height, groupSize.y ), 1 );

}

// TODO: enforce some clearOp ---> clearVals params correctness ?
inline static VkRenderingAttachmentInfo VkMakeAttachemntInfo(
	VkImageView view,
	VkAttachmentLoadOp       loadOp,
	VkAttachmentStoreOp      storeOp,
	VkClearValue             clearValue
) {
	return {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		.loadOp = loadOp,
		.storeOp = storeOp,
		.clearValue = ( loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ) ? clearValue : VkClearValue{},
	};
}

void InitResources( render_path& rndPath, vk_swapchain& sc, std::vector<vk_descriptor_write>& vkDescUpdateCache )
{
	if( rndPath.pHiZTarget == nullptr )
	{
		u16 squareDim = 512;
		u8 hiZMipCount = GetImgMipCountForPow2( squareDim, squareDim, MAX_MIP_LEVELS );

		assert( MAX_MIP_LEVELS >= hiZMipCount );

		constexpr VkImageUsageFlags hiZUsg =
			VK_IMAGE_USAGE_SAMPLED_BIT |
			VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
			VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		image_info hiZInfo = {
			.name = "Img_HiZ",
			.format = VK_FORMAT_R32_SFLOAT,
			.usg = hiZUsg,
			.width = squareDim,
			.height = squareDim,
			.layerCount = 1,
			.mipCount = hiZMipCount
		};

		rndPath.pHiZTarget = std::make_shared<vk_image>( VkCreateAllocBindImage( &dc, hiZInfo ) );

		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( hiZInfo.format );
		rndPath.hiZView = VkMakeImgView(
			dc.device, rndPath.pHiZTarget->hndl, aspectFlags, hiZInfo.format, 0, hiZInfo.mipCount, 
			VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );

		rndPath.hizSrv = VkAllocDescriptorIdx( vk.descManager, 
												  vk_descriptor_info{ rndPath.hiZView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );

		for( u64 i = 0; i < rndPath.pHiZTarget->mipCount; ++i )
		{
			rndPath.hiZMipViews[ i ] = VkMakeImgView( 
				dc.device, rndPath.pHiZTarget->hndl, aspectFlags, hiZInfo.format, i, 1,
				VK_IMAGE_VIEW_TYPE_2D, 0, hiZInfo.layerCount );
			rndPath.hizMipUavs[ i ] = VkAllocDescriptorIdx( vk.descManager,
				vk_descriptor_info{ rndPath.hiZMipViews[ i ], VK_IMAGE_LAYOUT_GENERAL } );
		}

		rndPath.quadMinSampler =
			VkMakeSampler( dc.device, VK_SAMPLER_REDUCTION_MODE_MIN, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE );
		
		rndPath.quadMinSamplerIdx = VkAllocDescriptorIdx( vk.descManager, vk_descriptor_info{ rndPath.quadMinSampler } );
	}
	if( rndPath.pDepthTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		image_info info = {
			.name = "Img_DepthTarget",
			.format = renderCfg.desiredDepthFormat,
			.usg = usgFlags,
			.width = sc.width,
			.height = sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		rndPath.pDepthTarget = std::make_shared<vk_image>( VkCreateAllocBindImage( &dc, info ) );

		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( info.format );
		rndPath.depthView = VkMakeImgView(
			dc.device, rndPath.pDepthTarget->hndl, aspectFlags, info.format, 0, info.mipCount, 
			VK_IMAGE_VIEW_TYPE_2D, 0, info.layerCount );

		rndPath.depthSrv = VkAllocDescriptorIdx( vk.descManager,
			vk_descriptor_info{ rndPath.depthView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	}
	if( rndPath.pColorTarget == nullptr )
	{
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		image_info info = {
			.name = "Img_ColorTarget",
			.format = renderCfg.desiredColorFormat,
			.usg = usgFlags,
			.width = sc.width,
			.height = sc.height,
			.layerCount = 1,
			.mipCount = 1,
		};
		rndPath.pColorTarget = std::make_shared<vk_image>( VkCreateAllocBindImage( &dc, info ) );

		VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( info.format );
		rndPath.colorView = VkMakeImgView(
			dc.device, rndPath.pColorTarget->hndl, aspectFlags, info.format, 0, info.mipCount, 
			VK_IMAGE_VIEW_TYPE_2D, 0, info.layerCount );

		rndPath.colSrv = VkAllocDescriptorIdx( vk.descManager,
			vk_descriptor_info{ rndPath.colorView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
	
		rndPath.pbrSampler = 
			VkMakeSampler( dc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );

		rndPath.pbrSamplerIdx = VkAllocDescriptorIdx( vk.descManager,
			vk_descriptor_info{ rndPath.pbrSampler } );
	}

	for( u64 scImgIdx = 0; scImgIdx < sc.imgCount; ++scImgIdx )
	{
		rndPath.swapchainUavs[ scImgIdx ] = VkAllocDescriptorIdx( 
			vk.descManager, vk_descriptor_info{ sc.imgViews[ scImgIdx ], VK_IMAGE_LAYOUT_GENERAL } );
	}

	rndPath.pAvgLumBuff = std::make_shared<vk_buffer>( VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_AvgLum",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
		.elemCount = 1,
		.stride = sizeof( float ),
		.usage = buffer_usage::GPU_ONLY } ) );
	const vk_buffer& avgLumBuff = *rndPath.pAvgLumBuff;
	rndPath.avgLumIdx = VkAllocDescriptorIdx( vk.descManager, vk_descriptor_info{ avgLumBuff.hndl, 0, avgLumBuff.size } );
}

static std::vector<vk_image> textures;
static std::vector<VkImageView> textureViews;

static inline void VkUploadResources( 
	VkCommandBuffer cmdBuff, 
	entities_data& entities, 
	u64 currentFrameId
) {
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
	//assert( "DRK" == fileFooter.magik  );

	const std::span<mesh_desc> meshes = { 
		(mesh_desc*) ( std::data( binaryData ) + fileFooter.meshesByteRange.offset ),
		fileFooter.meshesByteRange.size / sizeof( mesh_desc ) };

	const std::span<material_data> mtrlDesc = {
		( material_data* ) ( std::data( binaryData ) + fileFooter.mtrlsByteRange.offset ),
		fileFooter.mtrlsByteRange.size / sizeof( material_data ) };

	const std::span<image_metadata> imgDesc = {
		( image_metadata* ) ( std::data( binaryData ) + fileFooter.imgsByteRange.offset ),
		fileFooter.imgsByteRange.size / sizeof( image_metadata ) };


	std::srand( randSeed );

	std::vector<instance_desc> instDesc = SpawnRandomInstances( { std::data( meshes ),std::size( meshes ) }, drawCount, 1, sceneRad );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRad * 0.75f );

	assert( std::size( instDesc ) < u16( -1 ) );


	for( const instance_desc& ii : instDesc )
	{
		const mesh_desc& m = meshes[ ii.meshIdx ];
		entities.transforms.push_back( ii.localToWorld );
		entities.instAabbs.push_back( { m.aabbMin, m.aabbMax } );
	}


	// TODO: make easier to use 
	std::vector<VkBufferMemoryBarrier2> buffBarriers;
	{
		const std::span<u8> vtxView = { std::data( binaryData ) + fileFooter.vtxByteRange.offset, fileFooter.vtxByteRange.size };

		globVertexBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_Vtx",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) std::size( vtxView ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { globVertexBuff.hndl, ( u32 ) std::size( vtxView ), ( const u8* ) std::data( vtxView ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			globVertexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );
	}
	{
		const std::span<u8> idxSpan = { std::data( binaryData ) + fileFooter.idxByteRange.offset, fileFooter.idxByteRange.size };

		indexBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_Idx",
			.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) std::size( idxSpan ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { indexBuff.hndl, ( u32 ) std::size( idxSpan ), ( const u8* ) std::data( idxSpan ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2( 
			indexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_INDEX_READ_BIT, 
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT ) );
	}
	{
		meshBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_MeshDesc",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( BYTE_COUNT( meshes ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } ); 

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { meshBuff.hndl, ( u32 ) ( BYTE_COUNT( meshes ) ), ( const u8* ) std::data( meshes ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );
	}
	{
		lightsBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_Lights",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( BYTE_COUNT( lights ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { lightsBuff.hndl, ( u32 ) ( BYTE_COUNT( lights ) ), ( const u8* ) std::data( lights ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			lightsBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ) );
	}
	{
		instDescBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_InstDescs",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( BYTE_COUNT( instDesc ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { instDescBuff.hndl, ( u32 ) ( BYTE_COUNT( instDesc ) ), ( const u8* ) std::data( instDesc ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			instDescBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ) );

	}
	{
		const std::span<u8> mletView = { std::data( binaryData ) + fileFooter.mletsByteRange.offset,fileFooter.mletsByteRange.size };

		assert( fileFooter.mletsByteRange.size < u16( -1 ) * sizeof( meshlet ) );

		meshletBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_Meshlets",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( std::size( mletView ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { meshletBuff.hndl, ( u32 ) ( std::size( mletView ) ), ( const u8* ) std::data( mletView ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );
	}
	{
		const std::span<u8> mletDataView = { 
			std::data( binaryData ) + fileFooter.mletsDataByteRange.offset,
			fileFooter.mletsDataByteRange.size };

		meshletDataBuff = VkCreateAllocBindBuffer( &dc, {
			.name = "Buff_MeshletData",
			.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			.elemCount = (u32) ( std::size( mletDataView ) ),
			.stride = 1,
			.usage = buffer_usage::GPU_ONLY } );  

		StagingManagerBufferCopy( stagingManager, cmdBuff,
								  { meshletDataBuff.hndl, ( u32 ) ( std::size( mletDataView ) ), ( const u8* ) std::data( mletDataView ) },
								  currentFrameId );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletDataBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) );
	}


	drawCmdBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_IndirectDrawCmds",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = std::size( instDesc ),
		.stride = sizeof( draw_command ),
		.usage = buffer_usage::GPU_ONLY } );   

	drawCmdDbgBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_IndirectDrawCmds",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = std::size( instDesc ),
		.stride = sizeof( draw_command ),
		.usage = buffer_usage::GPU_ONLY } );  

	// TODO: expose from asset compiler 
	constexpr u64 MAX_TRIS = 256;
	//u64 maxByteCountMergedIndexBuff = std::size( instDesc ) * ( meshletBuff.size / sizeof( meshlet ) ) * MAX_TRIS * 3ull;
	u64 maxByteCountMergedIndexBuff = 10 * MB;

	intermediateIndexBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_IntermediateIdx",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = maxByteCountMergedIndexBuff,
		.stride = 1,
		.usage = buffer_usage::GPU_ONLY } );

	indirectMergedIndexBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_MergedIdx",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = maxByteCountMergedIndexBuff,
		.stride = 1,
		.usage = buffer_usage::GPU_ONLY } ); 

	drawCmdAabbsBuff = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_AabbsDrawCms",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 10'000,
		.stride = sizeof( draw_indirect ),
		.usage = buffer_usage::GPU_ONLY } );  

	drawMergedCmd = VkCreateAllocBindBuffer( &dc, {
		.name = "Buff_MergedDrawCmd",
		.usageFlags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		.elemCount = 1,
		.stride = sizeof( draw_command ),
		.usage = buffer_usage::GPU_ONLY } );

	auto CreateStagingBuffAndMemCopy = [&] ( const u8* pData, u32 elemCount, u32 stride ) -> vk_buffer
	{
		VkBufferUsageFlags usg = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		vk_buffer stagingBuff = VkCreateAllocBindBuffer( &dc, {
			.usageFlags = usg, .elemCount = elemCount, .stride = stride, .usage = buffer_usage::STAGING } );
		std::memcpy( stagingBuff.hostVisible, pData, stagingBuff.size );
		StagingManagerPushForRecycle( stagingBuff.hndl, stagingManager, currentFrameId );
		return stagingBuff;
	};

	// NOTE: create and texture uploads
	std::vector<VkImageMemoryBarrier2> imageBarriers;
	{
		imageBarriers.reserve( std::size( imgDesc ) );

		u64 newTexturesOffset = std::size( textures );

		for( const image_metadata& meta : imgDesc )
		{
			image_info info = GetImageInfoFromMetadata( meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
			vk_image img = VkCreateAllocBindImage( &dc, info );
			textures.push_back( img );

			imageBarriers.push_back( VkMakeImageBarrier2(
				img.hndl,
				0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}

		VkDependencyInfo imitImagesDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
		imitImagesDependency.imageMemoryBarrierCount = std::size( imageBarriers );
		imitImagesDependency.pImageMemoryBarriers = std::data( imageBarriers );
		vkCmdPipelineBarrier2( cmdBuff, &imitImagesDependency );

		imageBarriers.resize( 0 );

		assert( u32( -1 ) >= fileFooter.texBinByteRange.size );

		const u8* pTexBinData = std::data( binaryData ) + fileFooter.texBinByteRange.offset;
		vk_buffer stagingBuff = CreateStagingBuffAndMemCopy( pTexBinData, fileFooter.texBinByteRange.size, 1 );

		for( u64 i = 0; i < std::size( imgDesc ); ++i )
		{
			const vk_image& dst = textures[ i + newTexturesOffset ];

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
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}

		auto CreateImgViewAndAllocDscIdx = [&] ( const vk_image& tex ) -> u16
			{
				VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( tex.nativeFormat );
				VkImageView imgView = VkMakeImgView(
					dc.device, tex.hndl, aspectFlags, tex.nativeFormat, 0, tex.mipCount, VK_IMAGE_VIEW_TYPE_2D, 0, tex.layerCount );
				textureViews.push_back( imgView );

				return VkAllocDescriptorIdx( vk.descManager, vk_descriptor_info{ imgView, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL } );
			};

		textureViews.reserve( std::size( textures ) );
		// NOTE: we assume materials have an abs idx for the textures
		std::vector<material_data> mtrls = {};
		for( const material_data& m : mtrlDesc )
		{
			mtrls.push_back( m );
			material_data& refM = mtrls[ std::size( mtrls ) - 1 ];

			const auto& mBaseCol = textures[ m.baseColIdx ];
			const auto& mNormalMap = textures[ m.normalMapIdx ];
			const auto& mOccRoughMetal = textures[ m.occRoughMetalIdx ];

			refM.baseColIdx = CreateImgViewAndAllocDscIdx( mBaseCol );
			refM.normalMapIdx = CreateImgViewAndAllocDscIdx( mNormalMap );
			refM.occRoughMetalIdx = CreateImgViewAndAllocDscIdx( mOccRoughMetal );
		}
		{
			VkBufferUsageFlags usg =
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

			materialsBuff = VkCreateAllocBindBuffer( &dc, {
				.name = "Buff_Mtrls",
				.usageFlags = usg,
				.elemCount = ( u32 ) std::size( mtrls ),
				.stride = sizeof( decltype( mtrls )::value_type ),
				.usage = buffer_usage::GPU_ONLY
													 } );

			StagingManagerBufferCopy( stagingManager, cmdBuff,
									  { materialsBuff.hndl, ( u32 ) ( BYTE_COUNT( mtrls ) ), ( const u8* ) std::data( mtrls ) },
									  currentFrameId );
			buffBarriers.push_back( VkMakeBufferBarrier2(
				materialsBuff.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ) );
		}
	}

	VkDependencyInfo uploadDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	uploadDependency.bufferMemoryBarrierCount = std::size( buffBarriers );
	uploadDependency.pBufferMemoryBarriers = std::data( buffBarriers );
	uploadDependency.imageMemoryBarrierCount = std::size( imageBarriers );
	uploadDependency.pImageMemoryBarriers = std::data( imageBarriers );
	vkCmdPipelineBarrier2( cmdBuff, &uploadDependency );
}

// TODO: in and out data
void HostFrames( const frame_data& frameData, gpu_data& gpuData )
{
	u64 currentFrameIdx = rndCtx.vFrameIdx++;
	u64 currentFrameBufferedIdx = currentFrameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
	const virtual_frame& thisVFrame = rndCtx.vrtFrames[ currentFrameBufferedIdx ];

	VkSemaphoreWaitInfo waitInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = 1,
		.pSemaphores = &rndCtx.timelineSema,
		.pValues = &currentFrameIdx,
	};
	VK_CHECK( VK_INTERNAL_ERROR( vkWaitSemaphores( dc.device, &waitInfo, UINT64_MAX ) > VK_TIMEOUT ) );

	VK_CHECK( vkResetCommandPool( dc.device, thisVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

	// TODO: no copy
	global_data globs = {
		.proj = frameData.proj,
		.mainView = frameData.mainView,
		.activeView = frameData.activeView,
		.worldPos = frameData.worldPos,
		.camViewDir = frameData.camViewDir,
	};
	std::memcpy( thisVFrame.frameData->hostVisible, &globs, sizeof( globs ) );

	// TODO: move out of this 
	dbgLineGeomCache = ComputeSceneDebugBoundingBoxes( XMLoadFloat4x4A( &frameData.frustTransf ), entities );
	// TODO: might need to double buffer
	std::memcpy( vkDbgCtx.dbgLinesBuff.hostVisible, std::data( dbgLineGeomCache ), BYTE_COUNT( dbgLineGeomCache ) );

	// TODO: remove from here
	static bool rscCpy = false;
	if( !rscCpy )
	{
		{
			using namespace DirectX;

			XMMATRIX t = XMMatrixMultiply( 
				XMMatrixScaling( 100.0f, 60.0f, 20.0f ), XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
			XMFLOAT4 boxVertices[ 8 ] = {};
			TrnasformBoxVertices( t, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f }, boxVertices );
			std::span<dbg_vertex> occlusionBoxSpan = { ( dbg_vertex* ) vkDbgCtx.dbgTrisBuff.hostVisible,boxTrisVertexCount };
			assert( std::size( occlusionBoxSpan ) == std::size( boxTrisIndices ) );
			for( u64 i = 0; i < std::size( occlusionBoxSpan ); ++i )
			{
				occlusionBoxSpan[ i ] = { boxVertices[ boxTrisIndices[ i ] ],{0.000004f,0.000250f,0.000123f,1.0f} };
			}
		}
		
		rscCpy = true;
	}
	
	vk_command_buffer thisFrameCmdBuffer = { thisVFrame.cmdBuff, vk.globalLayout, vk.descManager.set };
	
	// TODO: 
	if( currentFrameIdx < VK_MAX_FRAMES_IN_FLIGHT_ALLOWED )
	{
		vk_descriptor_info srvInfo = { thisVFrame.frameData->hndl, 0, sizeof( globs ) };
		u16 globalsSrv = VkAllocDescriptorIdx( vk.descManager, srvInfo );
		// TODO: 
		const_cast< virtual_frame& >( thisVFrame ).frameDescIdx = globalsSrv;

		// NOTE: must be reset before use
		VkResetGpuTimer( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	}

	std::vector<vk_descriptor_write> vkDescUpdateCache;
	static bool initResources = false;
	if( !initResources )
	{
		InitResources( renderPath, vk.sc, vkDescUpdateCache );

		if( !imguiVkCtx.fontsImg.hndl )
		{
			auto [pixels, width, height] = ImguiGetFontImage();

			constexpr VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
			constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			image_info info = {
				.name = "Img_ImGuiFonts",
				.format = format,
				.usg = usgFlags,
				.width = (u16)width,
				.height = (u16)height,
				.layerCount = 1,
				.mipCount = 1,
			};
			imguiVkCtx.fontsImg = VkCreateAllocBindImage( &dc, info );

			VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( info.format );
			imguiVkCtx.fontsView = VkMakeImgView(
				dc.device, imguiVkCtx.fontsImg.hndl, aspectFlags, info.format, 0, info.mipCount, 
				VK_IMAGE_VIEW_TYPE_2D, 0, info.layerCount );

		}

		thisFrameCmdBuffer.CmdFillVkBuffer( depthAtomicCounterBuff, 0u );

		VkBufferMemoryBarrier2 initBuffersBarriers[] = {
			VkMakeBufferBarrier2( depthAtomicCounterBuff.hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		};

		VkImageMemoryBarrier2 initBarriers[] = {
			VkMakeImageBarrier2( 
				imguiVkCtx.fontsImg.hndl, 0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT )
		};

		thisFrameCmdBuffer.CmdPipelineBarriers( initBuffersBarriers, initBarriers );
		initResources = true;
	}
	

	static bool rescUploaded = 0;
	if( !rescUploaded )
	{
		auto [pixels, width, height] = ImguiGetFontImage();

		u64 uploadSize = width * height * sizeof( u32 );
		buffer_info buffInfo = {
			.name = "Buff_ImGuiTexStaging",
			.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT,
			.elemCount = width * height * sizeof( u32 ),
			.stride = 1,
			.usage = buffer_usage::STAGING
		};
		vk_buffer upload = VkCreateAllocBindBuffer( &dc, buffInfo );
		std::memcpy( upload.hostVisible, pixels, uploadSize );
		StagingManagerPushForRecycle( upload.hndl, stagingManager, currentFrameIdx );

		VkBufferImageCopy imgCopyRegion = {};
		imgCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imgCopyRegion.imageSubresource.layerCount = 1;
		imgCopyRegion.imageExtent = { width,height,1 };
		vkCmdCopyBufferToImage(
			thisVFrame.cmdBuff, upload.hndl, imguiVkCtx.fontsImg.hndl,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );

		VkImageMemoryBarrier2 fontsBarrier[] = { VkMakeImageBarrier2( imguiVkCtx.fontsImg.hndl,
												 VK_ACCESS_2_TRANSFER_WRITE_BIT,
												 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
												 VK_ACCESS_2_SHADER_READ_BIT,
												 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
												 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
												 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
												 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( {}, fontsBarrier );

		VkUploadResources( thisVFrame.cmdBuff, entities, currentFrameIdx );
		rescUploaded = 1;
	}

	VkDescriptorManagerFlushUpdates( vk.descManager, dc.device );

	const vk_image& depthTarget = *renderPath.pDepthTarget;
	const vk_image& depthPyramid = *renderPath.pHiZTarget;
	const vk_image& colorTarget = *renderPath.pColorTarget;

	auto depthWrite = VkMakeAttachemntInfo( renderPath.depthView, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto depthRead = VkMakeAttachemntInfo( renderPath.depthView, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorWrite = VkMakeAttachemntInfo( renderPath.colorView, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorRead = VkMakeAttachemntInfo( renderPath.colorView, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );

	u32 imgIdx;
	{
		vk_time_section timePipeline = { thisVFrame.cmdBuff, thisVFrame.gpuTimer.queryPool, 0 };

		VkViewport viewport = { 0.0f, ( float ) vk.sc.height, ( float ) vk.sc.width, -( float ) vk.sc.height, 0.0f, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &viewport );
		VkRect2D scissor = { { 0, 0 }, { vk.sc.width, vk.sc.height } };

		thisFrameCmdBuffer.CmdBindBindlessDescriptorSet( VK_PIPELINE_BIND_POINT_GRAPHICS );

		VkImageMemoryBarrier2 acquireDepthBarriers[] = {
			VkMakeImageBarrier2(
				depthTarget.hndl,
				0, 0,
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
				VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_IMAGE_ASPECT_DEPTH_BIT ),
		};
		thisFrameCmdBuffer.CmdPipelineBarriers( {}, acquireDepthBarriers );

		struct { u64 vtxAddr, transfAddr, camIdx; } zPrepassPush = { 
			globVertexBuff.devicePointer, instDescBuff.devicePointer, thisVFrame.frameDescIdx };

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			rndCtx.gfxZPrepass,
			0,
			&depthWrite,
			vk.globalLayout,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			&zPrepassPush,
			sizeof(zPrepassPush),
			scissor
		);

		VkImageMemoryBarrier2 acquireColBarriers[] = {
			VkMakeImageBarrier2(
				colorTarget.hndl,
				0, 
				0, //VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
				VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 
				VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ),
		};
		thisFrameCmdBuffer.CmdPipelineBarriers( {}, acquireColBarriers );

		DebugDrawPass(
			thisVFrame.cmdBuff,
			vkDbgCtx.drawAsTriangles,
			0,
			&depthRead,
			vkDbgCtx.dbgTrisBuff,
			vkDbgCtx.pipeProg,
			scissor,
			frameData.mainProjView,
			{ 0,boxTrisVertexCount } );

		thisFrameCmdBuffer.CmdBindBindlessDescriptorSet( VK_PIPELINE_BIND_POINT_COMPUTE );

		DepthPyramidMultiPass(
			thisFrameCmdBuffer,
			rndCtx.compHiZPipeline,
			depthTarget,
			renderPath.depthSrv,
			depthPyramid,
			renderPath.hizSrv,
			renderPath.hizMipUavs,
			renderPath.quadMinSamplerIdx,
			vk.globalLayout,
			{32,31,1}
		 );


		VkBufferMemoryBarrier2 clearDrawCountBarrier[] = { VkMakeBufferBarrier2(
			//drawCountBuff.hndl,
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( clearDrawCountBarrier, {} );

		// TODO: Aaltonen double pass
		CullPass( 
			thisFrameCmdBuffer, 
			rndCtx.compPipeline, 
			{32, 1, 1},
			depthPyramid, 
			renderPath.quadMinSampler,
			thisVFrame.frameDescIdx,
			renderPath.hizSrv,
			renderPath.quadMinSamplerIdx
		);


		thisFrameCmdBuffer.CmdBindBindlessDescriptorSet( VK_PIPELINE_BIND_POINT_GRAPHICS );

		//DrawIndexedIndirectMerged(
		//	thisVFrame.cmdBuff,
		//	gfxZPrepass,
		//	0,
		//	&depthWrite,
		//	vk.globalLayout,
		//	indirectMergedIndexBuff,
		//	drawMergedCmd,
		//	drawMergedCountBuff,
		//	&zPrepassPush,
		//	sizeof(zPrepassPush)
		//);
		

		struct { u64 vtxAddr, transfAddr, camIdx, mtrlsAddr, lightsAddr, samplerIdx;
		} shadingPush = { 
					 globVertexBuff.devicePointer, 
					 instDescBuff.devicePointer, 
					 thisVFrame.frameDescIdx,
					 materialsBuff.devicePointer, 
					 lightsBuff.devicePointer,
					 renderPath.pbrSamplerIdx
		};

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			rndCtx.gfxMergedPipeline,
			&colorWrite,
			&depthWrite,
			vk.globalLayout,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			&shadingPush,
			sizeof(shadingPush),
			scissor
		);

		DebugDrawPass(
			thisVFrame.cmdBuff,
			vkDbgCtx.drawAsTriangles,
			&colorRead,
			&depthRead,
			vkDbgCtx.dbgTrisBuff,
			vkDbgCtx.pipeProg,
			scissor,
			frameData.activeProjView,
			{ 0,boxTrisVertexCount } );

		if( dbgDraw && ( frameData.freezeMainView || frameData.dbgDraw ) )
		{
			u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;

			range drawRange = {};
			drawRange.offset = frameData.dbgDraw ? 0 : frustBoxOffset;
			drawRange.size = ( frameData.freezeMainView && frameData.dbgDraw ) ?
				std::size( dbgLineGeomCache ) : ( frameData.freezeMainView ? boxLineVertexCount : frustBoxOffset );


			DebugDrawPass( thisVFrame.cmdBuff,
						   vkDbgCtx.drawAsLines,
						   &colorRead,
						   0,
						   vkDbgCtx.dbgLinesBuff,
						   vkDbgCtx.pipeProg,
						   scissor,
						   frameData.activeProjView,
						   drawRange );

			if( frameData.dbgDraw )
			{
				DrawIndirectPass( thisVFrame.cmdBuff,
								  gfxDrawIndirDbg,
								  &colorRead,
								  0,
								  drawCmdAabbsBuff,
								  drawCountDbgBuff.hndl,
								  dbgDrawProgram,
								  frameData.activeProjView, scissor );
			}

		}

		thisFrameCmdBuffer.CmdFillVkBuffer( shaderGlobalsBuff, 0u );
		thisFrameCmdBuffer.CmdFillVkBuffer( shaderGlobalSyncCounterBuff, 0u );

		VkBufferMemoryBarrier2 zeroInitGlobals[] = {
			VkMakeBufferBarrier2( shaderGlobalsBuff.hndl,
								  VK_ACCESS_2_TRANSFER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_TRANSFER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
								  VkMakeBufferBarrier2( shaderGlobalSyncCounterBuff.hndl,
														VK_ACCESS_2_TRANSFER_WRITE_BIT,
														VK_PIPELINE_STAGE_2_TRANSFER_BIT,
														VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
														VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT )
		};
		VkImageMemoryBarrier2 hrdColTargetAcquire[] = { VkMakeImageBarrier2( colorTarget.hndl,
																		 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
																		 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
																		 VK_ACCESS_2_SHADER_READ_BIT,
																		 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
																		 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
																		 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
																		 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( zeroInitGlobals, hrdColTargetAcquire );

		AverageLuminancePass(
			thisVFrame.cmdBuff,
			rndCtx.compAvgLumPipe,
			avgLumCompProgram,
			*renderPath.pAvgLumBuff,
			colorTarget,
			renderPath.colorView,
			frameData.elapsedSeconds );

		
		VK_CHECK( vkAcquireNextImageKHR( dc.device, vk.sc.swapchain, UINT64_MAX, thisVFrame.canGetImgSema, 0, &imgIdx ) );

		// NOTE: we need exec dependency from acquireImgKHR ( col out + compute shader ) to compute shader
		VkImageMemoryBarrier2 scWriteBarrier[] =
		{ VkMakeImageBarrier2( vk.sc.imgs[ imgIdx ],
								 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 0,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 VK_IMAGE_LAYOUT_UNDEFINED,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		VkBufferMemoryBarrier2 avgLumReadBarrier[] =
		{ VkMakeBufferBarrier2( renderPath.pAvgLumBuff->hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( avgLumReadBarrier, scWriteBarrier );

		thisFrameCmdBuffer.CmdBindBindlessDescriptorSet( VK_PIPELINE_BIND_POINT_COMPUTE );

		VK_CHECK( VK_INTERNAL_ERROR( ( colorTarget.width != vk.sc.width ) || ( colorTarget.height != vk.sc.height ) ) );
		TonemappingGammaPass( thisVFrame.cmdBuff,
							  rndCtx.compTonemapPipe,
							  renderPath.colSrv,
							  renderPath.swapchainUavs[ imgIdx ],
							  renderPath.avgLumIdx,
							  vk.globalLayout,
							  colorTarget,
							  { 16,16,1 } );

		VkImageMemoryBarrier2 compositionEndBarriers[] = {
			VkMakeImageBarrier2( colorTarget.hndl,
								 VK_ACCESS_2_SHADER_READ_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 0, 0,
								 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ),
			VkMakeImageBarrier2( vk.sc.imgs[ imgIdx ],
								 VK_ACCESS_2_SHADER_WRITE_BIT,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
								 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( {}, compositionEndBarriers );

		VkViewport uiViewport = { 0, 0, ( float ) vk.sc.width, ( float ) vk.sc.height, 0, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &uiViewport );

		auto swapchainUIRW = VkMakeAttachemntInfo( 
			vk.sc.imgViews[ imgIdx ], VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
		ImguiDrawUiPass( 
			imguiVkCtx, 
			thisVFrame.cmdBuff, 
			&swapchainUIRW,
			0,
			scissor,
			currentFrameIdx );


		VkImageMemoryBarrier2 presentWaitBarrier[] = { VkMakeImageBarrier2(
			vk.sc.imgs[ imgIdx ],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT ) };

		thisFrameCmdBuffer.CmdPipelineBarriers( {}, presentWaitBarrier );
	}

	gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, thisVFrame.gpuTimer );
	VkResetGpuTimer( thisVFrame.cmdBuff, thisVFrame.gpuTimer );

	thisFrameCmdBuffer.CmdEndCmbBuffer();


	VkSemaphore signalSemas[] = { vk.sc.canPresentSemas[ imgIdx ], rndCtx.timelineSema };
	u64 signalValues[] = { 0, rndCtx.vFrameIdx }; // NOTE: this is the next frame val
	
	VkTimelineSemaphoreSubmitInfo timelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.signalSemaphoreValueCount = std::size( signalValues ),
		.pSignalSemaphoreValues = signalValues,
	};

	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;// VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	VkSubmitInfo submitInfo = { 
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = &timelineInfo,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &thisVFrame.canGetImgSema,
		.pWaitDstStageMask = &waitDstStageMsk,
		.commandBufferCount = 1,
		.pCommandBuffers = &thisVFrame.cmdBuff,
		.signalSemaphoreCount = std::size( signalSemas ),
		.pSignalSemaphores = signalSemas,
	};
	// NOTE: queue submit has implicit host sync for trivial stuff so in theroy we shouldn't worry about memcpy
	VK_CHECK( vkQueueSubmit( dc.gfxQueue, 1, &submitInfo, 0 ) );

	VkPresentInfoKHR presentInfo = { 
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &vk.sc.canPresentSemas[ imgIdx ],
		.swapchainCount = 1,
		.pSwapchains = &vk.sc.swapchain,
		.pImageIndices = &imgIdx
	};
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
	vkDestroyDebugUtilsMessengerEXT( vk.inst.hndl, vk.inst.dbgMsg, 0 );
#endif
	vkDestroySurfaceKHR( vk.inst.hndl, vk.surf, 0 );
	vkDestroyInstance( vk.inst.hndl, 0 );

	SysDllUnload( vk.inst.dll );
}

#undef HTVK_NO_SAMPLER_REDUCTION
#undef VK_APPEND_DESTROYER
#undef VK_CHECK