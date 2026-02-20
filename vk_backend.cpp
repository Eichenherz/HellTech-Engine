#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES

#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#define VOLK_IMPLEMENTATION 
#include <Volk/volk.h>

#include <vector>
#include <cstdarg>
#include <format>
#include <span>
#include <array>

#define VMA_IMPLEMENTATION

//#define VMA_DEBUG_LOG( str ) do { std::fputs( std::format( "[VMA] {}\n", str ).c_str(), stdout ); } while ( 0 )

//#define VMA_DEBUG_LOG_FORMAT( fmt, ... ) \
//    do { std::fputs( std::format( "[VMA] " fmt "\n", __VA_ARGS__ ).c_str(), stdout ); } while ( 0 )

#include <vk_mem_alloc.h>

#include "core_types.h"
#include "ht_error.h"
#include "ht_mem_arena.h"
#include "vk_error.h"
#include "vk_context.h"
#include "vk_pso.h"
#include "vk_resources.h"
#include "vk_swapchain.h"

constexpr VkValidationFeatureEnableEXT enabledValidationFeats[] = {
	//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
	//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
	VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
	VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
	//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
};


inline static VkMemoryPropertyFlags VkChooseMemoryProperitesOnUsage( buffer_usage usage )
{
	using enum buffer_usage;
	switch( usage )
	{
	case GPU_ONLY: return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	case HOST_VISIBLE: return 
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	case STAGING: return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	default: assert( 0 && "Uknown memory type" );
	}
	return 0;
}

// NOTE: from Sascha Willems
static u32 VkGetQueueFamilyIndex( 
	std::span<const VkQueueFamilyProperties> queueFamProps, 
	VkQueueFlags queueFlags,
	VkBool32 mustPresent,
	VkPhysicalDevice gpu,
	VkSurfaceKHR vkSurf
) {
	if( ( queueFlags & VK_QUEUE_TRANSFER_BIT ) == queueFlags )
	{
		for( u32 qfi = 0; qfi < ( u32 ) std::size( queueFamProps ); qfi++ )
		{
			VkQueueFamilyProperties famProps = queueFamProps[ qfi ];
			bool hasTransfer = queueFamProps[ qfi ].queueFlags & VK_QUEUE_TRANSFER_BIT;
			bool hasCompute = famProps.queueFlags & VK_QUEUE_COMPUTE_BIT;
			bool hasGfx = queueFamProps[ qfi ].queueFlags & VK_QUEUE_GRAPHICS_BIT; 
			if( hasTransfer && !hasCompute && !hasGfx )
			{
				return qfi;
			}
		}
	}

	if( ( queueFlags & VK_QUEUE_COMPUTE_BIT ) == queueFlags )
	{
		for( u32 qfi = 0; qfi < ( u32 ) std::size( queueFamProps ); qfi++)
		{
			VkQueueFamilyProperties famProps = queueFamProps[ qfi ];
			bool hasCompute = famProps.queueFlags & VK_QUEUE_COMPUTE_BIT;
			bool hasGfx = queueFamProps[ qfi ].queueFlags & VK_QUEUE_GRAPHICS_BIT;
			if( hasCompute && !hasGfx )
			{
				if( mustPresent )
				{
					VkBool32 hasPresent = 0;
					vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qfi, vkSurf, &hasPresent );
					if( !hasPresent ) continue;
				}
				return qfi;
			}
		}
	}

	// NOTE: For other queue types or if no separate compute queue is present,
	// return the first one to support the requested flags
	for( u32 qfi = 0; qfi < ( u32 ) std::size( queueFamProps ); qfi++ )
	{
		if( ( queueFamProps[ qfi ].queueFlags & queueFlags ) == queueFlags )
		{
			if( mustPresent )
			{
				VkBool32 hasPresent = 0;
				vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qfi, vkSurf, &hasPresent );
				if( !hasPresent ) continue;
			}
			return qfi;
		}
	}

	HT_ASSERT( 0 && "No queue found that matches the requirements !" );
	return ~0u;
}

inline static VkDeviceAddress VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
	VkBufferDeviceAddressInfo deviceAddrInfo = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = hndl };
	return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
}

static vk_queue VkCreateQueue( 
	VkDevice        vkDevice, 
	u32             queueFamilyIndex,
	VkQueueFlags    desiredProps,
	u64				initialTimelineVal 
) {
	VkQueue	hndl;
	vkGetDeviceQueue( vkDevice, queueFamilyIndex, 0, &hndl );
	HT_ASSERT( hndl );

	VkSemaphoreTypeCreateInfo timelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = initialTimelineVal,
	};
	VkSemaphoreCreateInfo timelineSemaInfo = { 
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, 
		.pNext = &timelineInfo 
	};

	VkSemaphore timelineSema;
	VK_CHECK( vkCreateSemaphore( vkDevice, &timelineSemaInfo, 0, &timelineSema ) );

	return {
		.hndl = hndl,
		.timelineSema = timelineSema,
		.submitionCount = 0,
		.familyIdx = queueFamilyIndex,
		.familyFlags = desiredProps
	};
}

