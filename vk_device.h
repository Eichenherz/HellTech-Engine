#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include <3rdParty/vk_mem_alloc.h>

#include "vk_error.h"
#include "vk_resources.h"
#include "core_types.h"
#include "sys_os_api.h"
#include <type_traits>

struct vk_queue
{
	VkQueue	hndl;
	u32	index;
	VkQueueFlags familyFlags;
	bool canPresent;
};

void VkMakeCreateQueueInfoWithProperties( 
	std::span<VkQueueFamilyProperties> queueFamProps, 
	VkQueueFlags desiredProps, 
	bool canPresent,
	VkPhysicalDevice gpu,
	VkSurfaceKHR vkSurf,
	const float* pQueuePriorities,
	VkDeviceQueueCreateInfo& outCreateInfo,
	VkQueueFlags& outQueueFlags
) {
	u32 desiredQueueIdx = -1;
	VkQueueFlags queueFlags;

	for( u32 qIdx = 0; qIdx < std::size( queueFamProps ); ++qIdx )
	{
		if( queueFamProps[ qIdx ].queueCount == 0 ) continue;

		VkQueueFlags familyFlags = queueFamProps[ qIdx ].queueFlags; 
		if( ( familyFlags & desiredProps ) == desiredProps )
		{
			if( canPresent )
			{
				VkBool32 present = 0;
				vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qIdx, vkSurf, &present );
				VK_CHECK( VK_INTERNAL_ERROR( !present ) );
			}
			desiredQueueIdx = qIdx;
			queueFlags = familyFlags;
			break;
		}
	}

	VK_CHECK( VK_INTERNAL_ERROR( ( desiredQueueIdx == u32( -1 ) ) ) );
	VkDeviceQueueCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = desiredQueueIdx,
		.queueCount = 1,
		.pQueuePriorities = pQueuePriorities
	};

	outCreateInfo = createInfo;
	outQueueFlags = queueFlags;
}

vk_queue VkCreateQueue( VkDevice vkDevice, u32 queueFamilyIndex, VkQueueFlags desiredProps, bool canPresent )
{
	VkQueue	hndl;
	vkGetDeviceQueue( vkDevice, queueFamilyIndex, 0, &hndl );
	VK_CHECK( VK_INTERNAL_ERROR( !hndl ) );

	return {
		.hndl = hndl,
		.index = queueFamilyIndex,
		.familyFlags = desiredProps,
		.canPresent = canPresent
	};
}

void VkQueueSubmit( 
	vk_queue* vkQueue, 
	std::span<VkSemaphore> waitSemas,
	std::span<VkCommandBuffer> cmdBuffs,
	std::span<VkSemaphore> signalSemas,
	std::span<u64> signalValues,
	VkPipelineStageFlags waitDstStageMsk 
) {
	VkTimelineSemaphoreSubmitInfo timelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.signalSemaphoreValueCount = (u32) std::size( signalValues ),
		.pSignalSemaphoreValues = std::data( signalValues ),
	};

	VkSubmitInfo submitInfo = { 
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = &timelineInfo,
		.waitSemaphoreCount = (u32) std::size( waitSemas ),
		.pWaitSemaphores = std::data( waitSemas ),
		.pWaitDstStageMask = &waitDstStageMsk,
		.commandBufferCount = (u32) std::size( cmdBuffs ),
		.pCommandBuffers = std::data( cmdBuffs ),
		.signalSemaphoreCount = (u32) std::size( signalSemas ),
		.pSignalSemaphores = std::data( signalSemas ),
	};
	// NOTE: queue submit has implicit host sync for trivial stuff so in theroy we shouldn't worry about memcpy
	VK_CHECK( vkQueueSubmit( vkQueue->hndl, 1, &submitInfo, 0 ) );
}

struct vk_device_ctx
{
	VmaAllocator allocator;

	vk_queue		gfxQueue;

	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;
	
	u32             deviceMask;
	float           timestampPeriod;
	u8				waveSize;

	vk_buffer CreateBuffer( const buffer_info& buffInfo );
	vk_image CreateImage( const image_info& imgInfo );
};

inline VkMemoryPropertyFlags VkChooseMemoryProperitesOnUsage( buffer_usage usage )
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

