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
	VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
	//VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME,

	//VK_EXT_LOAD_STORE_OP_NONE_EXTENSION_NAME,
	VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
	VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
	VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
	VK_EXT_GRAPHICS_PIPELINE_LIBRARY_EXTENSION_NAME
};

inline auto VkSelectPhysicalDevice( VkInstance vkInst )
{
	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	VkPhysicalDeviceGraphicsPipelineLibraryPropertiesEXT gfxPipeLibProps = { 
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_PROPERTIES_EXT };
	VkPhysicalDeviceConservativeRasterizationPropertiesEXT conservativeRasterProps =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONSERVATIVE_RASTERIZATION_PROPERTIES_EXT, &gfxPipeLibProps };
	VkPhysicalDeviceSubgroupProperties waveProps = { 
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES, &conservativeRasterProps };
	VkPhysicalDeviceVulkan13Properties gpuProps13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES, &waveProps };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps13 };

	VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT gfxPipeLibFeatures = { 
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GRAPHICS_PIPELINE_LIBRARY_FEATURES_EXT };
	VkPhysicalDevicePresentWaitFeaturesKHR presentWaitFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR, &gfxPipeLibFeatures };
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

template<vk_queue_type QUEUE_TYPE>
vk_queue<QUEUE_TYPE> VkCreateQueue( VkDevice device, u32 queueFamilyIndex )
{
	VkQueue q;
	vkGetDeviceQueue( device, queueFamilyIndex, 0, &q );
	VK_CHECK( VK_INTERNAL_ERROR( !q ) );

	VkCommandPool cmdPool;
	VkCommandPoolCreateInfo cmdPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		.queueFamilyIndex = queueFamilyIndex
	};
	VK_CHECK( vkCreateCommandPool( device, &cmdPoolInfo, 0, &cmdPool ) );
	return {
		.hndl = q,
		.cmdPool = cmdPool,
		.index = queueFamilyIndex
	};
}

VmaAllocator VmaCreateAllocator( VkInstance vkInst, VkPhysicalDevice vkGpu, VkDevice vkDevice )
{
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

	return vmaAllocator;
}

inline static vk_device VkMakeDeviceContext( 
	VkInstance vkInst, 
	VkSurfaceKHR vkSurf, 
	VkFormat scDesiredFormat, 
	vk_queue_type presentQueueType 
) {
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

	vk_queue gfxQueue = VkCreateQueue<vk_queue_type::GRAPHICS>( vkDevice, queueInfos.graphics.queueFamilyIndex );
	vk_queue compQueue = VkCreateQueue<vk_queue_type::COMPUTE>( vkDevice, queueInfos.compute.queueFamilyIndex );
	vk_queue transfQueue = VkCreateQueue<vk_queue_type::TRANSFER>( vkDevice, queueInfos.transfer.queueFamilyIndex );

	VmaAllocator vmaAllocator = VmaCreateAllocator( vkInst, vkGpu, vkDevice );
	
	vk_device device = {
		.graphicsQueue = gfxQueue,
		.computeQueue = compQueue,
		.transferQueue = transfQueue,
		.allocator = vmaAllocator,
		.gpuProps = gpuProps2.properties,
		.gpu = vkGpu,
		.device = vkDevice,
	};

	device.descriptorManager = device.CreateDescriptorManagerBindless();
	device.swapchain = device.CreateSwapchain( vkSurf, scDesiredFormat, presentQueueType );

	return device;
}

// TODO: how to host comm ?
deleter_unique_ptr<vk_buffer> vk_device::CreateBuffer( const buffer_info& buffInfo ) const
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

void vk_device::DestroyBuffer( vk_buffer& buffer ) const
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