static VmaAllocator MakeVmaAllocator( VkPhysicalDevice vkGpu, VkDevice vkDevice, VkInstance vkInst, u32 vkVersion )
{
	constexpr VmaAllocatorCreateFlags createFlags =
		VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
		VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;

	VmaVulkanFunctions vulkanFunctions = {
		.vkGetInstanceProcAddr = vkGetInstanceProcAddr,
		.vkGetDeviceProcAddr = vkGetDeviceProcAddr,
	};

	// TODO: which are default, which do we have ?
	VmaAllocatorCreateInfo allocatorCreateInfo = {
		.flags = createFlags,
		.physicalDevice = vkGpu,
		.device = vkDevice,
		.pVulkanFunctions = &vulkanFunctions,
		.instance = vkInst,
		.vulkanApiVersion = vkVersion 
	};

	VmaAllocator vmaAllocator;
	vmaCreateAllocator( &allocatorCreateInfo, &vmaAllocator );

	return vmaAllocator;
}

inline constexpr VkDescriptorType VkDescBindingToType( vk_desc_binding_t binding )
{
	using enum vk_desc_binding_t;
	switch( binding )
	{
	case SAMPLER: return VK_DESCRIPTOR_TYPE_SAMPLER;
	case STORAGE_BUFFER: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	case STORAGE_IMAGE: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	case SAMPLED_IMAGE: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return VK_DESCRIPTOR_TYPE_MAX_ENUM;
}

inline constexpr vk_desc_binding_t VkDescTypeToBinding( VkDescriptorType type )
{
	using enum vk_desc_binding_t;
	switch( type )
	{
	case VK_DESCRIPTOR_TYPE_SAMPLER: return SAMPLER;
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: return STORAGE_BUFFER;
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE: return STORAGE_IMAGE;
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: return SAMPLED_IMAGE;
	default: HT_ASSERT( 0 && "Wrong descriptor type" ); 
	}
	return COUNT;
}

struct vk_descriptor_set
{
	VkDescriptorPool pool;
	VkDescriptorSetLayout setLayout;
	VkDescriptorSet set;
};
static vk_descriptor_set VkMakeDescriptorAllocator( VkDevice vkDevice, std::span<const VkDescriptorPoolSize> descPoolSizes ) 
{
	constexpr u32 maxSetCount = 1;

	VkDescriptorPool vkDescPool;
	VkDescriptorPoolCreateInfo descPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = maxSetCount,
		.poolSizeCount = ( u32 ) std::size( descPoolSizes ),
		.pPoolSizes = std::data( descPoolSizes )
	};
	VK_CHECK( vkCreateDescriptorPool( vkDevice, &descPoolInfo, 0, &vkDescPool ) );


	constexpr VkDescriptorBindingFlags flag =
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

	std::vector<VkDescriptorBindingFlags> bindingFlags( std::size( descPoolSizes ) );
	std::vector<VkDescriptorSetLayoutBinding> descSetLayout( std::size( descPoolSizes ) );
	for( u32 i = 0; i < vk_desc_binding_t::COUNT; ++i )
	{
		auto[ type, count ] = descPoolSizes[ i ];

		descSetLayout[ i ] = {
			.binding = i,
			.descriptorType = type,
			.descriptorCount = count,
			.stageFlags = VK_SHADER_STAGE_ALL 
		};
		bindingFlags[ i ] = flag;
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = ( u32 ) std::size( bindingFlags ),
		.pBindingFlags = std::data( bindingFlags )
	};
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descSetFalgs,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = ( u32 ) std::size( descSetLayout ),
		.pBindings = std::data( descSetLayout )
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

	return {
		.pool = vkDescPool,
		.setLayout = vkDescSetLayout,
		.set = vkDescSet
	};
}

static VkPipelineLayout VkMakeGlobalPipelineLayout( 
	VkDevice vkDevice, 
	VkDescriptorSetLayout descSetLayout,
	const VkPhysicalDeviceProperties& props
) {
	VkPushConstantRange pushConstRange = { 
		.stageFlags = VK_SHADER_STAGE_ALL, 
		.offset = 0, 
		.size = props.limits.maxPushConstantsSize 
	};
	VkPipelineLayoutCreateInfo pipeLayoutInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &descSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};

	VkPipelineLayout pipelineLayout = {};
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &pipelineLayout ) );

	return pipelineLayout;
}

struct vk_instance
{
	VkInstance hndl;
	VkDebugUtilsMessengerEXT dbgMsg;
};

