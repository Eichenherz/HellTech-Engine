#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES

#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>

#define VMA_IMPLEMENTATION
#include <3rdParty/vk_mem_alloc.h>

#define VOLK_IMPLEMENTATION 
#include <Volk/volk.h>

#include "vk_device.h"
#include "vk_descriptor.h"

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

inline void VkCheckFormatProperties( VkPhysicalDevice vkGpu, VkImageUsageFlags usg, VkFormat format )
{
	VkFormatFeatureFlags formatFeatures = 0;
	if( usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( usg & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	if( usg & VK_IMAGE_USAGE_HOST_TRANSFER_BIT ) formatFeatures |= VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT;

	VkFormatProperties3 formatProps3 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3 };
	VkFormatProperties2 fomratProps2 = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &formatProps3 };

	vkGetPhysicalDeviceFormatProperties2( vkGpu, format, &fomratProps2 );

	HT_ASSERT( ( formatProps3.optimalTilingFeatures & formatFeatures ) == formatFeatures );
	// Fallback to a different format or use other means of uploading data
}


static void VkMakeCreateQueueInfoWithProperties( 
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
				HT_ASSERT( present );
			}
			desiredQueueIdx = qIdx;
			queueFlags = familyFlags;
			break;
		}
	}

	HT_ASSERT( desiredQueueIdx != u32( -1 ) );
	VkDeviceQueueCreateInfo createInfo = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = desiredQueueIdx,
		.queueCount = 1,
		.pQueuePriorities = pQueuePriorities
	};

	outCreateInfo = createInfo;
	outQueueFlags = queueFlags;
}