vk_buffer vk_device_ctx::CreateBuffer( const buffer_info& buffInfo )
{
	VkBufferCreateInfo bufferCreateInfo = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = buffInfo.elemCount * buffInfo.stride,
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
	VK_CHECK( vmaCreateBuffer( this->allocator, &bufferCreateInfo, &allocCreateInfo, &vkBuffer, &mem, &allocInfo ) );

	if( buffInfo.name )
	{
		VkDbgNameObj( vkBuffer, this->device, buffInfo.name );
	}

	u64 devicePointer = 0;
	if( bufferCreateInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT )
	{
		devicePointer = VkGetBufferDeviceAddress( this->device, vkBuffer );
		assert( devicePointer );
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
vk_image vk_device_ctx::CreateImage( const image_info& imgInfo )
{
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) 
		formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( this->gpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );

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

	VmaAllocationCreateInfo allocCreateInfo = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	VkImage img;
	VmaAllocation mem;
	VmaAllocationInfo allocInfo;
	VK_CHECK( vmaCreateImage( this->allocator, &imageInfo, &allocCreateInfo, &img, &mem, &allocInfo ) );

	if( imgInfo.name )
	{
		VkDbgNameObj( img, this->device, imgInfo.name );
	}

	//VkImageView vkImgView = VkMakeImgView(
	//	this->device, img, imageInfo.format, 0, imageInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imageInfo.arrayLayers );

	return {
		.mem = mem,
		.hndl = img,
		.usageFlags = imageInfo.usage,
		.format = imageInfo.format,
		.width = (u16)imageInfo.extent.width,
		.height = (u16)imageInfo.extent.height,
		.layerCount = (u8)imageInfo.arrayLayers,
		.mipCount = (u8)imageInfo.mipLevels,
	};
}
inline VmaAllocator MakeVmaAllocator( VkPhysicalDevice vkGpu, VkDevice vkDevice, VkInstance vkInst, u32 vkVersion )
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

inline static vk_device_ctx VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf )
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
		VK_EXT_DYNAMIC_RENDERING_UNUSED_ATTACHMENTS_EXTENSION_NAME,
		//VK_EXT_MUTABLE_DESCRIPTOR_TYPE_EXTENSION_NAME,
		//VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,

		VK_EXT_MULTI_DRAW_EXTENSION_NAME
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	VkPhysicalDeviceInlineUniformBlockPropertiesEXT inlineBlockProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT };
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, &inlineBlockProps };
	VkPhysicalDeviceSubgroupProperties waveProps = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &conservativeRasterProps };
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
		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ deviceIdx ], 0, &extsNum, 0 ) || !extsNum ) continue;
		std::vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ deviceIdx ], 0, &extsNum, std::data( availableExts ) ) ) continue;

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

		vkGetPhysicalDeviceProperties2( availableDevices[ deviceIdx ], &gpuProps2 );
		if( gpuProps2.properties.apiVersion < VK_API_VERSION_1_4 ) continue;
		if( gpuProps2.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) continue;
		if( !gpuProps2.properties.limits.timestampComputeAndGraphics ) continue;

		vkGetPhysicalDeviceFeatures2( availableDevices[ deviceIdx ], &gpuFeatures );

		gpu = availableDevices[ deviceIdx ];

		break;

	NEXT_DEVICE:;
	}

	VK_CHECK( VK_INTERNAL_ERROR( !gpu ) );

	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, 0 );
	VK_CHECK( VK_INTERNAL_ERROR( !queueFamNum ) );
	std::vector<VkQueueFamilyProperties> queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, std::data( queueFamProps ) );

	constexpr VkQueueFlags careAboutQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

	float queuePriorities = 1.0f;
	VkDeviceQueueCreateInfo queueCreateInfos[1] = {};
	VkQueueFlags queueFlags;
	VkMakeCreateQueueInfoWithProperties(
		queueFamProps, careAboutQueueTypes, true, gpu, vkSurf, &queuePriorities, queueCreateInfos[ 0 ], queueFlags );

	VkDevice vkDevice;
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &gpuFeatures,
		.queueCreateInfoCount = std::size( queueCreateInfos ),
		.pQueueCreateInfos = &queueCreateInfos[ 0 ],
		.enabledExtensionCount = std::size( ENABLED_DEVICE_EXTS ),
		.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS,
	};
	VK_CHECK( vkCreateDevice( gpu, &deviceInfo, 0, &vkDevice ) );

	vk_device_ctx vkDc = {};

	VkLoadDeviceProcs( vkDevice );

	vkDc.gfxQueue = VkCreateQueue( vkDevice, queueCreateInfos[0].queueFamilyIndex, queueFlags, true );

	vkDc.device = vkDevice;
	vkDc.gpu = gpu;
	vkDc.gpuProps = gpuProps2.properties;
	vkDc.deviceMask = u32( 1u << deviceIdx );
	vkDc.timestampPeriod = gpuProps2.properties.limits.timestampPeriod;
	vkDc.waveSize = waveProps.subgroupSize;

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );

	vkDc.allocator = MakeVmaAllocator( gpu, vkDevice, vkInst, VK_API_VERSION_1_4 ); // TODO: main file with this );

	return vkDc;
}

#endif // !__VK_DEVICE_H__