static vk_instance VkMakeInstance()
{
	constexpr const char* ENABLED_INST_EXTS[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
	#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	#endif // VK_USE_PLATFORM_WIN32_KHR
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	};

	constexpr const char* LAYERS[] =
	{
		"VK_LAYER_KHRONOS_validation",
		//"VK_LAYER_LUNARG_api_dump"
	};

	VK_CHECK( volkInitialize() );

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
		HT_ASSERT( foundExt );
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
		HT_ASSERT( foundLayer );
	}


	VkInstance vkInstance = 0;
	VkDebugUtilsMessengerEXT vkDbgUtilsMsgExt = 0;

	VkApplicationInfo appInfo = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VK_CHECK( vkEnumerateInstanceVersion( &appInfo.apiVersion ) );
	HT_ASSERT( VK_API_VERSION_1_4 <= appInfo.apiVersion );

	VkInstanceCreateInfo instInfo = { 
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = std::size( LAYERS ),
		.ppEnabledLayerNames = LAYERS,
		.enabledExtensionCount = std::size( ENABLED_INST_EXTS ),
		.ppEnabledExtensionNames = ENABLED_INST_EXTS,
	};

	VkValidationFeaturesEXT vkValidationFeatures = { 
		.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
		.enabledValidationFeatureCount = std::size( enabledValidationFeats ),
		.pEnabledValidationFeatures = enabledValidationFeats,
	};

	VkDebugUtilsMessengerCreateInfoEXT vkDbgExt = { 
		.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
		.pNext = &vkValidationFeatures,
		.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | 
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT,
		.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
		.pfnUserCallback = VkDbgUtilsMsgCallback,
	};

	instInfo.pNext = &vkDbgExt;

	VK_CHECK( vkCreateInstance( &instInfo, 0, &vkInstance ) );

	volkLoadInstance( vkInstance );

	VK_CHECK( vkCreateDebugUtilsMessengerEXT( vkInstance, &vkDbgExt, 0, &vkDbgUtilsMsgExt ) );

	return { .hndl = vkInstance, .dbgMsg = vkDbgUtilsMsgExt };
}

#ifdef VK_USE_PLATFORM_WIN32_KHR
static VkSurfaceKHR VkMakeWinSurface( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
{
	VkWin32SurfaceCreateInfoKHR surfInfo = { 
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
		.hinstance = hInst,
		.hwnd = hWnd,
	};

	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;
}
#else
#error Must provide OS specific Surface
#endif // VK_USE_PLATFORM_WIN32_KHR

struct vk_device
{
	VkPhysicalDeviceProperties  gpuProps;
	VkPhysicalDevice			gpu;
	VkDevice					logical;
	u32                         gfxQueueFamIdx;
	u32                         computeQueueFamIdx;
	u32                         transferQueueFamIdx;
	u32							deviceMask;
	u32							waveSize;
};

static vk_device VkMakeDevice( VkInstance vkInst, VkSurfaceKHR vkSurf )
{
	constexpr const char* ENABLED_DEVICE_EXTS[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PRESENT_ID_EXTENSION_NAME,
		VK_KHR_PRESENT_WAIT_EXTENSION_NAME,

		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,

		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,

		VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
		VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	VkPhysicalDeviceHostImageCopyProperties hostImgCopyProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES };
	VkPhysicalDeviceSubgroupProperties waveProps = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &hostImgCopyProps };
	VkPhysicalDeviceVulkan14Properties gpuProps14 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_PROPERTIES, &waveProps };
	VkPhysicalDeviceVulkan13Properties gpuProps13 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES, &gpuProps14 };
	VkPhysicalDeviceVulkan12Properties gpuProps12 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, &gpuProps13 };
	VkPhysicalDeviceVulkan11Properties gpuProps11 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES, &gpuProps12 };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps11 };

	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeatures = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR };
	VkPhysicalDevicePresentIdFeaturesKHR presentIdFeatures = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, &presentWaitFeatures };
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicStateFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, &presentIdFeatures };
	VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT unusedAttachmentsFeature =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_FEATURES_EXT, &extDynamicStateFeatures };
	VkPhysicalDeviceVulkan14Features gpuFeatures14 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, &unusedAttachmentsFeature };
	VkPhysicalDeviceVulkan13Features gpuFeatures13 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &gpuFeatures14 };
	VkPhysicalDeviceVulkan12Features gpuFeatures12 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &gpuFeatures13 };
	VkPhysicalDeviceVulkan11Features gpuFeatures11 = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &gpuFeatures12 };
	VkPhysicalDeviceFeatures2 gpuFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &gpuFeatures11 };

	// TODO: check for more stuff ?
	VkPhysicalDevice gpu = 0;
	u32 deviceIdx = 0;
	for( ; deviceIdx < numDevices; ++deviceIdx )
	{
		vkGetPhysicalDeviceProperties2( availableDevices[ deviceIdx ], &gpuProps2 );
		if( gpuProps2.properties.apiVersion < VK_API_VERSION_1_4 ) continue;
		if( gpuProps2.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) continue;
		if( !gpuProps2.properties.limits.timestampComputeAndGraphics ) continue;

		vkGetPhysicalDeviceFeatures2( availableDevices[ deviceIdx ], &gpuFeatures );

		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ deviceIdx ], 0, &extsNum, 0 ) || !extsNum ) continue;
		std::vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ deviceIdx ], 0, &extsNum, std::data( availableExts ) ) ) continue;

		for( std::string_view requiredExt : ENABLED_DEVICE_EXTS )
		{
			bool foundExt = false;
			for( const VkExtensionProperties& availableExt : availableExts )
			{
				if( requiredExt == availableExt.extensionName )
				{
					foundExt = true;
					break;
				}
			}
			if( !foundExt )
			{
				//SysErrMsgBox( std::format( "ERR: Required ext {} not found !", requiredExt ).c_str() );
				goto NEXT_DEVICE;
			}
		}

		gpu = availableDevices[ deviceIdx ];

		break;

	NEXT_DEVICE:;
	}

	HT_ASSERT( gpu );

	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, 0 );
	HT_ASSERT( queueFamNum );
	std::vector<VkQueueFamilyProperties> queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, std::data( queueFamProps ) );

	constexpr float queuePriorities = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

	queueCreateInfos.push_back( {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = VkGetQueueFamilyIndex( queueFamProps, VK_QUEUE_GRAPHICS_BIT, VK_TRUE, gpu, vkSurf ),
		.queueCount = 1, 
		.pQueuePriorities = &queuePriorities
	} );
	queueCreateInfos.push_back( {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = VkGetQueueFamilyIndex( queueFamProps, VK_QUEUE_COMPUTE_BIT, VK_TRUE, gpu, vkSurf ),
		.queueCount = 1, 
		.pQueuePriorities = &queuePriorities
	} );
	queueCreateInfos.push_back( {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = VkGetQueueFamilyIndex( queueFamProps, VK_QUEUE_TRANSFER_BIT, VK_FALSE, gpu, vkSurf ),
		.queueCount = 1, 
		.pQueuePriorities = &queuePriorities
	} );

	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &gpuFeatures,
		.queueCreateInfoCount = ( u32 ) std::size( queueCreateInfos ),
		.pQueueCreateInfos = std::data( queueCreateInfos ),
		.enabledExtensionCount = ( u32 ) std::size( ENABLED_DEVICE_EXTS ),
		.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS,
	};

	VkDevice vkDevice;
	VK_CHECK( vkCreateDevice( gpu, &deviceInfo, 0, &vkDevice ) );

	volkLoadDevice( vkDevice );

	return {
		.gpuProps = gpuProps2.properties,
		.gpu = gpu,
		.logical = vkDevice,
		.gfxQueueFamIdx = queueCreateInfos[ 0 ].queueFamilyIndex,
		.computeQueueFamIdx = queueCreateInfos[ 1 ].queueFamilyIndex,
		.transferQueueFamIdx = queueCreateInfos[ 2 ].queueFamilyIndex,
		.waveSize = waveProps.subgroupSize
	};
}


