#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include <string_view>
#include <assert.h>
#include <vector>


#include "sys_os_api.h"
#include "core_lib_api.h"



constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 256 * MB;
constexpr u64 MAX_MIP_LEVELS = 12;
constexpr bool objectNaming = 1;

template<typename VKH>
constexpr VkObjectType VkGetObjTypeFromHandle()
{
#define if_same_type( VKT ) if( std::is_same<VKH, VKT>::value )

	if_same_type( VkInstance ) return VK_OBJECT_TYPE_INSTANCE;
	if_same_type( VkPhysicalDevice ) return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
	if_same_type( VkDevice ) return VK_OBJECT_TYPE_DEVICE;
	if_same_type( VkSemaphore ) return VK_OBJECT_TYPE_SEMAPHORE;
	if_same_type( VkCommandBuffer ) return VK_OBJECT_TYPE_COMMAND_BUFFER;
	if_same_type( VkFence ) return VK_OBJECT_TYPE_FENCE;
	if_same_type( VkDeviceMemory ) return VK_OBJECT_TYPE_DEVICE_MEMORY;
	if_same_type( VkBuffer ) return VK_OBJECT_TYPE_BUFFER;
	if_same_type( VkImage ) return VK_OBJECT_TYPE_IMAGE;
	if_same_type( VkEvent ) return VK_OBJECT_TYPE_EVENT;
	if_same_type( VkQueryPool ) return VK_OBJECT_TYPE_QUERY_POOL;
	if_same_type( VkBufferView ) return VK_OBJECT_TYPE_BUFFER_VIEW;
	if_same_type( VkImageView ) return VK_OBJECT_TYPE_IMAGE_VIEW;
	if_same_type( VkShaderModule ) return VK_OBJECT_TYPE_SHADER_MODULE;
	if_same_type( VkPipelineCache ) return VK_OBJECT_TYPE_PIPELINE_CACHE;
	if_same_type( VkPipelineLayout ) return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
	if_same_type( VkRenderPass ) return VK_OBJECT_TYPE_RENDER_PASS;
	if_same_type( VkPipeline ) return VK_OBJECT_TYPE_PIPELINE;
	if_same_type( VkDescriptorSetLayout ) return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
	if_same_type( VkSampler ) return VK_OBJECT_TYPE_SAMPLER;
	if_same_type( VkDescriptorPool ) return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
	if_same_type( VkDescriptorSet ) return VK_OBJECT_TYPE_DESCRIPTOR_SET;
	if_same_type( VkFramebuffer ) return VK_OBJECT_TYPE_FRAMEBUFFER;
	if_same_type( VkCommandPool ) return VK_OBJECT_TYPE_COMMAND_POOL;
	if_same_type( VkSamplerYcbcrConversion ) return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
	if_same_type( VkDescriptorUpdateTemplate ) return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if_same_type( VkSwapchainKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SWAPCHAIN_KHR;

	assert( 0 );
	return VK_OBJECT_TYPE_UNKNOWN;

#undef if_same_type
}
// TODO: gen from VkResult ?
inline std::string_view VkResErrorString( VkResult errorCode )
{
	switch( errorCode )
	{
#define STR(r) case VK_ ##r: return #r
		STR( NOT_READY );
		STR( TIMEOUT );
		STR( EVENT_SET );
		STR( EVENT_RESET );
		STR( INCOMPLETE );
		STR( ERROR_OUT_OF_HOST_MEMORY );
		STR( ERROR_OUT_OF_DEVICE_MEMORY );
		STR( ERROR_INITIALIZATION_FAILED );
		STR( ERROR_DEVICE_LOST );
		STR( ERROR_MEMORY_MAP_FAILED );
		STR( ERROR_LAYER_NOT_PRESENT );
		STR( ERROR_EXTENSION_NOT_PRESENT );
		STR( ERROR_FEATURE_NOT_PRESENT );
		STR( ERROR_INCOMPATIBLE_DRIVER );
		STR( ERROR_TOO_MANY_OBJECTS );
		STR( ERROR_FORMAT_NOT_SUPPORTED );
		STR( ERROR_FRAGMENTED_POOL );
		STR( ERROR_UNKNOWN );
		STR( ERROR_OUT_OF_POOL_MEMORY );
		STR( ERROR_INVALID_EXTERNAL_HANDLE );
		STR( ERROR_FRAGMENTATION );
		STR( ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS );
		STR( ERROR_SURFACE_LOST_KHR );
		STR( ERROR_NATIVE_WINDOW_IN_USE_KHR );
		STR( SUBOPTIMAL_KHR );
		STR( ERROR_OUT_OF_DATE_KHR );
		STR( ERROR_INCOMPATIBLE_DISPLAY_KHR );
		STR( ERROR_VALIDATION_FAILED_EXT );
		STR( ERROR_INVALID_SHADER_NV );
		STR( ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT );
		STR( ERROR_NOT_PERMITTED_EXT );
		STR( ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT );
		STR( THREAD_IDLE_KHR );
		STR( THREAD_DONE_KHR );
		STR( OPERATION_DEFERRED_KHR );
		STR( OPERATION_NOT_DEFERRED_KHR );
		STR( PIPELINE_COMPILE_REQUIRED_EXT );
		STR( RESULT_MAX_ENUM );
#undef STR
	default: return "VK_UNKNOWN_INTERNAL_ERROR";
	}
}
inline VkResult VkResFromStatement( bool statement )
{
	return !statement ? VK_SUCCESS : VkResult( int( 0x8FFFFFFF ) );
}
// TODO: keep ?
#define VK_INTERNAL_ERROR( vk ) VkResFromStatement( bool( vk ) )

#define VK_CHECK( vk )																\
do{																					\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR"\nERR: ";				\
	VkResult res = vk;																\
	if( res ){																		\
		char dbgStr[256] = {};														\
		strcat_s( dbgStr, sizeof( dbgStr ), DEV_ERR_STR );							\
		strcat_s( dbgStr, sizeof( dbgStr ), std::data( VkResErrorString( res ) ) );	\
		SysErrMsgBox( dbgStr );														\
		abort();																	\
	}																				\
}while( 0 )		

template<typename VKH>
inline void VkDbgNameObj( VKH vkHandle, VkDevice vkDevice, const char* name )
{
	if constexpr( !objectNaming ) return;

	static_assert( sizeof( vkHandle ) == sizeof( u64 ) );
	VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	nameInfo.objectType = VkGetObjTypeFromHandle<VKH>();
	nameInfo.objectHandle = ( u64 ) vkHandle;
	nameInfo.pObjectName = name;

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
}

export module r_vk_resources;
export{
	// TODO: make this part of the device ?
// TODO: move to memory section
// TODO: pass VkPhysicalDevice differently
// TODO: multi gpu ?
// TODO: multi threaded
// TODO: better alloc strategy ?
// TODO: alloc vector for debug only ?
// TODO: check alloc num ?
// TODO: recycle memory ?
// TODO: redesign ?
// TODO: per mem-block flags
	struct vk_mem_view
	{
		VkDeviceMemory	device;
		void* host = 0;
	};

	struct vk_allocation
	{
		VkDeviceMemory  deviceMem;
		u8* hostVisible = 0;
		u64				dataOffset;
	};

	struct vk_mem_arena
	{
		std::vector<vk_mem_view>	mem;
		std::vector<vk_mem_view>	dedicatedAllocs;
		u64							maxParentHeapSize;
		u64							minVkAllocationSize = VK_MIN_DEVICE_BLOCK_SIZE;
		u64							size;
		u64							allocated;
		VkDevice					device;
		VkMemoryPropertyFlags		memTypeProperties;
		u32							memTypeIdx;
	};

	inline bool IsPowOf2( u64 addr )
	{
		return !( addr & ( addr - 1 ) );
	}
	inline u64 FwdAlign( u64 addr, u64 alignment )
	{
		assert( IsPowOf2( alignment ) );
		u64 mod = addr & ( alignment - 1 );
		return mod ? addr + ( alignment - mod ) : addr;
	}

	// TODO: integrate into rsc creation
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

	// TODO: move alloc flags ?
	// NOTE: NV driver bug not allow HOST_VISIBLE + VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT for uniforms
	inline VkDeviceMemory
		VkTryAllocDeviceMem(
			VkDevice								vkDevice,
			u64										size,
			u32										memTypeIdx,
			VkMemoryAllocateFlags					allocFlags,
			const VkMemoryDedicatedAllocateInfo* dedicated
		) {
		VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
		allocFlagsInfo.pNext = dedicated;
		allocFlagsInfo.flags = // allocFlags;
			//VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT |
			VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

		VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
#if 1
		memoryAllocateInfo.pNext = ( memTypeIdx == 0xA ) ? 0 : &allocFlagsInfo;
#else
		memoryAllocateInfo.pNext = &allocFlagsInfo;
#endif
		memoryAllocateInfo.allocationSize = size;
		memoryAllocateInfo.memoryTypeIndex = memTypeIdx;

		VkDeviceMemory mem;
		VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &mem ) );

		return mem;
	}

	inline vk_mem_arena
		VkMakeMemoryArena(
			const VkPhysicalDeviceMemoryProperties& memProps,
			VkMemoryPropertyFlags				memType,
			VkDevice							vkDevice
		) {
		i32 i = 0;
		for( ; i < memProps.memoryTypeCount; ++i )
		{
			if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;
		}

		VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

		VkMemoryHeap backingHeap = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ];

		vk_mem_arena vkArena = {};
		vkArena.allocated = 0;
		vkArena.size = 0;
		vkArena.memTypeIdx = i;
		vkArena.memTypeProperties = memProps.memoryTypes[ i ].propertyFlags;
		vkArena.maxParentHeapSize = backingHeap.size;
		vkArena.minVkAllocationSize = ( backingHeap.size < VK_MIN_DEVICE_BLOCK_SIZE ) ? ( 1 * MB ) : VK_MIN_DEVICE_BLOCK_SIZE;
		vkArena.device = vkDevice;

		return vkArena;
	}

	// TODO: offset the global, persistently mapped hostVisible pointer when sub-allocating
	// TODO: assert vs VK_CHECK vs default + warning
	// TODO: must alloc in block with BUFFER_ADDR
	inline vk_allocation
		VkArenaAlignAlloc(
			vk_mem_arena* vkArena,
			u64										size,
			u64										align,
			u32										memTypeIdx,
			VkMemoryAllocateFlags					allocFlags,
			const VkMemoryDedicatedAllocateInfo* dedicated
		) {
		assert( size <= vkArena->maxParentHeapSize );

		u64 allocatedWithOffset = FwdAlign( vkArena->allocated, align );
		vkArena->allocated = allocatedWithOffset;

		if( dedicated )
		{
			VkDeviceMemory deviceMem = VkTryAllocDeviceMem( vkArena->device, size, memTypeIdx, allocFlags, dedicated );
			void* hostVisible = 0;
			if( vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
			{
				VK_CHECK( vkMapMemory( vkArena->device, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
			}
			vk_mem_view lastDedicated = { deviceMem,hostVisible };
			vkArena->dedicatedAllocs.push_back( lastDedicated );
			return { lastDedicated.device, ( u8* ) lastDedicated.host, 0 };
		}

		if( ( vkArena->allocated + size ) > vkArena->size )
		{
			u64 newArenaSize = std::max( size, vkArena->minVkAllocationSize );
			VkDeviceMemory deviceMem = VkTryAllocDeviceMem( vkArena->device, newArenaSize, memTypeIdx, allocFlags, 0 );
			void* hostVisible = 0;
			if( vkArena->memTypeProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
			{
				VK_CHECK( vkMapMemory( vkArena->device, deviceMem, 0, VK_WHOLE_SIZE, 0, &hostVisible ) );
			}
			vkArena->mem.push_back( { deviceMem,hostVisible } );
			vkArena->size = newArenaSize;
			vkArena->allocated = 0;
		}

		assert( ( vkArena->allocated + size ) <= vkArena->size );
		assert( vkArena->allocated % align == 0 );

		vk_mem_view lastBlock = vkArena->mem[ std::size( vkArena->mem ) - 1 ];
		vk_allocation allocId = { lastBlock.device, ( u8* ) lastBlock.host, vkArena->allocated };
		vkArena->allocated += size;

		return allocId;
	}

	// TODO: revisit
	inline void
		VkArenaTerimate( vk_mem_arena* vkArena )
	{
		for( u64 i = 0; i < std::size( vkArena->mem ); ++i )
			vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
		for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
			vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
	}

	

	inline u64 VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
	{
		static_assert( std::is_same<VkDeviceAddress, u64>::value );

		VkBufferDeviceAddressInfo deviceAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		deviceAddrInfo.buffer = hndl;

		return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
	}

	// TODO: keep memory in buffer ?
	// TODO: keep devicePointer here ?
	struct vk_buffer
	{
		VkBuffer		hndl;
		VkDeviceMemory	mem;
		u64				size;
		u8* hostVisible;
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

	 vk_buffer
		VkCreateAllocBindBuffer(
			const buffer_info& buffInfo,
			VkDevice vkDevice,
			vk_mem_arena& vkArena,
			VkPhysicalDevice gpu
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
		vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
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

	vk_image
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
		imageInfo.extent = { img.width = imgInfo.width, img.height = imgInfo.height, 1 };
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
	 vk_buffer
		VkCreateAllocBindBuffer(
			u64					sizeInBytes,
			VkBufferUsageFlags	usage,
			vk_mem_arena& vkArena,
			VkPhysicalDevice gpu
		) {
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
		vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
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

	vk_image
		VkCreateAllocBindImage(
			const VkImageCreateInfo& imgInfo,
			vk_mem_arena& vkArena,
			VkPhysicalDevice			gpu
		) {
		VkFormatFeatureFlags formatFeatures = 0;
		if( imgInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
		if( imgInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if( imgInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
		if( imgInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties( gpu, imgInfo.format, &formatProps );
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
		vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
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

	 vk_image
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
	 vk_image
		VkCreateAllocBindImage(
			VkFormat			format,
			VkImageUsageFlags	usageFlags,
			VkExtent3D			extent,
			u32					mipCount,
			vk_mem_arena& vkArena,
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
		vkGetPhysicalDeviceMemoryProperties( gpu, &memProps );
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
		) {
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

	inline  VkBufferMemoryBarrier2KHR
		VkMakeBufferBarrier2(
			VkBuffer					hBuff,
			VkAccessFlags2KHR			srcAccess,
			VkPipelineStageFlags2KHR	srcStage,
			VkAccessFlags2KHR			dstAccess,
			VkPipelineStageFlags2KHR	dstStage,
			VkDeviceSize				buffOffset = 0,
			VkDeviceSize				buffSize = VK_WHOLE_SIZE,
			u32							srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
			u32							dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED
		) {
		VkBufferMemoryBarrier2KHR barrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2_KHR };
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

	inline  VkBufferMemoryBarrier2KHR VkReverseBufferBarrier2( const VkBufferMemoryBarrier2KHR& b )
	{
		VkBufferMemoryBarrier2KHR barrier = b;
		std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
		std::swap( barrier.srcStageMask, barrier.dstStageMask );

		return barrier;
	}

	inline  VkImageMemoryBarrier2KHR
		VkMakeImageBarrier2(
			VkImage						hImg,
			VkAccessFlags2KHR           srcAccessMask,
			VkPipelineStageFlags2KHR    srcStageMask,
			VkAccessFlags2KHR           dstAccessMask,
			VkPipelineStageFlags2KHR	dstStageMask,
			VkImageLayout               oldLayout,
			VkImageLayout               newLayout,
			VkImageAspectFlags			aspectMask,
			u32							baseMipLevel = 0,
			u32							mipCount = 0,
			u32							baseArrayLayer = 0,
			u32							layerCount = 0,
			u32							srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			u32							dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
		) {
		VkImageMemoryBarrier2KHR barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR };
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
}