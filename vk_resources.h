#ifndef __VK_RESOURCES_H__
#define __VK_RESOURCES_H__

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
#include <vulkan.h>
#include "vk_procs.h"

#include "vk_error.h"
#include "vk_memory.h"
#include "core_types.h"
#include "sys_os_api.h"
#include <type_traits>

constexpr u64 MAX_MIP_LEVELS = 12;

// TODO: keep memory in buffer ?
// TODO: keep devicePointer here ?
struct vk_buffer
{
	VkBuffer		hndl;
	VkDeviceMemory	mem;
	u64				size;
	u8*             hostVisible;
	u64				devicePointer;
	VkBufferUsageFlags usgFlags;
	u32             stride;
};

inline VkDescriptorBufferInfo Descriptor( const vk_buffer& b )
{
	//return VkDescriptorBufferInfo{ hndl,offset,size };
	return VkDescriptorBufferInfo{ b.hndl,0,b.size };
}

// TODO: use a texture_desc struct
// TODO: add more data ?
// TODO: VkDescriptorImageInfo 
// TODO: rsc don't directly store the memory, rsc manager refrences it ?
struct vk_image
{
	VkImage			hndl;
	VkImageView		view;
	VkImageView     optionalViews[ MAX_MIP_LEVELS ];
	VkDeviceMemory	mem;
	VkImageUsageFlags usageFlags;
	VkFormat		nativeFormat;
	u16				width;
	u16				height;
	u8				layerCount;
	u8				mipCount;

	inline VkExtent3D Extent3D() const
	{
		return { width,height,1 };
	}
};

// TODO: pass aspect mask ? ?
inline static VkImageView
VkMakeImgView(
	VkDevice		vkDevice,
	VkImage			vkImg,
	VkFormat		imgFormat,
	u32				mipLevel,
	u32				levelCount,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32				arrayLayer = 0,
	u32				layerCount = 1
){
	VkImageAspectFlags aspectMask =
		( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
	viewInfo.viewType = imgViewType;
	viewInfo.format = imgFormat;
	viewInfo.subresourceRange.aspectMask = aspectMask;
	viewInfo.subresourceRange.baseMipLevel = mipLevel;
	viewInfo.subresourceRange.levelCount = levelCount;
	viewInfo.subresourceRange.baseArrayLayer = arrayLayer;
	viewInfo.subresourceRange.layerCount = layerCount;
	viewInfo.image = vkImg;

	VkImageView view;
	VK_CHECK( vkCreateImageView( vkDevice, &viewInfo, 0, &view ) );

	return view;
}

// TODO: pass device for rsc creation, and stuff
// TODO: re-think resource creation and management

struct buffer_info
{
	const char* name;
	VkBufferUsageFlags usage;
	u32 elemCount;
	u32 stride;
};

struct image_info
{
	const char* name;
	VkFormat		format;
	VkImageUsageFlags	usg;
	u16				width;
	u16				height;
	u8				layerCount;
	u8				mipCount;
};

inline u64 VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
	static_assert( std::is_same<VkDeviceAddress, u64>::value );

	VkBufferDeviceAddressInfo deviceAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	deviceAddrInfo.buffer = hndl;

	return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
}

