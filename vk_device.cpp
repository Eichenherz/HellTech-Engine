#include "vk_common.hpp"

#ifdef VMA_IMPLEMENTATION
#error VMA_IMPL should only be defined here
#endif

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "vk_device.hpp"

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
	VK_EXT_SHADER_OBJECT_EXTENSION_NAME,
	//VK_EXT_HOST_IMAGE_COPY_EXTENSION_NAME
};

inline auto VkSelectPhysicalDevice( VkInstance vkInst )
{
	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	//VkPhysicalDeviceHostImageCopyPropertiesEXT hostImgCpyProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_PROPERTIES_EXT };
	VkPhysicalDeviceShaderObjectPropertiesEXT shaderObjProps = { 
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_PROPERTIES_EXT };
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, &shaderObjProps };
	VkPhysicalDeviceSubgroupProperties waveProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &conservativeRasterProps };
	VkPhysicalDeviceVulkan13Properties gpuProps13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES, &waveProps };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps13 };

	//VkPhysicalDeviceHostImageCopyFeaturesEXT hostImgCpyFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_IMAGE_COPY_FEATURES_EXT };
	VkPhysicalDeviceShaderObjectFeaturesEXT shaderObjFeatures = { 
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT };
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
			for( const VkExtensionProperties& availableExt : availableExts )
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

	struct
	{
		VkPhysicalDeviceProperties2 gpuProps;
		VkPhysicalDeviceFeatures2 gpuFeatures;
		VkPhysicalDevice vkGpu;
	} __retval = { gpuProps2, gpuFeatures, vkGpu };

	return __retval;
}

struct vk_queue_infos
{
	VkDeviceQueueCreateInfo data[ 3 ] = {};
	VkDeviceQueueCreateInfo& graphics = data[ 0 ];
	VkDeviceQueueCreateInfo& compute = data[ 1 ];
	VkDeviceQueueCreateInfo& transfer = data[ 2 ];
};

inline vk_queue_infos VkSelectDeviceQueues( VkPhysicalDevice vkGpu, VkSurfaceKHR vkSurf )
{
	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( vkGpu, &queueFamNum, 0 );
	VK_CHECK( VK_INTERNAL_ERROR( !queueFamNum ) );
	std::vector<VkQueueFamilyProperties>  queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( vkGpu, &queueFamNum, std::data( queueFamProps ) );

	constexpr VkQueueFlags transferQueueType = VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT;
	constexpr VkQueueFlags presentQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
	constexpr float queuePriorities = 1.0f;

	vk_queue_infos queueInfos;
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
		if( familyFlags & VK_QUEUE_GRAPHICS_BIT )
		{
			queueInfos.graphics = {
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = qIdx,
				.queueCount = 1,
				.pQueuePriorities = &queuePriorities
			};
		}
		else if( familyFlags & VK_QUEUE_COMPUTE_BIT )
		{
			queueInfos.compute = {
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = qIdx,
				.queueCount = 1,
				.pQueuePriorities = &queuePriorities
			};
		}
		else if( !( familyFlags ^ transferQueueType ) )
		{
			queueInfos.transfer = {
				.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
				.queueFamilyIndex = qIdx,
				.queueCount = 1,
				.pQueuePriorities = &queuePriorities
			};
		}
	}

	return queueInfos;
}