struct vk_swapchain
{
	std::vector<vk_swapchain_image>     imgs;
	VkSwapchainKHR		                swapchain;
};


struct vk_surface_info
{
	VkFormat                         format;
	VkColorSpaceKHR                  colorSpace;
	VkSurfaceTransformFlagBitsKHR    currentTransform;
	VkCompositeAlphaFlagBitsKHR      surfaceAlphaCompositeFlags;
	VkExtent2D                       currentExtent;
};

static vk_surface_info VkCheckSwapchainRequirementsAgainstSurface(
	VkPhysicalDevice	vkPhysicalDevice,
	VkSurfaceKHR		vkSurf,
	VkFormat			scDesiredFormat,
	VkPresentModeKHR    desiredPresentMode,
	VkImageUsageFlags   scImgUsage,
	u32                 minNumIngs
) {
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkPhysicalDevice, vkSurf, &surfaceCaps ) );
	HT_ASSERT( surfaceCaps.maxImageArrayLayers >= 1 );

	HT_ASSERT( ( minNumIngs > surfaceCaps.minImageCount ) && ( minNumIngs < surfaceCaps.maxImageCount ) );
	HT_ASSERT( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) &&
		( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkCompositeAlphaFlagBitsKHR surfaceAlphaCompositeFlags =
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
		HT_ASSERT( scFormatAndColSpace.format );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, 0 ) );
		std::vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( 
			vkPhysicalDevice, vkSurf, &numPresentModes, std::data( presentModes ) ) );

		for( u32 j = 0; j < numPresentModes; ++j )
		{
			if( presentModes[ j ] == desiredPresentMode )
			{
				presentMode = desiredPresentMode;
				break;
			}
		}
		HT_ASSERT( presentMode );
	}

	HT_ASSERT( ( surfaceCaps.supportedUsageFlags & scImgUsage ) == scImgUsage );

	VkPhysicalDeviceImageFormatInfo2 imgFormatInfo = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
		.format = scFormatAndColSpace.format,
		.type = VK_IMAGE_TYPE_2D,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = scImgUsage,
	};

	VkImageFormatProperties2 imageFormatProperties = { .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2 };
	VK_CHECK( vkGetPhysicalDeviceImageFormatProperties2( vkPhysicalDevice, &imgFormatInfo, &imageFormatProperties ) );

	return { 
		.format						= scFormatAndColSpace.format, 
		.colorSpace					= scFormatAndColSpace.colorSpace, 
		.currentTransform			= surfaceCaps.currentTransform,
		.surfaceAlphaCompositeFlags = surfaceAlphaCompositeFlags,
		.currentExtent				= surfaceCaps.currentExtent
	};
}

static vk_virtual_frame VkCreateVirtualFrame( VkDevice vkDevice, u32 queueFamIdx )
{
	VkCommandPoolCreateInfo cmdPoolInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = queueFamIdx,
	};
	VkCommandPool cmdPool;
	VK_CHECK( vkCreateCommandPool( vkDevice, &cmdPoolInfo, 0, &cmdPool ) );

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cmdBuff;
	VK_CHECK( vkAllocateCommandBuffers( vkDevice, &cmdBuffAllocInfo, &cmdBuff ) );

	VkSemaphoreCreateInfo semaInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VkSemaphore canGetImgSema;
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &canGetImgSema ) );

	return {
		.cmdPool = cmdPool,
		.cmdBuff = cmdBuff,
		.canGetImgSema = canGetImgSema
	};
}