inline void vk_device::FlushDescriptorUpdates()
{
	u64 descriptorUpdates = std::size( this->descriptorManager.pendingUpdates );
	if( descriptorUpdates == 0 )
	{
		return;
	}

	vkUpdateDescriptorSets( this->device, descriptorUpdates, std::data( this->descriptorManager.pendingUpdates ), 0, 0 );
	this->descriptorManager.pendingUpdates.resize( 0 );
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

vk_swapchain vk_device::CreateSwapchain( VkSurfaceKHR vkSurf, VkFormat scDesiredFormat, vk_queue_type presentQueueType )
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( this->gpu, vkSurf, &surfaceCaps ) );

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
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( this->gpu, vkSurf, &formatCount, 0 ) );
		std::vector<VkSurfaceFormatKHR> formats( formatCount );
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( this->gpu, vkSurf, &formatCount, std::data( formats ) ) );

		for( const VkSurfaceFormatKHR& surfFormat : formats )
		{
			if( surfFormat.format == scDesiredFormat )
			{
				scFormatAndColSpace = surfFormat;
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( !scFormatAndColSpace.format ) );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( this->gpu, vkSurf, &numPresentModes, 0 ) );
		std::vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( this->gpu, vkSurf, &numPresentModes, std::data( presentModes ) ) );

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


	u32 scImgCount = VK_SWAPCHAIN_MAX_IMG_ALLOWED;
	assert( ( scImgCount > surfaceCaps.minImageCount ) && ( scImgCount < surfaceCaps.maxImageCount ) );
	assert( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) &&
			( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkImageUsageFlags scImgUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
	VK_CHECK( VK_INTERNAL_ERROR( ( surfaceCaps.supportedUsageFlags & scImgUsage ) != scImgUsage ) );

	assert( surfaceCaps.maxImageArrayLayers >= 1 );

	u32 queueFamIdx = -1;
	if( presentQueueType == vk_queue_type::GRAPHICS )
	{
		queueFamIdx = this->graphicsQueue.index;
	}
	else if( presentQueueType == vk_queue_type::COMPUTE )
	{
		queueFamIdx = this->computeQueue.index;
	}
	else
	{
		assert( false && "Wrong queue type" );
	}

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
		.oldSwapchain = 0,
	};

	VkImageFormatProperties scImageProps = {};
	VK_CHECK( vkGetPhysicalDeviceImageFormatProperties( this->gpu,
														scInfo.imageFormat,
														VK_IMAGE_TYPE_2D,
														VK_IMAGE_TILING_OPTIMAL,
														scInfo.imageUsage,
														scInfo.flags,
														&scImageProps ) );


	vk_swapchain sc = {};
	VK_CHECK( vkCreateSwapchainKHR( this->device, &scInfo, 0, &sc.hndl ) );

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( this->device, sc.hndl, &scImgsNum, 0 ) );
	VK_CHECK( VK_INTERNAL_ERROR( !( scImgsNum == scInfo.minImageCount ) ) );
	VK_CHECK( vkGetSwapchainImagesKHR( this->device, sc.hndl, &scImgsNum, sc.imgs ) );

	for( u64 i = 0; i < scImgsNum; ++i )
	{
		sc.imgViews[ i ] = VkMakeImgView( 
			this->device, sc.imgs[ i ], scInfo.imageFormat, 0, 1, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );
	}

	sc.width = scInfo.imageExtent.width;
	sc.height = scInfo.imageExtent.height;
	sc.imgCount = scInfo.minImageCount;
	sc.imgFormat = scInfo.imageFormat;

	return sc;
}

u32 vk_device::AcquireNextSwapcahinImage( VkSemaphore acquireScImgSema, u64 timeout = UINT64_MAX )
{
	u32 imgIdx;
	VK_CHECK( vkAcquireNextImageKHR( this->device, this->swapchain.hndl, timeout, acquireScImgSema, 0, &imgIdx ) );
	return imgIdx;
}

VkSemaphore vk_device::CreateVkSemaphore( bool isTimeline ) const
{
	constexpr VkSemaphoreTypeCreateInfo timelineInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = 0
	};
	VkSemaphoreCreateInfo semaInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = isTimeline ? &timelineInfo : 0 };

	VkSemaphore sema;
	VK_CHECK( vkCreateSemaphore( this->device, &semaInfo, 0, &sema ) );
	return sema;
}