inline static vk_device VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf )
{
	auto[ gpuProps2, gpuFeatures, vkGpu ] = VkSelectPhysicalDevice( vkInst );
	vk_queue_infos queueInfos = VkSelectDeviceQueues( vkGpu, vkSurf );
	
	VkDevice vkDevice;
	VkDeviceCreateInfo deviceInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.pNext = &gpuFeatures,
		.queueCreateInfoCount = ( u32 ) std::size( queueInfos.data ),
		.pQueueCreateInfos = std::data( queueInfos.data ),
		.enabledExtensionCount = ( u32 ) std::size( ENABLED_DEVICE_EXTS ),
		.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS
	};
	VK_CHECK( vkCreateDevice( vkGpu, &deviceInfo, 0, &vkDevice ) );

	VkLoadDeviceProcs( vkDevice );

	VkQueue gfxQueue, compQueue, transfQueue;
	vkGetDeviceQueue( vkDevice, queueInfos.graphics.queueFamilyIndex, 0, &gfxQueue );
	vkGetDeviceQueue( vkDevice, queueInfos.compute.queueFamilyIndex, 0, &compQueue );
	vkGetDeviceQueue( vkDevice, queueInfos.transfer.queueFamilyIndex, 0, &transfQueue );
	VK_CHECK( VK_INTERNAL_ERROR( !gfxQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !compQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !transfQueue ) );

	VmaVulkanFunctions vulkanFunctions = {
		.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
		.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
		.vkAllocateMemory = vkAllocateMemory,
		.vkFreeMemory = vkFreeMemory,
		.vkMapMemory = vkMapMemory,
		.vkUnmapMemory = vkUnmapMemory,
		.vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
		.vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
		.vkBindBufferMemory = vkBindBufferMemory,
		.vkBindImageMemory = vkBindImageMemory,
		.vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
		.vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
		.vkCreateBuffer = vkCreateBuffer,
		.vkDestroyBuffer = vkDestroyBuffer,
		.vkCreateImage = vkCreateImage,
		.vkDestroyImage = vkDestroyImage,
		.vkCmdCopyBuffer = vkCmdCopyBuffer,
		.vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2KHR,
		.vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2KHR,
		.vkBindBufferMemory2KHR = vkBindBufferMemory2KHR,
		.vkBindImageMemory2KHR = vkBindImageMemory2KHR,
		.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2KHR,
		.vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
		.vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements
	};

	// TODO: which are default, which do we have ?
	VmaAllocatorCreateInfo allocatorCreateInfo = {
		.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT | VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT |
		VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = vkGpu,
		.device = vkDevice,
		.pVulkanFunctions = &vulkanFunctions,
		.instance = vkInst,
		.vulkanApiVersion = VK_API_VERSION,
	};

	VmaAllocator vmaAllocator;
	vmaCreateAllocator(&allocatorCreateInfo, &vmaAllocator);

	return {
		.allocator = vmaAllocator,
		.gpuProps = gpuProps2.properties,
		.gpu = vkGpu,
		.device = vkDevice,
		.compQueue = compQueue,
		.transfQueue = transfQueue,
		.gfxQueue = gfxQueue,
		.gfxQueueIdx = queueInfos.graphics.queueFamilyIndex,
		.compQueueIdx = queueInfos.compute.queueFamilyIndex,
		.transfQueueIdx = queueInfos.transfer.queueFamilyIndex
	};
}

// TODO: how to host comm ?
deleter_unique_ptr<vk_buffer> vk_device::CreateBuffer( const buffer_info& buffInfo ) 
{
	VkBufferCreateInfo bufferInfo = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
		//.flags = ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT : 0,
		.size = buffInfo.elemCount * buffInfo.stride,
		.usage = buffInfo.usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	VmaAllocationCreateFlags allocFlags = 0;
	if( bufferInfo.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT )
	{
		allocFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}
	VmaAllocationCreateInfo allocCreateInfo = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VkBuffer vkBuff;
	VmaAllocation allocation;
	VmaAllocationInfo allocInfo;
	vmaCreateBuffer( allocator, &bufferInfo, &allocCreateInfo, &vkBuff, &allocation, &allocInfo );

	vk_buffer buffData = {
		.hndl = vkBuff,
		.allocation = allocation,
		.size = bufferInfo.size,
		.hostVisible = ( u8* ) allocInfo.pMappedData,
		.usgFlags = bufferInfo.usage,
		.stride = buffInfo.stride
	};

	if( bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT )
	{
		buffData.devicePointer = VkGetBufferDeviceAddress( this->device, buffData.hndl );
	}

	if( buffInfo.name )
	{
		VkDbgNameObj( buffData.hndl, this->device, buffInfo.name );
	}
	
	return { new vk_buffer( buffData ), [ this ]( vk_buffer& buffer ) { this->DestroyBuffer( buffer ); } };
}