vk_context VkMakeContext( uintptr_t hInst, uintptr_t hWnd, const vk_renderer_config& cfg )
{
	auto[ vkInst, vkDbgMsg ] = VkMakeInstance();
	VkSurfaceKHR vkSurf = VkMakeWinSurface( vkInst, ( HINSTANCE ) hInst, ( HWND ) hWnd );
	vk_device vkDevice = VkMakeDevice( vkInst, vkSurf );

	vk_frame_vector frameVec;
	for( u64 fi = 0; fi < cfg.framesInFlightCount; fi++ )
	{
		frameVec.push_back( VkCreateVirtualFrame( vkDevice.logical, vkDevice.gfxQueueFamIdx ) );
	}

	const VkPhysicalDeviceLimits& vkGpuLimits = vkDevice.gpuProps.limits;
	std::array<VkDescriptorPoolSize, vk_desc_binding_t::COUNT> poolSizes = {
		VkDescriptorPoolSize { 
			.type = VkDescBindingToType( vk_desc_binding_t::SAMPLER ), 
			.descriptorCount = std::min( ( u32 ) cfg.MAX_DESCRIPTOR_COUNT_PER_TYPE, vkGpuLimits.maxDescriptorSetSamplers ) 
		},
		VkDescriptorPoolSize { 
			.type = VkDescBindingToType( vk_desc_binding_t::STORAGE_BUFFER ), 
			.descriptorCount = std::min( ( u32 ) cfg.MAX_DESCRIPTOR_COUNT_PER_TYPE, vkGpuLimits.maxDescriptorSetStorageBuffers ) 
		},
		VkDescriptorPoolSize { 
			.type = VkDescBindingToType( vk_desc_binding_t::STORAGE_IMAGE ), 
			.descriptorCount = std::min( ( u32 ) cfg.MAX_DESCRIPTOR_COUNT_PER_TYPE, vkGpuLimits.maxDescriptorSetStorageImages ) 
		},
		VkDescriptorPoolSize { 
			.type = VkDescBindingToType( vk_desc_binding_t::SAMPLED_IMAGE ), 
			.descriptorCount = std::min( ( u32 ) cfg.MAX_DESCRIPTOR_COUNT_PER_TYPE, vkGpuLimits.maxDescriptorSetSampledImages ) 
		}
	};

	auto[ descPool, descSetLayout, descSet ] = VkMakeDescriptorAllocator( vkDevice.logical, poolSizes );

	vk_desc_vector bindingSlotFreelist;
	for( auto[ _, descCount ] : poolSizes )
	{
		bindingSlotFreelist.emplace_back( ( u64 ) descCount );
	}

	return {
		.vrtFrames = std::move( frameVec ),
		.descBindingSlotFreelist = bindingSlotFreelist,
		.gfxQueue = VkCreateQueue( vkDevice.logical, vkDevice.gfxQueueFamIdx, VK_QUEUE_GRAPHICS_BIT, 0 ),
		.copyQueue = VkCreateQueue( vkDevice.logical, vkDevice.transferQueueFamIdx, VK_QUEUE_TRANSFER_BIT, 0 ),
		.allocator = MakeVmaAllocator( vkDevice.gpu, vkDevice.logical, vkInst, VK_API_VERSION_1_4 ),
		.descPool = descPool,
		.descSetLayout = descSetLayout,
		.descSet = descSet,
		.gpuProps = vkDevice.gpuProps,
		.gpu = vkDevice.gpu,
		.device = vkDevice.logical,
		.inst = vkInst,
		.dbgMsg = vkDbgMsg,
		.surf = vkSurf,
		.globalPipelineLayout = VkMakeGlobalPipelineLayout( vkDevice.logical, descSetLayout, vkDevice.gpuProps ),
		.deviceMask = vkDevice.deviceMask,
		.timestampPeriod = vkDevice.gpuProps.limits.timestampPeriod,
		.waveSize = vkDevice.waveSize,
		.scConfig = cfg.scConfig
	};
}