void vk_device::WaitSemaphores( std::initializer_list<VkSemaphore> semas, std::initializer_list<u64> values, u64 maxWait )
{
	if( std::size( semas ) != std::size( values ) )
	{
		assert( false && "Semas must equal values" );
	}
	VkSemaphoreWaitInfo waitInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
		.semaphoreCount = ( u32 ) std::size( semas ),
		.pSemaphores = std::data( semas ),
		.pValues = std::data( values )
	};
	VkResult waitResult = vkWaitSemaphores( this->device, &waitInfo, maxWait );
	VK_CHECK( VK_INTERNAL_ERROR( waitResult > VK_TIMEOUT ) );
}

// TODO: where to move this?
#define HTVK_NO_SAMPLER_REDUCTION VK_SAMPLER_REDUCTION_MODE_MAX_ENUM
VkSampler vk_device::CreateSampler(
	VkSamplerReductionMode	reductionMode = HTVK_NO_SAMPLER_REDUCTION,
	VkFilter				filter = VK_FILTER_LINEAR,
	VkSamplerAddressMode	addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	VkSamplerMipmapMode		mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST
) {
	VkSamplerReductionModeCreateInfo reduxInfo = { 
		.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO, .reductionMode = reductionMode };

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
	VK_CHECK( vkCreateSampler( this->device, &samplerInfo, 0, &sampler ) );
	return sampler;
}


