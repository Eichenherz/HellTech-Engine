#pragma once

#include "r_vk_memory.hpp"

import handles;

struct vk_device
{
	vk_memory memory;

	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;

	VkQueue			compQueue;
	VkQueue			transfQueue;
	VkQueue			gfxQueue;
	u32				gfxQueueIdx;
	u32				compQueueIdx;
	u32				transfQueueIdx;

	float           timestampPeriod;
	u8				waveSize;
};

inline static vk_device VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf )
{
	constexpr const char* ENABLED_DEVICE_EXTS[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_PRESENT_ID_EXTENSION_NAME,
		VK_KHR_PRESENT_WAIT_EXTENSION_NAME,

		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,

		//VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,
		//VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME,

		//VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,
		VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
		VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
		VK_EXT_SHADER_OBJECT_EXTENSION_NAME
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	VkPhysicalDeviceShaderObjectPropertiesEXT shaderObjProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT };
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, &shaderObjProps };
	VkPhysicalDeviceSubgroupProperties waveProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &conservativeRasterProps };
	VkPhysicalDeviceVulkan13Properties gpuProps13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES, &waveProps };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps13 };

	VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT };
	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, &shaderObjFeatures };
	VkPhysicalDevicePresentIdFeaturesKHR presentIdFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR, &presentWaitFeatures };
	VkPhysicalDeviceIndexTypeUint8FeaturesEXT uint8IdxFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT, &presentWaitFeatures };
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicStateFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, &uint8IdxFeatures };
	VkPhysicalDeviceVulkan13Features gpuFeatures13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, &extDynamicStateFeatures };
	VkPhysicalDeviceVulkan12Features gpuFeatures12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &gpuFeatures13 };
	VkPhysicalDeviceVulkan11Features gpuFeatures11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &gpuFeatures12 };
	VkPhysicalDeviceFeatures2 gpuFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &gpuFeatures11 };

	VkPhysicalDevice vkGpu = 0;
	for( const VkPhysicalDevice currentVkGpu : availableDevices )
	{
		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( currentVkGpu, 0, &extsNum, 0 ) || !extsNum ) continue;
		std::vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( currentVkGpu, 0, &extsNum, std::data( availableExts ) ) ) continue;

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

		vkGetPhysicalDeviceProperties2( currentVkGpu, &gpuProps2 );
		if( gpuProps2.properties.apiVersion < VK_API_VERSION ) continue;
		if( gpuProps2.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) continue;
		if( !gpuProps2.properties.limits.timestampComputeAndGraphics ) continue;

		vkGetPhysicalDeviceFeatures2( currentVkGpu, &gpuFeatures );

		vkGpu = currentVkGpu;

		break;

	NEXT_DEVICE:;
	}
	VK_CHECK( VK_INTERNAL_ERROR( !vkGpu ) );

	gpuFeatures.features.geometryShader = 0;
	gpuFeatures.features.robustBufferAccess = 0;


	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( vkGpu, &queueFamNum, 0 );
	VK_CHECK( VK_INTERNAL_ERROR( !queueFamNum ) );
	std::vector<VkQueueFamilyProperties>  queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( vkGpu, &queueFamNum, std::data( queueFamProps ) );

	constexpr VkQueueFlags transferQueueType = VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT;
	constexpr VkQueueFlags presentQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;

	u32 qGfxIdx = -1, qCompIdx = -1, qTransfIdx = -1;
	for( u32 qIdx = 0; qIdx < queueFamNum; ++qIdx )
	{
		if( queueFamProps[ qIdx ].queueCount == 0 ) continue;

		VkQueueFlags familyFlags = queueFamProps[ qIdx ].queueFlags;
		if( familyFlags & presentQueueFlags )
		{
			VkBool32 present = 0;
			vkGetPhysicalDeviceSurfaceSupportKHR( vkGpu, qIdx, vkSurf, &present );
			VK_CHECK( VK_INTERNAL_ERROR( !present ) );
		}
		if( familyFlags & VK_QUEUE_GRAPHICS_BIT ) qGfxIdx = qIdx;
		else if( familyFlags & VK_QUEUE_COMPUTE_BIT ) qCompIdx = qIdx;
		else if( !( familyFlags ^ transferQueueType ) ) qTransfIdx = qIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( ( qGfxIdx == u32( -1 ) ) || ( qCompIdx == u32( -1 ) ) || ( qTransfIdx == u32( -1 ) ) ) );

	float queuePriorities = 1.0f;
	VkDeviceQueueCreateInfo queueInfos[ 3 ] = {};
	for( u32 qi = 0; qi < std::size( queueInfos ); ++qi )
	{
		queueInfos[ qi ] = {
			.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = qi,
			.queueCount = 1,
			.pQueuePriorities = &queuePriorities
		};
	}

	vk_device dc = {};

	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &gpuFeatures,
		.queueCreateInfoCount = std::size( queueInfos ),
		.pQueueCreateInfos = &queueInfos[ 0 ],
		.enabledExtensionCount = std::size( ENABLED_DEVICE_EXTS ),
		.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS
	};
	VK_CHECK( vkCreateDevice( vkGpu, &deviceInfo, 0, &dc.device ) );


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
	dc.gpu = vkGpu;
	dc.gpuProps = gpuProps2.properties;
	dc.timestampPeriod = gpuProps2.properties.limits.timestampPeriod;
	dc.waveSize = waveProps.subgroupSize;

	dc.memory = VkInitGfxMemory( dc.gpu, dc.device );

	return dc;
}

constexpr u32 HLTK_BUFFER_USE_HOST = 1u << 31;
// TODO: use custom usages that map to vk usages
inline vk_mem_arena& VkDeviceGetBufferMemArenaByUsage( vk_device& device, VkBufferUsageFlags flags )
{
	switch( flags )
	{
	case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT:
	case  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |VK_BUFFER_USAGE_TRANSFER_DST_BIT:
	case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT:
	case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT:
		return device.memory.arenas[ vk_arena_type::BUFFERS ];
	case VK_BUFFER_USAGE_TRANSFER_DST_BIT:
	case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | HLTK_BUFFER_USE_HOST:
		return device.memory.arenas[ vk_arena_type::HOST_VISIBLE ];
	
	}
}