vk_buffer vk_context::CreateBuffer( const buffer_info& buffInfo )
{
	VkBufferCreateInfo bufferCreateInfo = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = buffInfo.sizeInBytes,
		.usage = buffInfo.usageFlags,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	VkMemoryPropertyFlags memPropFlags = VkChooseMemoryProperitesOnUsage( buffInfo.usage );

	VmaAllocationCreateFlags allocFlags =
		VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT | VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
	// TODO: do we do random access ?
	if( memPropFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
	{
		allocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}
	VmaAllocationCreateInfo allocCreateInfo = {
		.flags = allocFlags,
		.usage = VMA_MEMORY_USAGE_AUTO,
		//.requiredFlags = memPropFlags,
	};

	VkBuffer vkBuffer;
	VmaAllocation mem;
	VmaAllocationInfo allocInfo;
	VK_CHECK( vmaCreateBuffer( allocator, &bufferCreateInfo, &allocCreateInfo, &vkBuffer, &mem, &allocInfo ) );

	if( buffInfo.name )
	{
		VkDbgNameObj( vkBuffer, device, buffInfo.name );
	}

	VkDeviceAddress devicePointer = 0;
	if( bufferCreateInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT )
	{
		devicePointer = VkGetBufferDeviceAddress( device, vkBuffer );
		HT_ASSERT( devicePointer );
	}

	return {
		.mem = mem,
		.hndl = vkBuffer,
		.sizeInBytes = bufferCreateInfo.size,
		.hostVisible = ( u8* ) allocInfo.pMappedData,
		.devicePointer = devicePointer,
		.usgFlags = bufferCreateInfo.usage
	};
}

inline static void VkCheckFormatProperties( VkPhysicalDevice vkGpu, VkImageUsageFlags usg, VkFormat format )
{
	VkFormatFeatureFlags2 formatFeatures = 0;
	if( usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT )         formatFeatures |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
	if( usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT )             formatFeatures |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
	if( usg & VK_IMAGE_USAGE_SAMPLED_BIT )                  formatFeatures |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
	if( usg & VK_IMAGE_USAGE_HOST_TRANSFER_BIT )            formatFeatures |= VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT;


	VkFormatProperties3 formatProps3 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	VkFormatProperties2 fomratProps2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &formatProps3 };

	vkGetPhysicalDeviceFormatProperties2( vkGpu, format, &fomratProps2 );

	HT_ASSERT( ( formatProps3.optimalTilingFeatures & formatFeatures ) == formatFeatures );
	// Fallback to a different format or use other means of uploading data
}

vk_image vk_context::CreateImage( const image_info& imgInfo )
{
	VkCheckFormatProperties( gpu, imgInfo.usg, imgInfo.format );

	VkImageCreateInfo imageInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imgInfo.format,
		.extent = { imgInfo.width,  imgInfo.height, 1 },
		.mipLevels = imgInfo.mipCount,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = imgInfo.usg,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
	};

	VmaAllocationCreateInfo allocCreateInfo = { .usage = VMA_MEMORY_USAGE_AUTO };
	VkImage img;
	VmaAllocation mem;
	VmaAllocationInfo allocInfo;
	VK_CHECK( vmaCreateImage( allocator, &imageInfo, &allocCreateInfo, &img, &mem, &allocInfo ) );

	if( imgInfo.name )
	{
		VkDbgNameObj( img, device, imgInfo.name );
	}

	VkImageView vkImgView = VkMakeImgView(
		device, img, imageInfo.format, 0, imageInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imageInfo.arrayLayers );

	return {
		.mem = mem,
		.hndl = img,
		.view = vkImgView,
		.usageFlags = imageInfo.usage,
		.format = imageInfo.format,
		.width = ( u16 ) imageInfo.extent.width,
		.height = ( u16 ) imageInfo.extent.height,
		.layerCount = ( u8 ) imageInfo.arrayLayers,
		.mipCount = ( u8 ) imageInfo.mipLevels,
	};
}

VkPipeline vk_context::CreateGfxPipeline( 
	std::span<const vk_gfx_shader_stage> shaderStages, 
	std::span<const VkDynamicState> dynamicStates, 
	const VkFormat* pColorAttachmentFormats, 
	u32 colorAttachmentCount, 
	VkFormat depthAttachmentFormat, 
	const vk_gfx_pso_config& psoConfig,
	VkPipelineLayout vkPipelineLayout
) {
	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = psoConfig.primTopology,
	};

	VkPipelineViewportStateCreateInfo viewportInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ( u32 ) std::size( dynamicStates ),
		.pDynamicStates = std::data( dynamicStates ),
	};

	VkPipelineRasterizationStateCreateInfo rasterInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = 0,
		.rasterizerDiscardEnable = 0,
		.polygonMode = psoConfig.polyMode,
		.cullMode = psoConfig.cullFlags,
		.frontFace = psoConfig.frontFace,
		.lineWidth = 1.0f
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = psoConfig.depthTestEnable,
		.depthWriteEnable = psoConfig.depthWrite,
		.depthCompareOp = VK_COMPARE_OP_GREATER,
		.depthBoundsTestEnable = VK_TRUE,
		.minDepthBounds = 0,
		.maxDepthBounds = 1.0f
	};

	VkPipelineColorBlendAttachmentState blendConfig = {
		.blendEnable = psoConfig.blendCol,
		.srcColorBlendFactor = psoConfig.srcColorBlendFactor,
		.dstColorBlendFactor = psoConfig.dstColorBlendFactor,
		.colorBlendOp = psoConfig.colorBlendOp,
		.srcAlphaBlendFactor = psoConfig.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = psoConfig.dstAlphaBlendFactor,
		.alphaBlendOp = psoConfig.alphaBlendOp,
		.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendConfig,
	};

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineRenderingCreateInfo renderingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.colorAttachmentCount = colorAttachmentCount,
		.pColorAttachmentFormats = pColorAttachmentFormats,
		.depthAttachmentFormat = depthAttachmentFormat,
	};

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO 
	};
	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &renderingInfo,
		.stageCount = ( u32 ) std::size( shaderStages ),
		.pStages = ( const VkPipelineShaderStageCreateInfo* ) std::data( shaderStages ),
		.pVertexInputState = &vtxInCreateInfo,
		.pInputAssemblyState = &inAsmStateInfo,
		.pViewportState = &viewportInfo,
		.pRasterizationState = &rasterInfo,
		.pMultisampleState = &multisamplingInfo,
		.pDepthStencilState = &depthStencilState,
		.pColorBlendState = &colorBlendStateInfo,
		.pDynamicState = &dynamicStateInfo,
		.layout = vkPipelineLayout ? vkPipelineLayout : globalPipelineLayout,
		.basePipelineIndex = -1
	};

	VkPipeline vkGfxPipeline;
	VK_CHECK( vkCreateGraphicsPipelines( device, 0, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	return vkGfxPipeline;
}

VkPipeline vk_context::CreateComptuePipeline( const vk_shader& shader, const char* pName )
{
	VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroupSizeInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
		.requiredSubgroupSize = waveSize
	};

	VkPipelineShaderStageCreateInfo stage = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = &subgroupSizeInfo,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = shader.module,
		.pName = shader.entryPoint.c_str()
	};

	VkComputePipelineCreateInfo compPipelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = globalPipelineLayout
	};

	VkPipeline pipeline;
	VK_CHECK( vkCreateComputePipelines( device, 0, 1, &compPipelineInfo, 0, &pipeline ) );
	if( pName ) VkDbgNameObj( pipeline, device, pName );

	return pipeline;
}