VkPipeline vk_device::CreateVertexInputInterfacePipelineStage( VkPrimitiveTopology primitiveTopology, const char* name )
{
	VkGraphicsPipelineLibraryCreateInfoEXT pipeLibExtInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
		.flags = VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT
	};

	// NOTE: never used
	VkPipelineVertexInputStateCreateInfo vtxInputStateCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo inputAsmStateCreateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = primitiveTopology,
	};

	VkGraphicsPipelineCreateInfo pipelineLibCreateInfo = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext               = &pipeLibExtInfo,
		.flags               = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
		.pVertexInputState   = &vtxInputStateCreateInfo,
		.pInputAssemblyState = &inputAsmStateCreateInfo,
	};

	VkPipeline vertexInputInterface;
	VK_CHECK( vkCreateGraphicsPipelines( this->device, 0, 1, &pipelineLibCreateInfo, 0, &vertexInputInterface ) );
	VkDbgNameObj( vertexInputInterface, this->device, name );

	return vertexInputInterface;
}
VkPipeline vk_device::CreateVertexShaderPipelineStage(
	std::span<VkDynamicState> dynamicState, 
	const vk_rasterization_config& rasterCfg, 
	const vk_shader_metadata& shader,
	const char* name 
) {
	VkGraphicsPipelineLibraryCreateInfoEXT pipeLibExtInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
		.flags = VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT
	};

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = ( u32 ) std::size( dynamicState ),
		.pDynamicStates = std::data( dynamicState )
	};

	VkPipelineRasterizationConservativeStateCreateInfoEXT conservativeRasterState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_CONSERVATIVE_STATE_CREATE_INFO_EXT,
		.conservativeRasterizationMode = VK_CONSERVATIVE_RASTERIZATION_MODE_OVERESTIMATE_EXT,
		.extraPrimitiveOverestimationSize = rasterCfg.extraPrimitiveOverestimationSize
	};

	VkPipelineRasterizationStateCreateInfo rasterInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.pNext = rasterCfg.conservativeRasterEnable ? &conservativeRasterState : 0,
		.depthClampEnable = 0,
		.rasterizerDiscardEnable = 0,
		.polygonMode = rasterCfg.polygonMode,
		.cullMode = rasterCfg.cullFlags,
		.frontFace = rasterCfg.frontFace,
		.lineWidth = 1.0f
	};

	VK_CHECK( VK_INTERNAL_ERROR( shader.stage != VK_SHADER_STAGE_VERTEX_BIT ) );
	VkShaderModuleCreateInfo vtxModuleCreateInfo = {
		.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = std::size( shader.spvByteCode ),
		.pCode    = reinterpret_cast<const u32*>( std::data( shader.spvByteCode ) )
	};
	
	VkPipelineShaderStageCreateInfo vtxStageCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = &vtxModuleCreateInfo,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.pName = shader.entryPointName,
	};

	VkGraphicsPipelineCreateInfo pipelineLibCreateInfo = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext               = &pipeLibExtInfo,
		.flags               = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
		.stageCount = 1u,
		.pStages = &vtxStageCreateInfo,
		.pRasterizationState = &rasterInfo,
		.layout = this->descriptorManager.pipelineLayout
	};

	VkPipeline vertexShaderStage;
	VK_CHECK( vkCreateGraphicsPipelines( this->device, 0, 1, &pipelineLibCreateInfo, 0, &vertexShaderStage ) );
	VkDbgNameObj( vertexShaderStage, this->device, name );

	return vertexShaderStage;
}
VkPipeline vk_device::CreateFragmentShaderPipelineStage(
	vk_depth_stencil_config depthStencilCfg,
	const vk_shader_metadata& shader,
	const char* name
) {
	VkGraphicsPipelineLibraryCreateInfoEXT pipeLibExtInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
		.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT 
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, 
		.depthTestEnable = depthStencilCfg.depthTestEnable,
		.depthWriteEnable = depthStencilCfg.depthWrite,
		.depthCompareOp = VK_COMPARE_OP_GREATER,
		.depthBoundsTestEnable = VK_TRUE,
		.minDepthBounds = 0.0f,
		.maxDepthBounds = 1.0f
	};

	VK_CHECK( VK_INTERNAL_ERROR( shader.stage != VK_SHADER_STAGE_FRAGMENT_BIT ) );
	VkShaderModuleCreateInfo fragModuleCreateInfo = {
		.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = std::size( shader.spvByteCode ),
		.pCode    = reinterpret_cast<const u32*>( std::data( shader.spvByteCode ) )
	};

	VkPipelineShaderStageCreateInfo fragStageCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = &fragModuleCreateInfo,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.pName = shader.entryPointName,
	};

	VkGraphicsPipelineCreateInfo pipelineLibCreateInfo = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext               = &pipeLibExtInfo,
		.flags               = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
		.stageCount = 1u,
		.pStages = &fragStageCreateInfo,
		.pDepthStencilState = &depthStencilState,
		.layout = this->descriptorManager.pipelineLayout
	};

	VkPipeline fragmentShaderStage;
	VK_CHECK( vkCreateGraphicsPipelines( this->device, 0, 1, &pipelineLibCreateInfo, 0, &fragmentShaderStage ) );
	VkDbgNameObj( fragmentShaderStage, this->device, name );

	return fragmentShaderStage;
}
VkPipeline vk_device::CreateFragmentOutputInterfacePipelineStage(
	std::span<VkFormat> colorAttachmentFormats,
	VkFormat depthAttachmentFormat,
	const vk_color_blending_config& colorBlendingCfg,
	bool colorBlendingEnable,
	const char* name
) {
	VkGraphicsPipelineLibraryCreateInfoEXT pipeLibExtInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT,
		.flags = VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT 
	};

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
	};

	constexpr VkColorComponentFlags colWriteMask = 
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendAttachmentState blendConfig = {
		.blendEnable = colorBlendingEnable,
		.srcColorBlendFactor = colorBlendingCfg.srcColorBlendFactor,
		.dstColorBlendFactor = colorBlendingCfg.dstColorBlendFactor,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = colorBlendingCfg.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = colorBlendingCfg.dstAlphaBlendFactor,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask = colWriteMask
	};

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blendConfig
	};

	VkPipelineRenderingCreateInfo renderingInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
		.pNext = &pipeLibExtInfo,
		.colorAttachmentCount = ( u32 ) std::size( colorAttachmentFormats ),
		.pColorAttachmentFormats = std::data( colorAttachmentFormats ),
		.depthAttachmentFormat = depthAttachmentFormat
	};

	VkGraphicsPipelineCreateInfo pipelineLibCreateInfo = {
		.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext               = &renderingInfo,
		.flags               = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR | VK_PIPELINE_CREATE_RETAIN_LINK_TIME_OPTIMIZATION_INFO_BIT_EXT,
		.pMultisampleState = &multisamplingInfo,
		.pColorBlendState = &colorBlendStateInfo,
	};

	VkPipeline fragmentOutputInterfaceStage;
	VK_CHECK( vkCreateGraphicsPipelines( this->device, 0, 1, &pipelineLibCreateInfo, 0, &fragmentOutputInterfaceStage ) );
	VkDbgNameObj( fragmentOutputInterfaceStage, this->device, name );

	return fragmentOutputInterfaceStage;
}