deleter_unique_ptr<vk_image> vk_device::CreateImage( const image_info& imgInfo ) 
{
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( this->gpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );

	VkImageCreateInfo imageInfo = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = imgInfo.format,
		.extent = { imgInfo.width, imgInfo.height, 1 },
		.mipLevels = imgInfo.mipCount,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = imgInfo.usg,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	// TODO: needed ?
	assert( imageInfo.extent.width <= u16( ~0u ) );
	assert( imageInfo.extent.height <= u16( ~0u ) );
	assert( imageInfo.arrayLayers <= u8( ~0u ) );
	assert( imageInfo.mipLevels <= u8( ~0u ) );
	
	VmaAllocationCreateInfo allocCreateInfo = {
		.usage = VMA_MEMORY_USAGE_AUTO,
	};
	VkImage vkImg;
	VmaAllocation allocation;
	VmaAllocationInfo allocInfo;
	vmaCreateImage( this->allocator, &imageInfo, &allocCreateInfo, &vkImg, &allocation, &allocInfo );

	VkImageAspectFlags aspectMask =
		( imageInfo.format == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	VkImageView vkImgView = VkMakeImgView(
		this->device, vkImg, imageInfo.format, 0, imageInfo.mipLevels, aspectMask, VK_IMAGE_VIEW_TYPE_2D, 0, imageInfo.arrayLayers );
	vk_image img = {
		.hndl = vkImg,
		.view = vkImgView,
		.allocation = allocation,
		.usageFlags = imageInfo.usage,
		.nativeFormat = imageInfo.format,
		.width = ( u16 ) imageInfo.extent.width,
		.height = ( u16 ) imageInfo.extent.height,
		.layerCount = ( u8 ) imageInfo.arrayLayers,
		.mipCount = ( u8 ) imageInfo.mipLevels
	};

	for( u32 i = 0; i < img.mipCount; ++i )
	{
		VkImageAspectFlags mipAspectMask =
			( imageInfo.format == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		img.optionalViews[ i ] = VkMakeImgView( this->device, img.hndl, img.nativeFormat, i, 1, mipAspectMask );
	}

	if( imgInfo.name )
	{
		VkDbgNameObj( img.hndl, this->device, imgInfo.name );
	}

	return { new vk_image( img ), [ this ]( vk_image& image ) { this->DestroyImage( image ); } };
}

void vk_device::DestroyBuffer( vk_buffer& buffer )
{
	vmaDestroyBuffer( this->allocator, buffer.hndl, buffer.allocation );
}

void vk_device::DestroyImage( vk_image& image )
{
	vmaDestroyImage( this->allocator, image.hndl, image.allocation );
}

vk_descriptor_manager vk_device::CreateDescriptorManagerBindless()
{
	constexpr u32 maxSize = u16( ~0u );
	constexpr u32 maxSetCount = 1;

	const VkPhysicalDeviceLimits& gpuLimits = this->gpuProps.limits;
	u32 descCount[ std::size( bindingToTypeMap ) ] = {
		std::min( 8u, gpuProps.limits.maxDescriptorSetSamplers ),
		std::min( maxSize, gpuLimits.maxDescriptorSetStorageBuffers ),
		std::min( maxSize, gpuLimits.maxDescriptorSetStorageImages ),
		std::min( maxSize, gpuLimits.maxDescriptorSetSampledImages )
	};

	vk_descriptor_manager mngr = {};
	for( u32 i = 0; i < std::size( mngr.table ); ++i )
	{
		mngr.table[ i ] = { .slotsCount = (u16)maxSize, .usedSlots = 0 };
	}

	mngr.pool = this->CreateDescriptorPool( descCount, maxSetCount );
	mngr.setLayout = this->CreateDescriptorSetLayout( descCount );
	mngr.set = this->CreateDescriptorSet( mngr.pool, mngr.setLayout );
	mngr.pipelineLayout = this->CreatePipelineLayout( mngr.setLayout );

	return mngr;
}

inline void vk_device::FlushDescriptorUpdates( std::span<VkWriteDescriptorSet> descriptorUpdates )
{
	if( std::size( descriptorUpdates ) == 0 )
	{
		return;
	}

	vkUpdateDescriptorSets( this->device, std::size( descriptorUpdates ), std::data( descriptorUpdates ), 0, 0 );
	//dealer.pendingUpdates.resize( 0 );
}

VkDescriptorPool vk_device::CreateDescriptorPool( std::span<u32> descriptorCount, u32 maxSetCount )
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	poolSizes.resize( std::size( descriptorCount ) );
	for( u32 i = 0; i < std::size( descriptorCount ); ++i )
	{
		poolSizes[ i ] = { .type = bindingToTypeMap[ i ], .descriptorCount = descriptorCount[ i ] };
	}

	VkDescriptorPoolCreateInfo descPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = maxSetCount,
		.poolSizeCount = ( u32 ) std::size( poolSizes ),
		.pPoolSizes = std::data( poolSizes )
	};

	VkDescriptorPool pool;
	VK_CHECK( vkCreateDescriptorPool( this->device, &descPoolInfo, 0, &pool ) );

	return pool;
}
VkDescriptorSetLayout vk_device::CreateDescriptorSetLayout( std::span<u32> descriptorCount )
{
	constexpr VkDescriptorBindingFlags flag =
		VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

	VkDescriptorBindingFlags bindingFlags[ std::size( bindingToTypeMap ) ] = {};
	VkDescriptorSetLayoutBinding descSetLayout[ std::size( bindingToTypeMap ) ] = {};
	for( u32 i = 0; i < std::size( bindingToTypeMap ); ++i )
	{
		descSetLayout[ i ] = {
			.binding = i,
			.descriptorType = bindingToTypeMap[ i ],
			.descriptorCount = descriptorCount[ i ],
			.stageFlags = VK_SHADER_STAGE_ALL
		};
		bindingFlags[ i ] = flag;
	}

	// NOTE: only the last descriptor entry is allowed to be variable in size
	bindingFlags[ std::size( bindingToTypeMap ) ] |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = ( u32 ) std::size( bindingFlags ),
		.pBindingFlags = bindingFlags
	};
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &descSetFalgs,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = ( u32 ) std::size( descSetLayout ),
		.pBindings = descSetLayout
	};

	VkDescriptorSetLayout setLayout;
	VK_CHECK( vkCreateDescriptorSetLayout( this->device, &descSetLayoutInfo, 0, &setLayout ) );

	return setLayout;
}
VkDescriptorSet vk_device::CreateDescriptorSet( VkDescriptorPool pool, VkDescriptorSetLayout setLayout )
{
	VkDescriptorSetAllocateInfo descSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &setLayout
	};

	VkDescriptorSet set = {};
	VK_CHECK( vkAllocateDescriptorSets( this->device, &descSetInfo, &set ) );

	return set;
}

VkPipelineLayout vk_device::CreatePipelineLayout( VkDescriptorSetLayout setLayout )
{
	VkPushConstantRange pushConstRange = { VK_SHADER_STAGE_ALL, 0, gpuProps.limits.maxPushConstantsSize };
	VkPipelineLayoutCreateInfo pipeLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &setLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstRange
	};

	VkPipelineLayout layout;
	VK_CHECK( vkCreatePipelineLayout( this->device, &pipeLayoutInfo, 0, &layout ) );
	VkDbgNameObj( layout, this->device, "Vk_Pipeline_Layout_Global" );

	return layout;
}

void vk_device::QueueSubmit( const std::span<VkQueueSubmit2> submits )
{
	// NOTE: queue submit has implicit host sync for trivial stuff
	VK_CHECK( vkQueueSubmit2( this->gfxQueue, 1, &submitInfo, 0 ) );
}