unique_shader_ptr vk_context::CreateShaderFromSpirv( std::span<const u8> spvByteCode )
{
	spv_reflect::ShaderModule reflInfo = SpvMakeReflectedShaderModule( spvByteCode );
	const char* pEntryPointName = reflInfo.GetEntryPointName();
	VkShaderStageFlagBits shaderStage = ( VkShaderStageFlagBits ) reflInfo.GetShaderStage();

	VkShaderModuleCreateInfo shaderModuleInfo = { 
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = std::size( spvByteCode ),
		.pCode = ( const u32* ) std::data( spvByteCode ) 
	};

	VkShaderModule sm;
	VK_CHECK( vkCreateShaderModule( device, &shaderModuleInfo, 0, &sm ) );

	PFN_VkShaderDestoryer deleter = [ this ]( vk_shader* p )
	{
		if( !p ) return;
		this->DestroyShaderModule( p->module );
		delete p;
	};

	return { new vk_shader{ pEntryPointName, sm, shaderStage }, std::move( deleter ) };
}

desc_hndl32 vk_context::AllocDescriptor( const vk_descriptor_info& rscDescInfo )
{
	vk_desc_binding_t bindingSlot = VkDescTypeToBinding( rscDescInfo.descriptorType );
	HT_ASSERT( std::size( descBindingSlotFreelist ) > bindingSlot );

	vector_freelist& slotAlloc = descBindingSlotFreelist[ bindingSlot ];

	desc_hndl32 descIdx = { .slot = slotAlloc.push(), .type = bindingSlot };
	HT_ASSERT( INVALID_IDX != descIdx.slot );

	descPendingUpdates.push_back( { rscDescInfo, descIdx } );

	return descIdx;
}

void vk_context::FlushPendingDescriptorUpdates()
{
	if( !std::size( descPendingUpdates ) ) return;

	std::vector<VkWriteDescriptorSet> writes;
	for( const auto& update : descPendingUpdates )
	{
		const VkDescriptorImageInfo* pImageInfo = 0;
		const VkDescriptorBufferInfo* pBufferInfo = 0;

		if( update.descInfo.rscType == vk_resource_type::BUFFER )
		{
			pBufferInfo = &update.descInfo.buff;
		}
		else if( update.descInfo.rscType == vk_resource_type::IMAGE )
		{
			pImageInfo = &update.descInfo.img;
		}

		VkDescriptorType descType = update.descInfo.descriptorType;
		VkWriteDescriptorSet writeEntryInfo = {
			.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet = descSet,
			.dstBinding = VkDescTypeToBinding( descType ),
			.dstArrayElement = update.hndl.slot,
			.descriptorCount = 1,
			.descriptorType = descType,
			.pImageInfo = pImageInfo,
			.pBufferInfo = pBufferInfo
		};
		writes.push_back( writeEntryInfo );
	}

	vkUpdateDescriptorSets( device, ( u32 ) std::size( writes ), std::data( writes ), 0, 0 );
	descPendingUpdates.resize( 0 );
}

