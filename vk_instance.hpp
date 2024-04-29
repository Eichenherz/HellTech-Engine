#pragma once

// NOTE: GPU validation broken
// NOTE: Sync validation doesn't work with desc indexing ?
constexpr VkValidationFeatureEnableEXT enabledValidationFeats[] = {
	//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
	//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
	VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
	//VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
	//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
};

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

struct vk_instance
{
	u64 vkDllHandle;
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMsg;
};

// NOTE: StOOPid Cpp
inline static std::tuple<u64, VkInstance, VkDebugUtilsMessengerEXT> VkMakeInstance()
{
	u64 vkDllHandle = SysDllLoad( "vulkan-1.dll" );

	VK_CHECK( VK_INTERNAL_ERROR( !vkDllHandle ) );

	vkGetInstanceProcAddr = ( PFN_vkGetInstanceProcAddr ) SysGetProcAddr( vkDllHandle, "vkGetInstanceProcAddr" );
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

	VkApplicationInfo appInfo = { 
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, 
		.pApplicationName = "HellTech Engine",
		.applicationVersion = APP_VERSION
	};
	VK_CHECK( vkEnumerateInstanceVersion( &appInfo.apiVersion ) );
	VK_CHECK( VK_INTERNAL_ERROR( appInfo.apiVersion < VK_API_VERSION ) );

	VkInstanceCreateInfo instInfo = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = std::size( LAYERS ),
		.ppEnabledLayerNames = LAYERS,
		.enabledExtensionCount = std::size( ENABLED_INST_EXTS ),
		.ppEnabledExtensionNames = ENABLED_INST_EXTS
	};
#ifdef _VK_DEBUG_

	VkValidationFeaturesEXT vkValidationFeatures = {
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = std::size( enabledValidationFeats ),
		.pEnabledValidationFeatures = enabledValidationFeats
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

	return { vkDllHandle, vkInstance, vkDbgUtilsMsgExt };
}