static vk_queue VkCreateQueue( VkDevice vkDevice, u32 queueFamilyIndex, VkQueueFlags desiredProps, bool canPresent )
{
	VkQueue	hndl;
	vkGetDeviceQueue( vkDevice, queueFamilyIndex, 0, &hndl );
	HT_ASSERT( hndl );

	return {
		.hndl = hndl,
		.index = queueFamilyIndex,
		.familyFlags = desiredProps,
		.canPresent = canPresent
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

static vk_desc_state VkMakeDescriptorState( VkDevice vkDevice, const VkPhysicalDeviceProperties& gpuProps )
{
	constexpr u32 maxSize = u16( -1 );
	constexpr u32 maxSetCount = 1;

	vk_desc_table descTable = {
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSamplers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageBuffers ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetStorageImages ),
		std::min( maxSize, gpuProps.limits.maxDescriptorSetSampledImages )
	};

	VkDescriptorPoolSize poolSizes[ vk_desc_table::BINDING_COUNT ] = {};
	for( u32 i = 0; i < vk_desc_table::BINDING_COUNT; ++i )
	{
		VkDescriptorType type = vk_desc_table::bindingToTypeMap[ i ];
		poolSizes[ i ] = { .type = type, .descriptorCount = descTable[ type ] };
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

	VkDescriptorBindingFlags bindingFlags[ vk_desc_table::BINDING_COUNT ] = {};
	VkDescriptorSetLayoutBinding descSetLayout[ vk_desc_table::BINDING_COUNT ] = {};
	for( u32 i = 0; i < vk_desc_table::BINDING_COUNT; ++i )
	{
		VkDescriptorType type = vk_desc_table::bindingToTypeMap[ i ];

		descSetLayout[ i ] = {
			.binding = i,
			.descriptorType = type,
			.descriptorCount = descTable[ type ],
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

	return {
		.pool = vkDescPool,
		.setLayout = vkDescSetLayout,
		.set = vkDescSet,
		.descTable = descTable
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

vk_device_ctx VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf, const renderer_config& cfg )
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

	volkLoadDevice( vkDevice );

	vk_queue gfxQueue = VkCreateQueue( vkDevice, queueCreateInfos[ 0 ].queueFamilyIndex, queueFlags, true );

	vk_desc_state descState = VkMakeDescriptorState( vkDevice, gpuProps2.properties );

	return {
		.sc = VkMakeSwapchain( vkDevice, gpu, vkSurf, gfxQueue.index, cfg.desiredSwapchainFormat, cfg.swapchainImageCount ),
		.descState = descState,
		.gfxQueue = gfxQueue,
		.allocator = MakeVmaAllocator( gpu, vkDevice, vkInst, VK_API_VERSION_1_4 ), // TODO: main file with this );
		.gpuProps = gpuProps2.properties,
		.gpu = gpu,
		.device = vkDevice,
		.globalPipelineLayout = VkMakeGlobalPipelineLayout( vkDevice, descState.setLayout, gpuProps2.properties ),
		.deviceMask = u32( 1u << deviceIdx ),
		.timestampPeriod = gpuProps2.properties.limits.timestampPeriod,
		.waveSize = ( u8 ) waveProps.subgroupSize
	};
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
	VkCheckFormatProperties( this->gpu, imgInfo.usg, imgInfo.format );

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

	VkImageView vkImgView = VkMakeImgView(
		this->device, img, imageInfo.format, 0, imageInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imageInfo.arrayLayers );

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

VkPipeline vk_device_ctx::CreateGfxPipeline( 
	std::span<const vk_gfx_shader_stage> shaderStages, 
	std::span<const VkDynamicState> dynamicStates, 
	const VkFormat* pColorAttachmentFormats, 
	u32 colorAttachmentCount, 
	VkFormat depthAttachmentFormat, 
	const vk_gfx_pipeline_state& pipelineState 
) {
	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = pipelineState.primTopology,
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
		.polygonMode = pipelineState.polyMode,
		.cullMode = pipelineState.cullFlags,
		.frontFace = pipelineState.frontFace,
		.lineWidth = 1.0f
	};

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = pipelineState.depthTestEnable,
		.depthWriteEnable = pipelineState.depthWrite,
		.depthCompareOp = VK_COMPARE_OP_GREATER,
		.depthBoundsTestEnable = VK_TRUE,
		.minDepthBounds = 0,
		.maxDepthBounds = 1.0f
	};

	VkPipelineColorBlendAttachmentState blendConfig = {
		.blendEnable = pipelineState.blendCol,
		.srcColorBlendFactor = pipelineState.srcColorBlendFactor,
		.dstColorBlendFactor = pipelineState.dstColorBlendFactor,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = pipelineState.srcAlphaBlendFactor,
		.dstAlphaBlendFactor = pipelineState.dstAlphaBlendFactor,
		.alphaBlendOp = VK_BLEND_OP_ADD,
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

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
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
		.layout = globalPipelineLayout,
		.basePipelineIndex = -1
	};

	VkPipeline vkGfxPipeline;
	VK_CHECK( vkCreateGraphicsPipelines( device, 0, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	return vkGfxPipeline;
}

VkPipeline vk_device_ctx::CreateComptuePipeline( const vk_shader& shader, vk_specializations consts, const char* pName )
{
	std::vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageRequiredSubgroupSizeCreateInfo subgroupSizeInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO,
		.requiredSubgroupSize = waveSize
	};

	VkPipelineShaderStageCreateInfo stage = { 
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = &subgroupSizeInfo,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = shader.module,
		.pName = shader.entryPoint.c_str(),
		.pSpecializationInfo = &specInfo
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

unique_shader_ptr vk_device_ctx::CreateShaderFromSpirv( std::span<const u8> spvByteCode )
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

void vk_device_ctx::UpdateDescriptorIndices( std::span<const vk_descriptor_write> updateCache )
{
	if( !std::size( updateCache ) ) return;

	std::vector<VkWriteDescriptorSet> writes;
	for( const auto& update : updateCache )
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
			.dstSet = descState.set,
			.dstBinding = bindingSlotIdx,
			.dstArrayElement = updateIdx,
			.descriptorCount = 1,
			.descriptorType = descType,
			.pImageInfo = pImageInfo,
			.pBufferInfo = pBufferInfo
		};
		writes.push_back( writeEntryInfo );
	}

	vkUpdateDescriptorSets( device, std::size( writes ), std::data( writes ), 0, 0 );
}