void vk_context::FlushDeletionQueues( u64 frameIdx )
{
	// NOTE: since it's a queue/ring buff, we always start at begin() 
	// and advance until there's an entry not deletable this frame
	// NOTE: eastl::ring_buffer pop decreases the size 
	while( std::size( resourceDeletionQueue ) != 0 )
	{
		vk_resc_deletion& rsc = resourceDeletionQueue.front();
		if( rsc.timelineCounterVal >= frameIdx ) break;
		if( vk_resource_type::BUFFER ==  rsc.type )
		{
			vmaDestroyBuffer( allocator, rsc.buff.hndl, rsc.buff.mem );
		}
		else if( vk_resource_type::IMAGE ==  rsc.type )
		{
			vkDestroyImageView( device, rsc.img.view, 0 );
			vmaDestroyImage( allocator, rsc.img.hndl, rsc.img.mem );
		}
		resourceDeletionQueue.pop_front();
	}
	while( std::size( descriptroDeletionQueue ) != 0 )
	{
		auto[ timelineCounterVal, hndl ] = descriptroDeletionQueue.front();
		if( timelineCounterVal >= frameIdx ) break;
		FreeDescriptor( hndl );
		descriptroDeletionQueue.pop_front();
	}
}

void vk_context::CreateSwapchin()
{
	// NOTE: rn we can't recreate the swapchian
	HT_ASSERT( !std::size( scImgs ) && ( VK_NULL_HANDLE == swapchain ) );

	u32 minNumImgs = scConfig.minNumImgs;
	VkFormat format = scConfig.format;
	VkPresentModeKHR presentMode = scConfig.presentMode;
	VkImageUsageFlags imgUsage = scConfig.imgUsage;

	vk_surface_info surfInfo = VkCheckSwapchainRequirementsAgainstSurface( 
		gpu, surf, format, presentMode, imgUsage, minNumImgs );

	HT_ASSERT( format == surfInfo.format );

	VkSwapchainCreateInfoKHR scInfo = { 
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.surface = surf,
		.minImageCount = minNumImgs,
		.imageFormat = surfInfo.format,
		.imageColorSpace = surfInfo.colorSpace,
		.imageExtent = surfInfo.currentExtent, // NOTE: will make "full screen"
		.imageArrayLayers = 1,
		.imageUsage = imgUsage,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.queueFamilyIndexCount = 1,
		.pQueueFamilyIndices = &gfxQueue.familyIdx,
		.preTransform = surfInfo.currentTransform,
		.compositeAlpha = surfInfo.surfaceAlphaCompositeFlags,
		.presentMode = presentMode,
		.clipped = VK_TRUE,
		.oldSwapchain = this->swapchain
	};

	VK_CHECK( vkCreateSwapchainKHR( device, &scInfo, 0, &this->swapchain ) );

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( device, this->swapchain, &scImgsNum, 0 ) ); 

	std::vector<VkImage> vkScImgs( scImgsNum );
	VK_CHECK( vkGetSwapchainImagesKHR( device, this->swapchain, &scImgsNum, std::data( vkScImgs ) ) );

	VkImageAspectFlags aspectFlags = VkSelectAspectMaskFromFormat( scInfo.imageFormat );

	scImgs.reserve( scImgsNum );
	scImgs.resize( 0 );

	std::array<char,64> imgNameStr;
	for( u64 scii = 0; scii < scImgsNum; ++scii )
	{
		VkImage img = vkScImgs[ scii ];

		imgNameStr = {};
		std::format_to_n( std::begin( imgNameStr ), std::size( imgNameStr ), "Img_Swapchain{}", scii );
		VkDbgNameObj( img, device, std::data( imgNameStr ) );

		VkImageView view = VkMakeImgView( device, img, scInfo.imageFormat, 0, 1, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );

		VkSemaphoreCreateInfo semaInfo = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkSemaphore canPresentSema;
		VK_CHECK( vkCreateSemaphore( device, &semaInfo, 0, &canPresentSema ) );

		scImgs.push_back( { 
			.canPresentSema = canPresentSema,
			.img = { 
				.hndl = img,
				.view = view,
				.usageFlags = scInfo.imageUsage,
				.format = scInfo.imageFormat,
				.width = scInfo.imageExtent.width,
				.height = scInfo.imageExtent.height,
			},
			.writeDescIdx = AllocDescriptor( vk_descriptor_info{ view, VK_IMAGE_LAYOUT_GENERAL } )
		} );
	}
}

void vk_context::QueueSubmit( 
	vk_queue& queue, 
	std::span<VkSemaphoreSubmitInfo> waits, 
	std::span<VkSemaphoreSubmitInfo> signals, 
	VkCommandBuffer cmdBuff 
) {
	scoped_stack memScope = { scratchArena };
	std::pmr::vector<VkSemaphoreSubmitInfo> vecSignals{ &memScope };
	vecSignals.insert( std::end( vecSignals ), std::cbegin( signals ), std::cend( signals ) );

	queue.submitionCount++;
	vecSignals.push_back( {
		.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = queue.timelineSema,
		.value     = queue.submitionCount,
		.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
		} );

	VkCommandBufferSubmitInfo cmdInfo = {
		.sType         = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = cmdBuff,
	};

	VkSubmitInfo2 submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.waitSemaphoreInfoCount = ( u32 ) std::size( waits ),
		.pWaitSemaphoreInfos = std::data( waits ),
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &cmdInfo,
		.signalSemaphoreInfoCount = ( u32 ) std::size( vecSignals ),
		.pSignalSemaphoreInfos = std::data( vecSignals ),
	};
	VK_CHECK( vkQueueSubmit2( queue.hndl, 1, &submitInfo, VK_NULL_HANDLE ) );
}