static vk_buffer
VkCreateAllocBindBuffer(
	const buffer_info& buffInfo,
	VkDevice vkDevice,
	VkPhysicalDevice vkGpu,
	vk_mem_arena& vkArena
) {
	vk_buffer buffData = {};

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	//bufferInfo.flags = ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? 
	//	VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT : 0;
	bufferInfo.size = buffInfo.elemCount * buffInfo.stride;
	bufferInfo.usage = buffInfo.usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( vkCreateBuffer( vkDevice, &bufferInfo, 0, &buffData.hndl ) );

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
	buffMemReqs2.buffer = buffData.hndl;
	vkGetBufferMemoryRequirements2( vkDevice, &buffMemReqs2, &memReqs2 );

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkGpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena.memTypeIdx ) ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	VkMemoryAllocateFlags allocFlags =
		( bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.buffer = buffMemReqs2.buffer;

	bool dedicatedAlloc = dedicatedReqs.requiresDedicatedAllocation; //|| dedicatedReqs.prefersDedicatedAllocation;

	vk_allocation bufferMem = VkArenaAlignAlloc( &vkArena,
												 memReqs2.memoryRequirements.size,
												 memReqs2.memoryRequirements.alignment,
												 vkArena.memTypeIdx,
												 allocFlags,
												 dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	buffData.mem = bufferMem.deviceMem;
	buffData.hostVisible = ( bufferMem.hostVisible ) ? ( bufferMem.hostVisible + bufferMem.dataOffset ) : 0;
	buffData.size = bufferInfo.size;
	buffData.stride = buffInfo.stride;

	VK_CHECK( vkBindBufferMemory( vkDevice, buffData.hndl, buffData.mem, bufferMem.dataOffset ) );

	if( allocFlags == VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
	{
		buffData.devicePointer = VkGetBufferDeviceAddress( vkArena.device, buffData.hndl );
		assert( buffData.devicePointer );
	}

	buffData.usgFlags = bufferInfo.usage = bufferInfo.usage;

	if( buffInfo.name ) VkDbgNameObj( buffData.hndl, vkDevice, buffInfo.name );

	return buffData;
}

static vk_image
VkCreateAllocBindImage(
	const image_info& imgInfo,
	vk_mem_arena& vkArena,
	VkDevice            vkDevice,
	VkPhysicalDevice	gpu
) {
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usg & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usg & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );


	vk_image img = {};

	VkImageCreateInfo imageInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = img.nativeFormat = imgInfo.format;
	imageInfo.extent = { img.width = imgInfo.width, img.height = imgInfo.height, 1};
	imageInfo.mipLevels = img.mipCount = imgInfo.mipCount;
	imageInfo.arrayLayers = img.layerCount = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = img.usageFlags = imgInfo.usg;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( vkCreateImage( vkDevice, &imageInfo, 0, &img.hndl ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.hndl;

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkDevice, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	//VK_CHECK( VK_INTERNAL_ERROR( memTypeIdx == vkArena->memTypeIdx ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc(
		//vkDevice,
		&vkArena,
		memReqs2.memoryRequirements.size,
		memReqs2.memoryRequirements.alignment,
		vkArena.memTypeIdx,
		0,
		dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkDevice, img.hndl, img.mem, imgMem.dataOffset ) );

	img.view = VkMakeImgView(
		vkDevice, img.hndl, imgInfo.format, 0, imageInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imageInfo.arrayLayers );

	if( imgInfo.name ) VkDbgNameObj( img.hndl, vkDevice, imgInfo.name );

	return img;
}

// TODO: Pass BuffCreateInfo
static vk_buffer
VkCreateAllocBindBuffer(
	u64					sizeInBytes,
	VkBufferUsageFlags	usage,
	VkPhysicalDevice	vkGpu,
	vk_mem_arena& vkArena
){
	vk_buffer buffData = {};

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	//bufferInfo.flags = ( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? 
	//	VK_BUFFER_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT : 0;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( vkCreateBuffer( vkArena.device, &bufferInfo, 0, &buffData.hndl ) );

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
	buffMemReqs2.buffer = buffData.hndl;
	vkGetBufferMemoryRequirements2( vkArena.device, &buffMemReqs2, &memReqs2 );

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkGpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena.memTypeIdx ) ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	VkMemoryAllocateFlags allocFlags =
		( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.buffer = buffMemReqs2.buffer;

	bool dedicatedAlloc = dedicatedReqs.requiresDedicatedAllocation; //|| dedicatedReqs.prefersDedicatedAllocation;

	vk_allocation bufferMem = VkArenaAlignAlloc( &vkArena,
												 memReqs2.memoryRequirements.size,
												 memReqs2.memoryRequirements.alignment,
												 vkArena.memTypeIdx,
												 allocFlags,
												 dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	buffData.mem = bufferMem.deviceMem;
	buffData.hostVisible = ( bufferMem.hostVisible ) ? ( bufferMem.hostVisible + bufferMem.dataOffset ) : 0;
	buffData.size = sizeInBytes;

	VK_CHECK( vkBindBufferMemory( vkArena.device, buffData.hndl, buffData.mem, bufferMem.dataOffset ) );

	if( allocFlags == VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
	{
		buffData.devicePointer = VkGetBufferDeviceAddress( vkArena.device, buffData.hndl );
		assert( buffData.devicePointer );
	}

	buffData.usgFlags = bufferInfo.usage = usage;

	return buffData;
}

static vk_image
VkCreateAllocBindImage(
	const VkImageCreateInfo& imgInfo,
	vk_mem_arena& vkArena,
	VkPhysicalDevice			vkGpu
){
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( vkGpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );

	vk_image img = {};
	img.nativeFormat = imgInfo.format;
	img.width = imgInfo.extent.width;
	img.height = imgInfo.extent.height;
	img.mipCount = imgInfo.mipLevels;
	img.layerCount = imgInfo.arrayLayers;
	VK_CHECK( vkCreateImage( vkArena.device, &imgInfo, 0, &img.hndl ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.hndl;
	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkArena.device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkGpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena.memTypeIdx ) ) );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc( &vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  vkArena.memTypeIdx,
											  0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkArena.device, img.hndl, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown vk_image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena.device, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

static vk_image
VkCreateAllocBindImage(
	VkFormat			format,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			extent,
	u32					mipCount,
	vk_mem_arena& vkArena,
	VkDevice            vkDevice,
	VkPhysicalDevice	gpu
) {
	VkFormatFeatureFlags formatFeatures = 0;
	if( usageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );


	vk_image img = {};

	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = img.nativeFormat = format;
	img.width = extent.width;
	img.height = extent.height;
	imgInfo.extent = extent;
	imgInfo.mipLevels = img.mipCount = mipCount;
	imgInfo.arrayLayers = img.layerCount = 1;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = img.usageFlags = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( vkCreateImage( vkDevice, &imgInfo, 0, &img.hndl ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.hndl;

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkDevice, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	//VK_CHECK( VK_INTERNAL_ERROR( memTypeIdx == vkArena->memTypeIdx ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc(
		//vkDevice,
		&vkArena,
		memReqs2.memoryRequirements.size,
		memReqs2.memoryRequirements.alignment,
		vkArena.memTypeIdx,
		0,
		dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkDevice, img.hndl, img.mem, imgMem.dataOffset ) );

	img.view = VkMakeImgView( 
		vkDevice, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imgInfo.arrayLayers );

	return img;
}

// TODO: fomralize vk_image creation even more ?
static vk_image
VkCreateAllocBindImage(
	VkFormat			format,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			extent,
	u32					mipCount,
	vk_mem_arena&		vkArena,
	VkPhysicalDevice	vkGpu
){
	VkFormatFeatureFlags formatFeatures = 0;
	if( usageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( vkGpu, format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );


	vk_image img = {};

	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = img.nativeFormat = format;
	img.width = extent.width;
	img.height = extent.height;
	imgInfo.extent = extent;
	imgInfo.mipLevels = img.mipCount = mipCount;
	imgInfo.arrayLayers = img.layerCount = 1;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( vkCreateImage( vkArena.device, &imgInfo, 0, &img.hndl ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.hndl;

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkArena.device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	bool dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkGpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena.memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	//VK_CHECK( VK_INTERNAL_ERROR( memTypeIdx == vkArena->memTypeIdx ) );
	assert( memTypeIdx == vkArena.memTypeIdx );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc( &vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  vkArena.memTypeIdx,
											  0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkArena.device, img.hndl, img.mem, imgMem.dataOffset ) );

	img.view = VkMakeImgView( 
		vkArena.device, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, VK_IMAGE_VIEW_TYPE_2D, 0, imgInfo.arrayLayers );

	return img;
}



inline VkImageMemoryBarrier
VkMakeImgBarrier(
	VkImage				image,
	VkAccessFlags		srcAccessMask,
	VkAccessFlags		dstAccessMask,
	VkImageLayout		oldLayout,
	VkImageLayout		newLayout,
	VkImageAspectFlags	aspectMask,
	u32					srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32					dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32					baseMipLevel = 0,
	u32					mipCount = 0,
	u32					baseArrayLayer = 0,
	u32					layerCount = 0
){
	VkImageMemoryBarrier imgMemBarrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	imgMemBarrier.srcAccessMask = srcAccessMask;
	imgMemBarrier.dstAccessMask = dstAccessMask;
	imgMemBarrier.oldLayout = oldLayout;
	imgMemBarrier.newLayout = newLayout;
	imgMemBarrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
	imgMemBarrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	imgMemBarrier.image = image;
	imgMemBarrier.subresourceRange.aspectMask = aspectMask;
	imgMemBarrier.subresourceRange.baseMipLevel = baseMipLevel;
	imgMemBarrier.subresourceRange.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS;
	imgMemBarrier.subresourceRange.baseArrayLayer = baseArrayLayer;
	imgMemBarrier.subresourceRange.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS;

	return imgMemBarrier;
}

inline static VkBufferMemoryBarrier2
VkMakeBufferBarrier2(
	VkBuffer					hBuff,
	VkAccessFlags2			srcAccess,
	VkPipelineStageFlags2	srcStage,
	VkAccessFlags2		dstAccess,
	VkPipelineStageFlags2	dstStage,
	VkDeviceSize				buffOffset = 0,
	VkDeviceSize				buffSize = VK_WHOLE_SIZE,
	u32							srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED
){
	VkBufferMemoryBarrier2 barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.srcQueueFamilyIndex = srcQueueFamIdx;
	barrier.dstQueueFamilyIndex = dstQueueFamIdx;
	barrier.buffer = hBuff;
	barrier.offset = buffOffset;
	barrier.size = buffSize;

	return barrier;
}

inline static VkBufferMemoryBarrier2 VkReverseBufferBarrier2( const VkBufferMemoryBarrier2& b )
{
	VkBufferMemoryBarrier2 barrier = b;
	std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
	std::swap( barrier.srcStageMask, barrier.dstStageMask );

	return barrier;
}

inline static VkImageMemoryBarrier2
VkMakeImageBarrier2(
	VkImage						hImg,
	VkAccessFlags2           srcAccessMask,
	VkPipelineStageFlags2    srcStageMask,
	VkAccessFlags2          dstAccessMask,
	VkPipelineStageFlags2	dstStageMask,
	VkImageLayout               oldLayout,
	VkImageLayout               newLayout,
	VkImageAspectFlags			aspectMask,
	u32							baseMipLevel = 0,
	u32							mipCount = 0,
	u32							baseArrayLayer = 0,
	u32							layerCount = 0,
	u32							srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	u32							dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
){
	VkImageMemoryBarrier2 barrier = { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	barrier.image = hImg;
	barrier.srcAccessMask = srcAccessMask;
	barrier.srcStageMask = srcStageMask;
	barrier.dstAccessMask = dstAccessMask;
	barrier.dstStageMask = dstStageMask;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
	barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
	barrier.subresourceRange.aspectMask = aspectMask;
	barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
	barrier.subresourceRange.baseMipLevel = baseMipLevel;
	barrier.subresourceRange.layerCount = ( layerCount ) ? layerCount : VK_REMAINING_ARRAY_LAYERS;
	barrier.subresourceRange.levelCount = ( mipCount ) ? mipCount : VK_REMAINING_MIP_LEVELS;

	return barrier;
}



#endif // !__VK_RESOURCES_H__