VkPipeline vk_device::CreateGraphicsPipeline( std::span<VkPipeline> libaries, const char* name )
{
	VkPipelineLibraryCreateInfoKHR linkingInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR,
		.libraryCount = ( u32 ) std::size( libaries ),
		.pLibraries = std::data( libaries )
	};

	VkGraphicsPipelineCreateInfo executablePipelineCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.pNext = &linkingInfo,
		.flags = VK_PIPELINE_CREATE_LINK_TIME_OPTIMIZATION_BIT_EXT
	};

	VkPipeline executable;
	VK_CHECK( vkCreateGraphicsPipelines( this->device, 0, 1, &executablePipelineCreateInfo, 0, &executable ) );
	VkDbgNameObj( executable, this->device, name );

	return executable;
}

VkPipeline vk_device::CreateComputePipeline( 
	const vk_shader& shader, 
	const std::span<vk_specialization_type> consts, 
	const char* name
) {
	VK_CHECK( VK_INTERNAL_ERROR( shader.stage != VK_SHADER_STAGE_COMPUTE_BIT ) );

	std::vector<VkSpecializationMapEntry> specializations = VkMakeSpecializationMap( consts );
	VkSpecializationInfo specInfo = {
		.mapEntryCount = (u32) std::size( specializations ),
		.pMapEntries = std::data( specializations ),
		.dataSize = std::size( consts ) * sizeof( decltype( consts )::value_type ),
		.pData = std::data( consts )
	};

	VkPipelineShaderStageCreateInfo stage = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = shader.hndl,
		.pName = shader.entryPointName,
		.pSpecializationInfo = &specInfo,
	};

	VkComputePipelineCreateInfo compPipelineInfo = { 
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = stage,
		.layout = this->descriptorManager.pipelineLayout
	};

	VkPipeline pipeline = 0;
	VK_CHECK( vkCreateComputePipelines( this->device, 0, 1, &compPipelineInfo, 0, &pipeline ) );

	VkDbgNameObj( pipeline, this->device, name );

	return pipeline;
}

vk_gpu_timer vk_device::CreateGpuTimer( u32 queryCount, const char* name ) const
{
	VkQueryPoolCreateInfo queryPoolInfo = {
		.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
		.queryType = VK_QUERY_TYPE_TIMESTAMP,
		.queryCount = queryCount,
		//.pipelineStatistics
	};

	VkQueryPool queryPool;
	VK_CHECK( vkCreateQueryPool( this->device, &queryPoolInfo, 0, &queryPool ) );
	VkDbgNameObj( queryPool, this->device, name );

	constexpr VkBufferUsageFlags usgQuery = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	auto pBuffer = this->CreateBuffer( 
		{ .name = "Buff_Timestamp_Queries", .usage = usgQuery, .elemCount = queryCount, .stride = sizeof( u64 ) } );

	return {
		.resultBuff = std::move( pBuffer ),
		.queryPool = queryPool,
		.queryCount = queryCount,
		.timestampPeriod = this->gpuProps.limits.timestampPeriod
	};
}
