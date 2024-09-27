#include "r_vk_resources.hpp"

inline i32
VkFindMemTypeIdx(
	const VkPhysicalDeviceMemoryProperties* pVkMemProps,
	VkMemoryPropertyFlags				requiredProps,
	u32									memTypeBitsRequirement
) {
	for( u64 memIdx = 0; memIdx < pVkMemProps->memoryTypeCount; ++memIdx )
	{
		u32 memTypeBits = ( 1 << memIdx );
		bool isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps->memoryTypes[ memIdx ].propertyFlags;
		bool hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps ) return ( i32 ) memIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( "Memory type unmatch !" ) );

	return -1;
}

// TODO: pass aspect mask ? ?
inline VkImageView
VkMakeImgView(
	VkDevice		vkDevice,
	VkImage			vkImg,
	VkFormat		imgFormat,
	u32				mipLevel,
	u32				levelCount,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32				arrayLayer = 0,
	u32				layerCount = 1
) {
	VkImageAspectFlags aspectMask =
		( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewInfo = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = vkImg,
		.viewType = imgViewType,
		.format = imgFormat,
		.subresourceRange = {
			.aspectMask = aspectMask,
			.baseMipLevel = mipLevel,
			.levelCount = levelCount,
			.baseArrayLayer = arrayLayer,
			.layerCount = layerCount,
	    }
	};
	VkImageView view;
	VK_CHECK( vkCreateImageView( vkDevice, &viewInfo, 0, &view ) );

	return view;
}

inline vk_mem_requirements VkGetBufferMemRequrements( 
	VkDevice vkDevice, 
	VkPhysicalDevice gpu, 
	VkBuffer hndl, 
	VkBufferUsageFlags usage, 
	VkMemoryPropertyFlags memTypeProperties 
) {
	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = { .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2, .buffer = hndl };
	vkGetBufferMemoryRequirements2( vkDevice, &buffMemReqs2, &memReqs2 );

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );

	VkMemoryAllocateFlags allocFlags =
		( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .buffer = buffMemReqs2.buffer };
	bool dedicatedAlloc = dedicatedReqs.requiresDedicatedAllocation;

	return {
		.dedicated = dedicatedAlloc ? dedicatedAllocateInfo : VkMemoryDedicatedAllocateInfo{},
		.size = memReqs2.memoryRequirements.size,
		.align = memReqs2.memoryRequirements.alignment,
		.memTypeIdx = memTypeIdx,
		.allocFlags = allocFlags,
		.requiresDedicated = dedicatedAlloc
	};
}

// TODO: explicitly pass dest memory 
vk_buffer VkCreateAllocBindBuffer( const vk_device& vkDevice, const buffer_info& buffInfo, vk_mem_usage memUsg ) 
{
	VkBuffer hndl = 0;
	VkBufferCreateInfo bufferInfo = { 
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 
		//.flags = ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ?VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT : 0,
		.size = buffInfo.elemCount * buffInfo.stride,
		.usage = buffInfo.usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};
	VK_CHECK( vkCreateBuffer( vkDevice.device, &bufferInfo, 0, &hndl ) );

	vk_mem_arena& vkArena = VkDeviceGetBufferMemArenaByUsage( const_cast< vk_device& >( vkDevice ), memUsg );
	vk_mem_requirements memReqs = VkGetBufferMemRequrements( 
		vkDevice.device, vkDevice.gpu, hndl, bufferInfo.usage, vkArena.memTypeProperties );
	vk_allocation bufferMem = vkArena.VkArenaAlignAlloc( vkDevice.device, memReqs );

	vk_buffer buffData = {
		.hndl = hndl, 
		.mem = bufferMem.deviceMem, 
		.size = bufferInfo.size, 
		.hostVisible = ( bufferMem.hostVisible ) ? ( bufferMem.hostVisible + bufferMem.dataOffset ) : 0,
		.usgFlags = bufferInfo.usage,
		.stride = buffInfo.stride
	};
	VK_CHECK( vkBindBufferMemory( vkDevice.device, buffData.hndl, buffData.mem, bufferMem.dataOffset ) );

	if( memReqs.allocFlags == VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
	{
		buffData.devicePointer = VkGetBufferDeviceAddress( vkDevice.device, buffData.hndl );
		assert( buffData.devicePointer );
	}

	if( buffInfo.name )
	{
		VkDbgNameObj( buffData.hndl, vkDevice.device, buffInfo.name );
	}
	return buffData;
}


inline vk_mem_requirements VkGetImageMemRequrements(
	VkDevice vkDevice,
	VkPhysicalDevice gpu,
	VkImage hndl,
	VkMemoryPropertyFlags memTypeProperties
) {
	VkImageMemoryRequirementsInfo2 imgReqs2 = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, .image = hndl };
	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkDevice, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .image = imgReqs2.image };
	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );

	return {
		.dedicated = dedicatedAlloc ? dedicatedAllocateInfo : VkMemoryDedicatedAllocateInfo{},
		.size = memReqs2.memoryRequirements.size,
		.align = memReqs2.memoryRequirements.alignment,
		.memTypeIdx = memTypeIdx,
		.allocFlags = 0,
		.requiresDedicated = dedicatedAlloc
	};
}

vk_image VkCreateAllocBindImage( const vk_device& vkDevice, const image_info& imgInfo ) 
{
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( vkDevice.gpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );


	vk_image img = {};

	VkImageCreateInfo imageInfo = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = img.nativeFormat = imgInfo.format,
		.extent = { img.width = imgInfo.width, img.height = imgInfo.height, 1 },
		.mipLevels = img.mipCount = imgInfo.mipCount,
		.arrayLayers = img.layerCount = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = img.usageFlags = imgInfo.usg,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VK_CHECK( vkCreateImage( vkDevice.device, &imageInfo, 0, &img.hndl ) );


	vk_mem_arena& vkArena = const_cast< vk_device& >( vkDevice ).memory.arenas[ vk_arena_type::IMAGES ];
	vk_mem_requirements memReqs = VkGetImageMemRequrements( vkDevice.device, vkDevice.gpu, img.hndl, vkArena.memTypeProperties );
	vk_allocation imgMem = vkArena.VkArenaAlignAlloc( vkDevice.device, memReqs );

	img.mem = imgMem.deviceMem;
	VK_CHECK( vkBindImageMemory( vkDevice.device, img.hndl, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imageInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown vk_image type !" ) ); break;
	};
	img.view = VkMakeImgView(
		vkDevice.device, img.hndl, imageInfo.format, 0, imageInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imageInfo.arrayLayers );
	for( u64 i = 0; i < img.mipCount; ++i )
	{
		img.optionalViews[ i ] = VkMakeImgView( vkDevice.device, img.hndl, img.nativeFormat, i, 1 );
	}

	if( imgInfo.name )
	{
		VkDbgNameObj( img.hndl, vkDevice.device, imgInfo.name );
	}

	return img;
}