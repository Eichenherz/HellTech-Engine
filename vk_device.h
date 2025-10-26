#ifndef __VK_DEVICE_H__
#define __VK_DEVICE_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include "vk_error.h"
#include "vk_memory.h"
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
	vk_mem_arena deviceLocalArena;
	vk_mem_arena hostVisibleArena;
	vk_mem_arena stagingArena;
	vk_mem_arena imgArena;

	vk_queue		gfxQueue;

	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;
	
	float           timestampPeriod;
	u8				waveSize;
};

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

	VkPhysicalDeviceMultiDrawPropertiesEXT multiDrawExt = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_PROPERTIES_EXT };
	VkPhysicalDeviceInlineUniformBlockPropertiesEXT inlineBlockProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_PROPERTIES_EXT, &multiDrawExt };
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
	VkPhysicalDeviceMultiDrawFeaturesEXT multiDrawFeasturesExt = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTI_DRAW_FEATURES_EXT, &presentIdFeatures };
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicStateFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, &multiDrawFeasturesExt };
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
		if( gpuProps2.properties.apiVersion < VK_API_VERSION_1_4 ) continue;
		if( gpuProps2.properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) continue;
		if( !gpuProps2.properties.limits.timestampComputeAndGraphics ) continue;

		vkGetPhysicalDeviceFeatures2( availableDevices[ i ], &gpuFeatures );

		gpu = availableDevices[ i ];

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
	vkDc.timestampPeriod = gpuProps2.properties.limits.timestampPeriod;
	vkDc.waveSize = waveProps.subgroupSize;

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );

	vkDc.deviceLocalArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	vkDc.imgArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	vkDc.stagingArena = VkMakeMemoryArena( 
		memProps, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT );
	vkDc.hostVisibleArena = VkMakeMemoryArena( 
		memProps,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

	return vkDc;
}

vk_mem_arena* VkChooseBufferArenaBasedOnUsage( vk_device_ctx* vkDc, buffer_usage usage )
{
	using enum buffer_usage;
	switch( usage )
	{
	case GPU_ONLY: return &vkDc->deviceLocalArena;
	case HOST_VISIBLE: return &vkDc->hostVisibleArena;
	case STAGING: return &vkDc->stagingArena;
	}
	return 0;
}

static vk_buffer VkCreateAllocBindBuffer( vk_device_ctx* vkDc, const buffer_info& buffInfo ) 
{
	VkBuffer vkBuffer;
	VkBufferCreateInfo bufferInfo = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = buffInfo.elemCount * buffInfo.stride,
		.usage = buffInfo.usageFlags,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VK_CHECK( vkCreateBuffer( vkDc->device, &bufferInfo, 0, &vkBuffer ) );

	if( buffInfo.name )
	{
		VkDbgNameObj( vkBuffer, vkDc->device, buffInfo.name );
	}

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,
		.buffer = vkBuffer,
	};
	vkGetBufferMemoryRequirements2( vkDc->device, &buffMemReqs2, &memReqs2 );

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkDc->gpu, &memProps );

	vk_mem_arena* vkArena = VkChooseBufferArenaBasedOnUsage( vkDc, buffInfo.usage );
	u32 memTypeIdx = VkFindMemTypeIdx( memProps, vkArena->memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena->memTypeIdx ) ) );

	VkMemoryAllocateFlags allocFlags =
		( bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { 
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.buffer = buffMemReqs2.buffer,
	};

	bool dedicatedAlloc = dedicatedReqs.requiresDedicatedAllocation;

	u64 size = memReqs2.memoryRequirements.size;
	u64 align = memReqs2.memoryRequirements.alignment;
	vk_allocation bufferMem = VkArenaAlignAlloc( 
		vkDc->device, vkArena, size, align, allocFlags, dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	VK_CHECK( vkBindBufferMemory( vkDc->device, vkBuffer, bufferMem.deviceMem, bufferMem.dataOffset ) );

	u8* hostVisible = ( bufferMem.hostVisible ) ? ( bufferMem.hostVisible + bufferMem.dataOffset ) : 0;

	u64 devicePointer = 0;
	if( allocFlags & VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
	{
		devicePointer = VkGetBufferDeviceAddress( vkDc->device, vkBuffer );
		assert( devicePointer );
	}

	return {
		.mem = bufferMem,
		.hndl = vkBuffer,
		.sizeInBytes = bufferInfo.size,
		.hostVisible = hostVisible,
		.devicePointer = devicePointer,
		.usgFlags = bufferInfo.usage
	};
}

static vk_image VkCreateAllocBindImage( vk_device_ctx* vkDc, const image_info& imgInfo ) 
{
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) 
		formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( vkDc->gpu, imgInfo.format, &formatProps );
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

	VkImage img;
	VK_CHECK( vkCreateImage( vkDc->device, &imageInfo, 0, &img ) );

	if( imgInfo.name )
	{
		VkDbgNameObj( img, vkDc->device, imgInfo.name );
	}

	VkImageMemoryRequirementsInfo2 imgReqs2 = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.image = img,
	};
	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkDc->device, &imgReqs2, &memReqs2 );


	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkDc->gpu, &memProps );

	u32 memTypeBits = memReqs2.memoryRequirements.memoryTypeBits;
	u32 memTypeIdx = VkFindMemTypeIdx( memProps, vkDc->imgArena.memTypeProperties, memTypeBits );
	u32 arenaMemTypeIdx = vkDc->imgArena.memTypeIdx;
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == arenaMemTypeIdx ) ) );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { 
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.image = imgReqs2.image,
	};
	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;
	const VkMemoryDedicatedAllocateInfo* pDedicatedAllocInfo = dedicatedAlloc ? &dedicatedAllocateInfo : 0;

	u64 size = memReqs2.memoryRequirements.size;
	u64 align = memReqs2.memoryRequirements.alignment;
	vk_allocation imgMem = VkArenaAlignAlloc( vkDc->device, &vkDc->imgArena, size, align, 0, pDedicatedAllocInfo );

	VK_CHECK( vkBindImageMemory( vkDc->device, img, imgMem.deviceMem, imgMem.dataOffset ) );

	return {
		.mem = imgMem,
		.hndl = img,
		.usageFlags = imageInfo.usage,
		.nativeFormat = imageInfo.format,
		.width = (u16)imageInfo.extent.width,
		.height = (u16)imageInfo.extent.height,
		.layerCount = (u8)imageInfo.arrayLayers,
		.mipCount = (u8)imageInfo.mipLevels,
	};
}

#endif // !__VK_DEVICE_H__
