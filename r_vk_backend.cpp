#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define __VK
#include "DEFS_WIN32_NO_BS.h"
// TODO: autogen custom vulkan ?
#include <vulkan.h>
#include <vulkan_win32.h>
// TODO: header + .cpp ?
// TODO: revisit this
#include "vk_procs.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string_view>
#include <charconv>

// TODO: use own allocator
// TODO: precompiled header
#include "diy_pch.h"
using namespace std;

// TODO: write own lib

// TODO: switch to MSVC ?

#ifdef __clang__
// NOTE: clang-cl on VS issue
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif

#include <DirectXCollision.h>

#include "sys_os_api.h"
#include "core_lib_api.h"

// TODO: gen from VkResult
__forceinline std::string_view VkResErrorString( VkResult errorCode )
{
	switch( errorCode )			{
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
		STR( ERROR_INCOMPATIBLE_VERSION_KHR );
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

__forceinline VkResult VkResFromStatemen( b32 statement )
{
	return !statement ? VK_SUCCESS : VkResult( int( 0x8FFFFFFF ) );
}

#define VK_INTERNAL_ERROR( vk ) VkResFromStatemen( b32( vk ) )

#define VK_CHECK( vk )																						\
do{																											\
	constexpr char VK_DEV_ERR_STR[] = ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__"\nERR: ";	\
	VkResult res = vk;																						\
	if( res ){																								\
		char dbgStr[256] = {};																				\
		strcat_s( dbgStr, sizeof( dbgStr ), VK_DEV_ERR_STR );												\
		strcat_s( dbgStr, sizeof( dbgStr ), VkResErrorString( res ).data() );					\
		SysErrMsgBox( dbgStr );																				\
		abort();																							\
	}																										\
}while( 0 )		

// TODO: full vk file generation ?
#define GET_VK_GLOBAL_PROC( VkProc ) VkProc = (PFN_##VkProc) vkGetInstanceProcAddr( 0, #VkProc )

//====================CONSTS====================//
constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 8 * MB;
constexpr u64 MAX_MIP_LEVELS = 12;
constexpr u32 NOT_USED_IDX = -1;
constexpr u64 OBJ_CULL_WORKSIZE = 64;
constexpr u64 MLET_CULL_WORKSIZE = 256;
//==============================================//
// TODO: cvars
//====================CVARS====================//
static b32 boundingSphereDbgDraw = 1;
static b32 colorBlending = 0;
static b32 occlusionCullingPass = 1;
//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//
constexpr b32 multiShaderDepthPyramid = 1;
constexpr b32 vkValidationLayerFeatures = 0;
constexpr b32 worldLeftHanded = 1;
constexpr b32 objectNaming = 1;
//==============================================//


// TODO: objType from objHandle ?
inline void VkDbgNameObj( VkDevice vkDevice, VkObjectType objType, u64 objHandle, const char* name )
{
#ifdef _VK_DEBUG_
	if constexpr( !objectNaming ) return;

	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = objType;
	nameInfo.objectHandle = objHandle;
	nameInfo.pObjectName = name;

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
#endif
}

struct swapchain
{
	VkSwapchainKHR	swapchain;
	VkImageView		imgViews[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkImage			imgs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkFormat		imgFormat;
	u32				imgWidth;
	u32				imgHeight;
	u8				imgCount;
};
// TODO: separate GPU and logical device ?
struct device
{
	VkPhysicalDeviceProperties gpuProps;
	VkPhysicalDevice gpu;
	VkDevice		device;
	VkQueue			gfxQueue;
	VkQueue			compQueue;
	VkQueue			transfQueue;
	u32				gfxQueueIdx;
	u32				compQueueIdx;
	u32				transfQueueIdx;
	u8				waveSize;
};
// TODO: use timeline Sema 
struct virtual_frame
{
	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuf;
	VkSemaphore		canGetImgSema;
	VkSemaphore		canPresentSema;
	VkFence			hostSyncFence;
};

// TODO: add more data ?
// TODO: VkDescriptorImageInfo 
struct image
{
	VkImage			img;
	VkImageView		view;
	VkDeviceMemory	mem;
	VkImageLayout	usageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkFormat		nativeFormat;
	VkExtent3D		nativeRes;
};

// TODO: remake
struct render_context
{
	VkPipeline		gfxPipeline;
	VkPipeline		compPipeline;
	VkPipeline		compLatePipeline;
	VkPipeline		compHiZPipeline;
	VkPipeline		gfxBVDbgDrawPipeline;
	VkPipeline		gfxTranspPipe;

	VkRenderPass	renderPass;
	VkRenderPass	render2ndPass;

	VkSampler		linearMinSampler;
	VkSampler		linearTextureSampler;
	image			depthTarget;
	image			colorTarget;
	image			depthPyramid;
	VkImageView		depthPyramidChain[ MAX_MIP_LEVELS ];
	u32				depthPyramidWidth;
	u32				depthPyramidHeight;
	u8				depthPyramidMipCount;
	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
	VkFramebuffer	offscreenFbo;
	
	virtual_frame	vrtFrames[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	u32				vFrameIdx = 0;
	u8				framesInFlight = VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
};

static VkInstance				vkInst = 0;
static VkSurfaceKHR				vkSurf = 0;
static VkDebugUtilsMessengerEXT	vkDbgMsg = 0;

extern HINSTANCE hInst;
extern HWND hWnd;

// TODO: keep global ?
static swapchain sc;
static device dc;
static render_context rndCtx;


__forceinline b32 IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
__forceinline static u64 FwdAlign( u64 addr, u64 alignment )
{
	assert( IsPowOf2( alignment ) );
	u64 mod = addr & ( alignment - 1 );
	return mod ? addr + ( alignment - mod ) : addr;
}


// TODO: keep memory in buffer ?
struct buffer_data
{
	VkBuffer		hndl = 0;
	VkDeviceMemory	mem = 0;
	u64				size = 0;
	u64				offset = 0;
	u8*				hostVisible = 0;
	u64				devicePointer = 0;

	__forceinline VkDescriptorBufferInfo descriptor() const
	{
		return VkDescriptorBufferInfo{ hndl,offset,size };
	}
};
// TODO: manage arenas
// TODO: better dedicated alloc ?
// TODO: better staging
// TODO: check alloc num ?
// TODO: recycle memory ?
// TODO: redesign ?
// TODO: who should check memType match ?
// TODO: polish min block size ?
struct vk_mem_view
{
	VkDeviceMemory	device;
	void*			host = 0;
};

struct vk_allocation
{
	VkDeviceMemory  deviceMem;
	u8*				hostVisible = 0;
	u64				dataOffset;
};

struct vk_mem_arena
{
	VkDevice							device;
	vector<vk_mem_view>					mem;
	vector<vk_mem_view>					dedicatedAllocs;
	u64									minArenaSize = VK_MIN_DEVICE_BLOCK_SIZE;
	u64									maxParentHeapSize;
	u64									size;
	u64									allocated;
	VkMemoryPropertyFlags				memType;
	VkPhysicalDeviceMemoryProperties	gpuMemProps;
};

static vk_mem_arena vkRscArena, vkStagingArena, vkAlbumArena, vkHostComArena, vkDbgArena;


inline i32
VkFindMemTypeIdx(
	const VkPhysicalDeviceMemoryProperties*  pVkMemProps,
	VkMemoryPropertyFlags				requiredProps,
	u32									memTypeBitsRequirement )
{
	for( u64 memIdx = 0; memIdx < pVkMemProps->memoryTypeCount; ++memIdx ){
		u32 memTypeBits = ( 1 << memIdx );
		b32 isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps->memoryTypes[ memIdx ].propertyFlags;
		b32 hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps )
			return (i32) memIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( "Memory type unmatch !" ) );

	return -1;
}

// TODO: move alloc flags ?
inline static vk_mem_view
VkTryAllocDeviceMem(
	VkDevice								vkDevice,
	u64										size,
	u32										memTypeIdx,
	VkMemoryPropertyFlags					memFlags,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated )
{
	VkMemoryAllocateFlagsInfo allocFlagsInfo = {};
	allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	allocFlagsInfo.pNext = dedicated;
	allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;// allocFlags;

	VkMemoryAllocateInfo memoryAllocateInfo = {};
	memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memoryAllocateInfo.pNext = &allocFlagsInfo;
	memoryAllocateInfo.allocationSize = size;
	memoryAllocateInfo.memoryTypeIndex = memTypeIdx;

	vk_mem_view vkMemView;
	VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &vkMemView.device ) );

	if( memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
		VK_CHECK( vkMapMemory( vkDevice, vkMemView.device, 0, VK_WHOLE_SIZE, 0, &vkMemView.host ) );

	return vkMemView;
}

inline static void
VkArenaInit(
	vk_mem_arena*						vkArena,
	const VkPhysicalDeviceMemoryProperties&	memProps,
	VkMemoryPropertyFlags				memType,
	VkDevice							vkDevice )
{
	i32 i = 0;
	for( ; i < memProps.memoryTypeCount; ++i )
		if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;

	VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

	vkArena->allocated = 0;
	vkArena->size = 0;
	vkArena->maxParentHeapSize = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ].size;
	vkArena->memType = memType;
	vkArena->device = vkDevice;
	vkArena->gpuMemProps = memProps;
	vkArena->minArenaSize =
		( vkArena->maxParentHeapSize < VK_MIN_DEVICE_BLOCK_SIZE ) ?
		( 1 * MB ) :
		VK_MIN_DEVICE_BLOCK_SIZE;
}

// TODO: assert vs VK_CHECK vs default + warning
// TODO: must alloc in block with BUFFER_ADDR
inline vk_allocation
VkArenaAlignAlloc( 
	vk_mem_arena*							vkArena, 
	u64										size, 
	u64										align, 
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated )
{
	u64 allocatedWithOffset = FwdAlign( vkArena->allocated, align );
	vkArena->allocated = allocatedWithOffset;

	assert( size <= vkArena->maxParentHeapSize );

	if( dedicated ){
		vkArena->dedicatedAllocs.push_back( 
			VkTryAllocDeviceMem( vkArena->device, size, memTypeIdx, vkArena->memType, allocFlags, dedicated ) );
		return { vkArena->dedicatedAllocs.back().device, (u8*) vkArena->dedicatedAllocs.back().host, 0 };
	}

	if( ( vkArena->allocated + size ) > vkArena->size ){
		u64 newArenaSize = max( size, vkArena->minArenaSize );
		vkArena->mem.push_back( 
			VkTryAllocDeviceMem( vkArena->device, newArenaSize, memTypeIdx, vkArena->memType, allocFlags, 0 ) );
		vkArena->size = newArenaSize;
		vkArena->allocated = 0;
	}

	assert( ( vkArena->allocated + size ) <= vkArena->size );
	assert( vkArena->allocated % align == 0 );

	vk_allocation allocId = { vkArena->mem.back().device, (u8*) vkArena->mem.back().host, vkArena->allocated };
	vkArena->allocated += size;

	return allocId;
}

inline void
VkArenaTerimate( vk_mem_arena* vkArena )
{
	for( u64 i = 0; i < vkArena->mem.size(); ++i )
		vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
	for( u64 i = 0; i < vkArena->dedicatedAllocs.size(); ++i )
		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
}

// TODO: multi gpu ? 
inline static void
VkInitMemory( VkPhysicalDevice vkPhysicalDevice, VkDevice vkDevice )
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkPhysicalDevice, &memProps );

	VkArenaInit( &vkRscArena,
				 memProps,
				 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				 vkDevice );
	VkArenaInit( &vkAlbumArena,
				 memProps,
				 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				 vkDevice );
	VkArenaInit( &vkStagingArena,
				 memProps,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				 vkDevice );
	VkArenaInit( &vkHostComArena,
				 memProps,
				 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
				 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
				 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				 vkDevice );

	VkArenaInit( &vkDbgArena,
				 memProps,
				 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
				 vkDevice );

}

__forceinline u64 VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
#ifdef _VK_DEBUG_
	static_assert( std::is_same<VkDeviceAddress, u64>::value );
#endif

	VkBufferDeviceAddressInfo deviceAddrInfo = {};
	deviceAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	deviceAddrInfo.buffer = hndl;

	return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
}

// TODO: pass device requirements/limits/shit ?
// TODO: return/use alignmed size ?
static buffer_data
VkCreateAllocBindBuffer(
	u64					sizeInBytes,
	VkBufferUsageFlags	usage,
	vk_mem_arena*		vkArena )
{
	buffer_data buffData;

	u64 offsetAlignmnet = 0;
	if( usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ){
		offsetAlignmnet = dc.gpuProps.limits.minStorageBufferOffsetAlignment;
		sizeInBytes = FwdAlign( sizeInBytes, offsetAlignmnet );
	}
	else if( usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ) offsetAlignmnet = dc.gpuProps.limits.minUniformBufferOffsetAlignment;

	

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( vkCreateBuffer( vkArena->device, &bufferInfo, 0, &buffData.hndl ) );


	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = {};
	memReqs2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	memReqs2.pNext = &dedicatedReqs;

	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = {};
	buffMemReqs2.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
	buffMemReqs2.buffer = buffData.hndl;
	vkGetBufferMemoryRequirements2( vkArena->device, &buffMemReqs2, &memReqs2 );

	i32 memTypeIdx = VkFindMemTypeIdx( &vkArena->gpuMemProps, vkArena->memType, memReqs2.memoryRequirements.memoryTypeBits );

	VkMemoryAllocateFlags allocFlags = 
		( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = {};
	dedicatedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicatedAllocateInfo.buffer = buffMemReqs2.buffer;

	b32 dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation; 

	vk_allocation bufferMem = VkArenaAlignAlloc( vkArena,
												 memReqs2.memoryRequirements.size,
												 memReqs2.memoryRequirements.alignment,
												 memTypeIdx,
												 allocFlags,
												 dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	buffData.mem = bufferMem.deviceMem;
	buffData.hostVisible = bufferMem.hostVisible;
	buffData.size = sizeInBytes;

	VK_CHECK( vkBindBufferMemory( vkArena->device, buffData.hndl, buffData.mem, bufferMem.dataOffset ) );

	if( allocFlags == VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
		buffData.devicePointer = VkGetBufferDeviceAddress( vkArena->device, buffData.hndl );

	return buffData;
}

// TODO: account for more formats/aspect masks ?
// TODO: pass aspect mask ? ?
inline static VkImageView
VkMakeImgView(
	VkDevice	vkDevice,
	VkImage		vkImg,
	VkFormat	imgFormat,
	u32			mipLevel,
	u32			levelCount,
	VkImageViewType imgViewType = VK_IMAGE_VIEW_TYPE_2D,
	u32			arrayLayer = 0,
	u32			layerCount = 1 )
{
	VkImageAspectFlags aspectMask =
		( imgFormat == VK_FORMAT_D32_SFLOAT ) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
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

// TODO: add more VkImageCreateInfo stuff
static image
VkCreateAllocBindImage(
	VkFormat			format,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			extent,
	u32					mipCount,
	vk_mem_arena*		vkArena,
	VkPhysicalDevice	gpu = dc.gpu )
{
	VkFormatFeatureFlags imgType = VK_FORMAT_FEATURE_FLAG_BITS_MAX_ENUM;
	switch( usageFlags ){
	case VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT: imgType = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	break;

	case VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT: imgType = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	break;

	case VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT: 
	imgType = VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	break;
	}

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( !( formatProps.optimalTilingFeatures & imgType ) ) );


	image img = {};

	VkImageCreateInfo imgInfo = {};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.format = img.nativeFormat = format;
	imgInfo.extent = img.nativeRes = extent;
	imgInfo.mipLevels = mipCount;
	imgInfo.arrayLayers = 1;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK( vkCreateImage( vkArena->device, &imgInfo, 0, &img.img ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = {};
	imgReqs2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
	imgReqs2.image = img.img;

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = {};
	memReqs2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
	memReqs2.pNext = &dedicatedReqs;
	vkGetImageMemoryRequirements2( vkArena->device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = {};
	dedicatedAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
	dedicatedAllocateInfo.image = imgReqs2.image;

	b32 dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

	i32 memTypeIdx = VkFindMemTypeIdx( &vkArena->gpuMemProps, vkArena->memType, memReqs2.memoryRequirements.memoryTypeBits );
	vk_allocation imgMem = VkArenaAlignAlloc( vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  memTypeIdx, 0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;
	
	VK_CHECK( vkBindImageMemory( vkArena->device, img.img, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType ){
		case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
		case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
		case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
		default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	}
	img.view = VkMakeImgView( vkArena->device, img.img, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

// TODO: tewak ? make own ?
// TODO: provide with own allocators
#define WIN32
#include "spirv_reflect.h"
#undef WIN32
// TODO: convention entry point = "main" ?
constexpr char SHADER_ENTRY_POINT[] = "main";

// TODO: cache shader ?
struct vk_shader
{
	VkShaderModule			module;
	vector<char>			spvByteCode;
	// TODO: use this ? or keep hardcoded in MakePipeline func
	VkShaderStageFlagBits	stage;
};

// TODO: where to place this ?
struct group_size
{
	u32 localSizeX;
	u32 localSizeY;
	u32 localSizeZ;
};

// TODO: vk_shader_program ?
// TODO: put shader module inside ?
struct vk_program
{
	VkPipelineBindPoint			bindPoint;
	VkPipelineLayout			pipeLayout;
	VkDescriptorSetLayout		descSetLayout;
	VkDescriptorUpdateTemplate	descUpdateTemplate;
	VkShaderStageFlags			pushConstStages;
	group_size					groupSize;
};

struct vk_descriptor_info
{
	union
	{
		VkDescriptorBufferInfo buff;
		VkDescriptorImageInfo img;
	};

	vk_descriptor_info() = default;
	vk_descriptor_info( VkBuffer buff, u64 offset, u64 range ) 
		: buff{ buff, offset, range }{}
	vk_descriptor_info( VkDescriptorBufferInfo buffInfo ) 
		: buff{ buffInfo }{}
	vk_descriptor_info( VkSampler sampler, VkImageView view, VkImageLayout imgLayout ) 
		: img{ sampler, view, imgLayout }{}
	vk_descriptor_info( VkDescriptorImageInfo imgInfo ) 
		: img{ imgInfo }{}
};

enum vk_global_descriptor_slot : u8
{
	VK_GLOBAL_SLOT_STORAGE_BUFFER = 0,
	VK_GLOBAL_SLOT_UNIFORM_BUFFER = 1,
	VK_GLOBAL_SLOT_SAMPLED_IMAGE = 2,
	VK_GLOBAL_SLOT_SAMPLER = 3,
	VK_GLOBAL_SLOT_COUNT = 4
};

constexpr VkDescriptorType globalDescTable[ VK_GLOBAL_SLOT_COUNT ] = {
	VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
	VK_DESCRIPTOR_TYPE_SAMPLER
};

constexpr u64 GLOBAL_DESC_SET = 1;

// TODO: add more ?
struct vk_descriptor_count
{
	u32 storageBuff = 512 * 1024;
	u32 uniformBuff = 90;
	u32 sampledImg = 512 * 1024;
	u32 samplers = 4 * 1024;
};

struct vk_global_descriptor
{
	VkDescriptorPool		pool;
	VkDescriptorSetLayout	setLayout;
	VkDescriptorSet			set;
};

static vk_global_descriptor globBindlessDesc;

// TODO: provide with desc array sizes
static vk_global_descriptor VkMakeBindlessGlobalDescriptor(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties&	deviceProps, 
	const vk_descriptor_count&			poolSizes = {} )
{
	VK_CHECK( VK_INTERNAL_ERROR( poolSizes.storageBuff > deviceProps.limits.maxDescriptorSetStorageBuffers ) );
	VK_CHECK( VK_INTERNAL_ERROR( poolSizes.uniformBuff > deviceProps.limits.maxDescriptorSetUniformBuffers ) );
	VK_CHECK( VK_INTERNAL_ERROR( poolSizes.sampledImg > deviceProps.limits.maxDescriptorSetSampledImages ) );
	VK_CHECK( VK_INTERNAL_ERROR( poolSizes.samplers > deviceProps.limits.maxDescriptorSetSamplers ) );
	
	VkDescriptorPoolSize sizes[] = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, poolSizes.storageBuff },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, poolSizes.uniformBuff },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, poolSizes.sampledImg },
		{ VK_DESCRIPTOR_TYPE_SAMPLER, poolSizes.samplers }
	};

	vk_global_descriptor desc = {};

	VkDescriptorPoolCreateInfo descPoolInfo = {};
	descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	descPoolInfo.maxSets = 1;
	descPoolInfo.poolSizeCount = std::size( sizes );
	descPoolInfo.pPoolSizes = sizes;
	VK_CHECK( vkCreateDescriptorPool( vkDevice, &descPoolInfo, 0, &desc.pool ) );


	VkDescriptorSetLayoutBinding bindlessLayout[ std::size( sizes ) ] = {};
	bindlessLayout[ 0 ].binding = VK_GLOBAL_SLOT_STORAGE_BUFFER;
	bindlessLayout[ 0 ].descriptorType = globalDescTable[ VK_GLOBAL_SLOT_STORAGE_BUFFER ];
	bindlessLayout[ 0 ].descriptorCount = 128;
	bindlessLayout[ 0 ].stageFlags = VK_SHADER_STAGE_ALL;

	bindlessLayout[ 1 ].binding = VK_GLOBAL_SLOT_UNIFORM_BUFFER;
	bindlessLayout[ 1 ].descriptorType = globalDescTable[ VK_GLOBAL_SLOT_UNIFORM_BUFFER ];
	bindlessLayout[ 1 ].descriptorCount = 8;
	bindlessLayout[ 1 ].stageFlags = VK_SHADER_STAGE_ALL;

	bindlessLayout[ 2 ].binding = VK_GLOBAL_SLOT_SAMPLED_IMAGE;
	bindlessLayout[ 2 ].descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];
	bindlessLayout[ 2 ].descriptorCount = 128;
	bindlessLayout[ 2 ].stageFlags = VK_SHADER_STAGE_ALL;

	bindlessLayout[ 3 ].binding = VK_GLOBAL_SLOT_SAMPLER;
	bindlessLayout[ 3 ].descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLER ];
	bindlessLayout[ 3 ].descriptorCount = 4;
	bindlessLayout[ 3 ].stageFlags = VK_SHADER_STAGE_ALL;

	VkDescriptorBindingFlags flags[ std::size( bindlessLayout ) ];
	for( VkDescriptorBindingFlags& f : flags )
		f = VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = {};
	descSetFalgs.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	descSetFalgs.bindingCount = std::size( flags );
	descSetFalgs.pBindingFlags = flags;
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = {};
	descSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutInfo.pNext = &descSetFalgs;
	descSetLayoutInfo.bindingCount = std::size( bindlessLayout );
	descSetLayoutInfo.pBindings = bindlessLayout;
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &desc.setLayout ) );


	VkDescriptorSetAllocateInfo descSetInfo = {};
	descSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descSetInfo.descriptorPool = desc.pool;
	descSetInfo.descriptorSetCount = 1;
	descSetInfo.pSetLayouts = &desc.setLayout;
	VK_CHECK( vkAllocateDescriptorSets( vkDevice, &descSetInfo, &desc.set ) );

	return desc;
}

template<typename T>
__forceinline VkWriteDescriptorSet VkMakeBindlessGlobalUpdate( 
	const T*					descInfo, 
	u64							descInfoCount,
	vk_global_descriptor_slot	bindingSlot,
	u64							dstAarryElem = 0,
	const vk_global_descriptor& desc = globBindlessDesc )
{
	VkWriteDescriptorSet update = {};
	update.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	update.dstSet = desc.set;
	update.dstBinding = bindingSlot;
	update.dstArrayElement = dstAarryElem;
	update.descriptorCount = descInfoCount;
	update.descriptorType = globalDescTable[ bindingSlot ];

	if constexpr( std::is_same<T, VkDescriptorBufferInfo>::value ){
		update.pBufferInfo = (const VkDescriptorBufferInfo*) descInfo;
	} else if constexpr( std::is_same<T, VkDescriptorImageInfo>::value ){
		update.pImageInfo = (const VkDescriptorImageInfo*) descInfo;
	}

	return update;
}

// TODO: 
constexpr char shaderPathPrefix[] = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\";
constexpr char shaderPathSuffix[] = ".spv";
// TODO: no std::vector
inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	FILE* fpSpvShader = 0;
	fopen_s( &fpSpvShader, shaderPath, "rb" );
	VK_CHECK( VK_INTERNAL_ERROR( !fpSpvShader ) );
	fseek( fpSpvShader, 0L, SEEK_END );
	const u32 size = ftell( fpSpvShader );
	rewind( fpSpvShader );
	vector<char> binSpvShader( size );
	fread( binSpvShader.data(), size, 1, fpSpvShader );
	fclose( fpSpvShader );

	VkShaderModuleCreateInfo shaderModuleInfo = {};
	shaderModuleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shaderModuleInfo.codeSize = size;
	shaderModuleInfo.pCode = (const u32*) binSpvShader.data();

	vk_shader shader = {};
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &shader.module ) );
	shader.spvByteCode = std::move( binSpvShader );
	
	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shaderPathPrefix ) - 1 );
	shaderName.remove_suffix( std::size( shaderPathSuffix ) - 1 );
	VkDbgNameObj( vkDevice, VK_OBJECT_TYPE_SHADER_MODULE, (u64) shader.module, &shaderName[ 0 ] );

	return shader;
}

// TODO: rewrite 
inline static void VkReflectShaderLayout(
	const VkPhysicalDeviceProperties&			gpuProps,
	const vk_shader*							s,
	vector<VkDescriptorSetLayoutBinding>&		descSetBindings,
	vector<VkPushConstantRange>&				pushConstRanges,
	group_size&									gs )
{
	SpvReflectShaderModule shaderReflection;
	VK_CHECK( (VkResult) spvReflectCreateShaderModule( s->spvByteCode.size() * sizeof( s->spvByteCode[ 0 ] ), s->spvByteCode.data(),
													   &shaderReflection ) );

	SpvReflectDescriptorSet& set = shaderReflection.descriptor_sets[ 0 ];

	for( u64 bindingIdx = 0; bindingIdx < set.binding_count; ++bindingIdx ){
		if( set.set > 0 ) continue;

		SpvReflectDescriptorBinding& descBinding = *set.bindings[ bindingIdx ];

		if( bindingIdx < std::size( descSetBindings ) ){
			// NOTE: if binding matches, assume the same resource will be used in multiple shaders in the same pipeline/program
			// TODO: should VK_CHECK here ?
			assert( descSetBindings[ bindingIdx ].descriptorType == VkDescriptorType( descBinding.descriptor_type ) );
			descSetBindings[ bindingIdx ].stageFlags |= shaderReflection.shader_stage;
			continue;
		}

		VkDescriptorSetLayoutBinding binding = {};
		binding.binding = descBinding.binding;
		binding.descriptorType = VkDescriptorType( descBinding.descriptor_type );
		binding.descriptorCount = descBinding.count;
		binding.stageFlags = shaderReflection.shader_stage;
		descSetBindings.push_back( binding );
	}

	// TODO: more push const blocks ?
	if( shaderReflection.push_constant_block_count ){
		VkPushConstantRange pushConstRange = {};
		pushConstRange.offset = shaderReflection.push_constant_blocks[ 0 ].offset;
		pushConstRange.size = shaderReflection.push_constant_blocks[ 0 ].size;
		pushConstRange.stageFlags = shaderReflection.shader_stage;
		VK_CHECK( VK_INTERNAL_ERROR( pushConstRange.size > gpuProps.limits.maxPushConstantsSize ) );

		pushConstRanges.push_back( pushConstRange );
	}

	if( VkShaderStageFlags( shaderReflection.shader_stage ) == VK_SHADER_STAGE_COMPUTE_BIT ){
		assert( !( gs.localSizeX && gs.localSizeY && gs.localSizeZ ) );
		gs = {	shaderReflection.entry_points[ 0 ].local_size.x,
				shaderReflection.entry_points[ 0 ].local_size.y,
				shaderReflection.entry_points[ 0 ].local_size.z };
	}
	spvReflectDestroyShaderModule( &shaderReflection );
}

// TODO: map spec consts ?
using vk_shader_list = std::initializer_list<const vk_shader*>;
using vk_specializations = std::initializer_list<u64>;


vk_program VkMakePipelineProgram(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties&	gpuProps,
	VkPipelineBindPoint					bindPoint,
	vk_shader_list						shaders,
	VkDescriptorSetLayout				bindlessLayout = globBindlessDesc.setLayout )
{
	assert( shaders.size() );
	
	vk_program program = {};

	vector<VkDescriptorSetLayoutBinding> bindings;
	vector<VkPushConstantRange>	pushConstRanges;
	group_size gs = {};
	
	for( const vk_shader* s : shaders ) VkReflectShaderLayout( gpuProps, s, bindings, pushConstRanges, gs );

	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = {};
	descSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	descSetLayoutInfo.bindingCount = std::size( bindings );
	descSetLayoutInfo.pBindings = &bindings[ 0 ];
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &program.descSetLayout ) );

	VkDescriptorSetLayout setLayouts[] = { program.descSetLayout, bindlessLayout };

	VkPipelineLayoutCreateInfo pipeLayoutInfo = {};
	pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeLayoutInfo.setLayoutCount = std::size( setLayouts );
	pipeLayoutInfo.pSetLayouts = setLayouts;
	pipeLayoutInfo.pushConstantRangeCount = pushConstRanges.size();
	pipeLayoutInfo.pPushConstantRanges = pushConstRanges.data();
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &program.pipeLayout ) );


	vector<VkDescriptorUpdateTemplateEntry> entries;
	entries.reserve( bindings.size() );
	entries.resize( 0 );
	for( const VkDescriptorSetLayoutBinding& binding : bindings ){
		VkDescriptorUpdateTemplateEntry entry = {};
		entry.dstBinding = binding.binding;
		entry.dstArrayElement = 0;
		entry.descriptorCount = binding.descriptorCount;
		entry.descriptorType = binding.descriptorType;
		entry.offset = std::size( entries ) * sizeof( vk_descriptor_info );
		entry.stride = sizeof( vk_descriptor_info );
		entries.emplace_back( entry );
	}

	VkDescriptorUpdateTemplateCreateInfo templateInfo = {};
	templateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
	templateInfo.descriptorUpdateEntryCount = std::size( entries );
	templateInfo.pDescriptorUpdateEntries = &entries[ 0 ];
	templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
	templateInfo.descriptorSetLayout = program.descSetLayout;
	templateInfo.pipelineBindPoint = program.bindPoint = bindPoint;
	templateInfo.pipelineLayout = program.pipeLayout;
	templateInfo.set = 0;
	VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &program.descUpdateTemplate ) );


	program.pushConstStages = pushConstRanges.size() ? pushConstRanges[ 0 ].stageFlags : 0;
	program.groupSize = gs;

	return program;
}

// TODO: 
inline void VkKillPipelineProgram( VkDevice vkDevice, vk_program* program )
{
	vkDestroyDescriptorUpdateTemplate( vkDevice, program->descUpdateTemplate, 0 );
	vkDestroyPipelineLayout( vkDevice, program->pipeLayout, 0 );
	vkDestroyDescriptorSetLayout( vkDevice, program->descSetLayout, 0 );

	*program = {};
}

// NOTE: std::vector with size==0 can't be plucked for data with &name[0], must use .data()
__forceinline static VkSpecializationInfo
VkMakeSpecializationInfo(
	vector<VkSpecializationMapEntry>& specializations,
	vk_specializations& consts )
{
	specializations.resize( consts.size() );
	for( u64 i = 0; i < consts.size(); ++i )
		specializations[ i ] = { u32( i ), u32( i * sizeof( *consts.begin() ) ), u32( sizeof( *consts.begin() ) ) };

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = std::size( specializations );
	specInfo.pMapEntries = specializations.data();
	specInfo.dataSize = std::size( consts ) * sizeof( *consts.begin() );
	specInfo.pData = consts.begin();

	return specInfo;
}

// TODO: specialization for gfx ?
// TODO: depth clamp ?
VkPipeline VkMakeGfxPipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkRenderPass		vkRndPass,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		vs,
	VkShaderModule		fs,
	VkPolygonMode		polyMode = VK_POLYGON_MODE_FILL,
	b32					blendCol = colorBlending,
	b32					depthWrite = true )
{
	VkPipelineShaderStageCreateInfo shaderStagesInfo[ 2 ] = {};
	shaderStagesInfo[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStagesInfo[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStagesInfo[ 0 ].module = vs;
	shaderStagesInfo[ 0 ].pName = SHADER_ENTRY_POINT;
	shaderStagesInfo[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStagesInfo[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStagesInfo[ 1 ].module = fs;
	shaderStagesInfo[ 1 ].pName = SHADER_ENTRY_POINT;


	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = {};
	inAsmStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inAsmStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inAsmStateInfo.primitiveRestartEnable = 0;

	VkPipelineViewportStateCreateInfo viewportInfo = {};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = {};
	dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateInfo.dynamicStateCount = POPULATION( dynamicStates );
	dynamicStateInfo.pDynamicStates = dynamicStates;

	VkPipelineRasterizationStateCreateInfo rasterInfo = {};
	rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterInfo.depthClampEnable = 0;
	rasterInfo.rasterizerDiscardEnable = 0;
	rasterInfo.polygonMode = polyMode;

	rasterInfo.cullMode = worldLeftHanded ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
	rasterInfo.frontFace = worldLeftHanded ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;

	rasterInfo.depthBiasEnable = 0;
	rasterInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = {};
	multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = depthWrite;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER;
	depthStencilState.depthBoundsTestEnable = VK_TRUE;
	depthStencilState.stencilTestEnable = 0;
	depthStencilState.minDepthBounds = 0;
	depthStencilState.maxDepthBounds = 1.0f;

	VkPipelineColorBlendAttachmentState blendConfig = {};
	blendConfig.blendEnable = blendCol;
	blendConfig.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendConfig.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	blendConfig.colorBlendOp = VK_BLEND_OP_ADD;
	blendConfig.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendConfig.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendConfig.alphaBlendOp = VK_BLEND_OP_ADD;
	blendConfig.colorWriteMask = 
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = {};
	colorBlendStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateInfo.logicOpEnable = 0;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &blendConfig;


	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = POPULATION( shaderStagesInfo );
	pipelineInfo.pStages = shaderStagesInfo;

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo =
	{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	pipelineInfo.pVertexInputState = &vtxInCreateInfo;
	pipelineInfo.pInputAssemblyState = &inAsmStateInfo;
	pipelineInfo.pTessellationState = 0;
	pipelineInfo.pViewportState = &viewportInfo;
	pipelineInfo.pRasterizationState = &rasterInfo;
	pipelineInfo.pMultisampleState = &multisamplingInfo;
	pipelineInfo.pDepthStencilState = &depthStencilState;
	pipelineInfo.pColorBlendState = &colorBlendStateInfo;
	pipelineInfo.pDynamicState = &dynamicStateInfo;
	pipelineInfo.layout = vkPipelineLayout;
	pipelineInfo.renderPass = vkRndPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = 0;
	pipelineInfo.basePipelineIndex = -1;

	VkPipeline vkGfxPipeline;
	VK_CHECK( vkCreateGraphicsPipelines( vkDevice, vkPipelineCache, 1, &pipelineInfo, 0, &vkGfxPipeline ) );

	return vkGfxPipeline;
}

VkPipeline VkMakeComputePipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		cs,
	vk_specializations	consts )
{
	vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageCreateInfo stage = {};
	stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cs;
	stage.pName = SHADER_ENTRY_POINT;
	stage.pSpecializationInfo = &specInfo;

	VkComputePipelineCreateInfo compPipelineInfo = {};
	compPipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	compPipelineInfo.stage = stage;
	compPipelineInfo.layout = vkPipelineLayout;

	VkPipeline pipeline = 0;
	VK_CHECK( vkCreateComputePipelines( vkDevice, vkPipelineCache, 1, &compPipelineInfo, 0, &pipeline ) );

	return pipeline;
}


// TODO: stretchy buffer ?
// TODO: remove std::stuff
#include <functional>
static vector<std::function<void()>> deviceGlobalDeletionQueue;

#define VK_APPEND_DESTROYER( VkObjectDestroyerLambda ) deviceGlobalDeletionQueue.push_back( [=](){ VkObjectDestroyer; } )


#ifdef _VK_DEBUG_

#include <iostream>

VKAPI_ATTR VkBool32 VKAPI_CALL 
VkDbgUtilsMsgCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT		msgSeverity,
	VkDebugUtilsMessageTypeFlagsEXT				msgType,
	const VkDebugUtilsMessengerCallbackDataEXT*	callbackData,
	void*										userData )
{
	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT ){
		std::string_view msgView = { callbackData->pMessage };
		std::cout << msgView.substr( msgView.rfind( "| " ) + 2 ) << "\n";

		return VK_FALSE;
	}

	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ) {					
		std::cout<< ">>> VK_WARNING <<<\n"; 
	} else if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ) {
		std::cout << ">>> VK_ERROR <<<\n";
	}
	std::cout << callbackData->pMessageIdName << "\n" << callbackData->pMessage << "\n" << "\n";

	return VK_FALSE;
}

#endif


inline static VkSurfaceKHR VkMakeSurfWin32( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR surfInfo = {};
	surfInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfInfo.hinstance = hInst;
	surfInfo.hwnd = hWnd;

	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;

#else
#error Must provide OS specific Surface
#endif // VK_USE_PLATFORM_WIN32_KHR
}

// TODO: use more queues
inline static void VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf, device* dc )
{
	// TODO: what about integrated devices ?
	constexpr VkPhysicalDeviceType PREFFERED_PHYSICAL_DEVICE_TYPE = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

	constexpr const char* ENABLED_DEVICE_EXTS[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,
		VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,

		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
		
		VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,

		//VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
#ifdef _VK_DEBUG_
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME
#endif
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, availableDevices.data() ) );

	VkPhysicalDevice gpu = 0;

	VkPhysicalDeviceSubgroupProperties subgroupProperties = {};
	subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;

	VkPhysicalDeviceVulkan12Properties gpuProps12 = {};
	gpuProps12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
	gpuProps12.pNext = &subgroupProperties;
	VkPhysicalDeviceProperties2 gpuProps2 = {};
	gpuProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	gpuProps2.pNext = &gpuProps12;

	// TODO: GPU specific features NV/AMD/whatever?
	VkPhysicalDevice16BitStorageFeatures _16BitStorageFeatures = {};
	_16BitStorageFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;

	VkPhysicalDeviceVulkan12Features gpuFeatures12 = {};
	gpuFeatures12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	gpuFeatures12.pNext = &_16BitStorageFeatures;
	VkPhysicalDeviceFeatures2 gpuFeatures = {};
	gpuFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	gpuFeatures.pNext = &gpuFeatures12;

	for( u64 i = 0; i < numDevices; ++i ){

		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, 0 ) || !extsNum ) continue;
		vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, availableExts.data() ) ) continue;

		for( u64 i = 0; i < POPULATION( ENABLED_DEVICE_EXTS ); ++i ){
			b32 foundExt = false;
			for( u64 j = 0; j < extsNum; ++j )
				if( !strcmp( ENABLED_DEVICE_EXTS[ i ], availableExts[ j ].extensionName ) ){
					foundExt = true;
					break;
				}
			if( !foundExt ) goto END_OF_LOOP;
		
		}

		vkGetPhysicalDeviceProperties2( availableDevices[ i ], &gpuProps2 );
		if( gpuProps2.properties.apiVersion < VK_API_VERSION_1_2 ) continue;
		if( gpuProps2.properties.deviceType != PREFFERED_PHYSICAL_DEVICE_TYPE ) continue;

		vkGetPhysicalDeviceFeatures2( availableDevices[ i ], &gpuFeatures );

		gpu = availableDevices[ i ];

		break;

	END_OF_LOOP:;
	}

	VK_CHECK( VK_INTERNAL_ERROR( !gpu ) );

	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, 0 );
	VK_CHECK( VK_INTERNAL_ERROR( !queueFamNum ) );
	vector<VkQueueFamilyProperties>  queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, queueFamProps.data() );

	// TODO: is this overkill, make it simple ?
	auto FindDesiredQueueFam = [&]( VkQueueFlags desiredFlags, VkBool32 present ) -> u32
	{
		u32 qIdx = 0;
		for( ; qIdx < queueFamNum; ++qIdx ){
			if( !queueFamProps[ qIdx ].queueCount ) continue;

			if( ( queueFamProps[ qIdx ].queueFlags - desiredFlags ) == 0 ){
				if( present ){
					vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qIdx, vkSurf, &present );
					VK_CHECK( VK_INTERNAL_ERROR( !present ) );
				}
				break;
			}
		}
		VK_CHECK( VK_INTERNAL_ERROR( qIdx == queueFamNum ) );

		return qIdx;
	};

	u32 qGfxIdx = FindDesiredQueueFam( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | 
									   VK_QUEUE_SPARSE_BINDING_BIT, 1 );
	u32 qCompIdx = FindDesiredQueueFam( VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT, 1 );
	u32 qTransfIdx = FindDesiredQueueFam( VK_QUEUE_TRANSFER_BIT | VK_QUEUE_SPARSE_BINDING_BIT, 0 );

	float queuePriorities = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = qGfxIdx;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &queuePriorities;

	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.pNext = &gpuFeatures;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = POPULATION( ENABLED_DEVICE_EXTS );
	deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS;
	VK_CHECK( vkCreateDevice( gpu, &deviceInfo, 0, &dc->device ) );

	// TODO: move to BackendInit ?
	VkLoadDeviceProcs( dc->device );

	vkGetDeviceQueue( dc->device, queueInfo.queueFamilyIndex, 0, &dc->gfxQueue );
	VK_CHECK( VK_INTERNAL_ERROR( !dc->gfxQueue ) );

	dc->gfxQueueIdx = queueInfo.queueFamilyIndex;
	dc->gpu = gpu;
	dc->gpuProps = gpuProps2.properties;
	dc->waveSize = subgroupProperties.subgroupSize;
}

// TODO: sep initial validation form sc creation when resize
inline static swapchain
VkMakeSwapchain( 
	VkDevice			vkDevice, 
	VkPhysicalDevice	vkPhysicalDevice, 
	VkSurfaceKHR		vkSurf,
	u32					queueFamIdx,
	VkFormat			scDesiredFormat = VK_FORMAT_B8G8R8A8_SRGB )
{
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vkPhysicalDevice, vkSurf, &surfaceCaps ) );

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
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, 0 ) );
		vector<VkSurfaceFormatKHR> formats( formatCount );
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, std::data( formats ) ) );

		for( u64 i = 0; i < formatCount; ++i )
			if( formats[ i ].format == scDesiredFormat )
			{
				scFormatAndColSpace.format = formats[ i ].format;
				scFormatAndColSpace.colorSpace = formats[ i ].colorSpace;
				break;
			}
		
		VK_CHECK( VK_INTERNAL_ERROR( !scFormatAndColSpace.format ) );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, 0 ) );
		vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, presentModes.data() ) );

		VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		for( u32 j = 0; j < numPresentModes; ++j )
			if( presentModes[ j ] == desiredPresentMode ){
				presentMode = desiredPresentMode;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !presentMode ) );
	}


	u32 scImgCount = VK_SWAPCHAIN_MAX_IMG_ALLOWED;
	assert( ( scImgCount > surfaceCaps.minImageCount ) && ( scImgCount < surfaceCaps.maxImageCount ) );
	assert( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) && 
			( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkImageUsageFlags scImgUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	assert( surfaceCaps.supportedUsageFlags & scImgUsage );

	swapchain sc = {};

	VkSwapchainCreateInfoKHR scInfo = {};
	scInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	scInfo.surface = vkSurf;
	scInfo.minImageCount = sc.imgCount = scImgCount;
	scInfo.imageFormat = sc.imgFormat = scFormatAndColSpace.format;
	scInfo.imageColorSpace = scFormatAndColSpace.colorSpace;
	scInfo.imageExtent = surfaceCaps.currentExtent;
	scInfo.imageArrayLayers = surfaceCaps.maxImageArrayLayers;
	scInfo.imageUsage = scImgUsage;
	scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	scInfo.preTransform = surfaceCaps.currentTransform;
	scInfo.compositeAlpha = surfaceComposite;
	scInfo.presentMode = presentMode;
	scInfo.queueFamilyIndexCount = 1;
	scInfo.pQueueFamilyIndices = &queueFamIdx;
	scInfo.clipped = VK_TRUE; //TODO: change according to perf/user experience
	scInfo.oldSwapchain = 0;
	VK_CHECK( vkCreateSwapchainKHR( vkDevice, &scInfo, 0, &sc.swapchain ) );

	// TODO: sc resize
	sc.imgWidth = scInfo.imageExtent.width;
	sc.imgHeight = scInfo.imageExtent.height;

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, 0 ) );
	VK_CHECK( VK_INTERNAL_ERROR( !( scImgsNum == scInfo.minImageCount ) ) );
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, sc.imgs ) );


	// TODO: ImgView only for MSAA 

	//VkImageViewCreateInfo imgViewInfo = {};
	//imgViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	//imgViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	//imgViewInfo.format = scInfo.imageFormat;
	//imgViewInfo.components = { 
	//	VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A            
	//};
	//imgViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	//for( u64 i = 0; i < scImgNum; ++i ){
	//	imgViewInfo.image = scImgs[ i ];
	//	VK_CHECK( vkCreateImageView( vkDevice, &imgViewInfo, 0, &sc->imgViews[ i ] ) );
	//}

	return sc;
}


// TODO: make general ?
inline static VkRenderPass 
VkMakeRndPass( 
	VkDevice	vkDevice, 
	VkFormat	colorFormat, 
	VkFormat	depthFormat, 
	b32			secondPass )
{
	VkAttachmentReference colorAttachement = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depthAttachement = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescr = {};
	subpassDescr.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescr.colorAttachmentCount = 1;
	subpassDescr.pColorAttachments = &colorAttachement;
	subpassDescr.pDepthStencilAttachment = &depthAttachement;

	VkAttachmentDescription attachmentDescriptions[] = {
		{
			0,                                 
			colorFormat,
			VK_SAMPLE_COUNT_1_BIT,             
			secondPass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
			VK_ATTACHMENT_STORE_OP_STORE,      
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,   
			VK_ATTACHMENT_STORE_OP_DONT_CARE,  
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
		},
		{
			0,                                  
			depthFormat,
			VK_SAMPLE_COUNT_1_BIT,              
			secondPass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
			secondPass ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VK_ATTACHMENT_STORE_OP_DONT_CARE,   
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
		}
	};

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = POPULATION( attachmentDescriptions );
	renderPassInfo.pAttachments = attachmentDescriptions;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescr;

	VkRenderPass rndPass;
	VK_CHECK( vkCreateRenderPass( vkDevice, &renderPassInfo, 0, &rndPass ) );
	return rndPass;
}

inline static VkFramebuffer
VkMakeFramebuffer(
	VkDevice		vkDevice,
	VkRenderPass	vkRndPass,
	VkImageView*	attachements,
	u32				attachementCount,
	u32				width,
	u32				height )
{
	VkFramebufferCreateInfo fboInfo = {};
	fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fboInfo.renderPass = vkRndPass;
	fboInfo.attachmentCount = attachementCount;
	fboInfo.pAttachments = attachements;
	fboInfo.width = width;
	fboInfo.height = height;
	fboInfo.layers = 1;

	VkFramebuffer fbo;
	VK_CHECK( vkCreateFramebuffer( vkDevice, &fboInfo, 0, &fbo ) );
	return fbo;
}


static void VkInitVirutalFrames( 
	VkDevice		vkDevice, 
	u32				boundQueueIdx, 
	virtual_frame*	vrtFrames,
	u64				framesInFlight )
{
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmdPoolInfo.queueFamilyIndex = boundQueueIdx;
	for( u64 i = 0; i < framesInFlight; ++i )
		VK_CHECK( vkCreateCommandPool( vkDevice, &cmdPoolInfo, 0, &vrtFrames[ i ].cmdPool ) );

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = {};
	cmdBuffAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdBuffAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdBuffAllocInfo.commandBufferCount = 1;
	for( u64 i = 0; i < framesInFlight; ++i ){
		cmdBuffAllocInfo.commandPool = vrtFrames[ i ].cmdPool;
		VK_CHECK( vkAllocateCommandBuffers( vkDevice, &cmdBuffAllocInfo, &vrtFrames[ i ].cmdBuf ) );
	}

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	for( u64 i = 0; i < framesInFlight; ++i ){
		VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrames[ i ].canGetImgSema ) );
		VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrames[ i ].canPresentSema ) );
	}

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;	
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for( u64 i = 0; i < framesInFlight; ++i )
		VK_CHECK( vkCreateFence( vkDevice, &fenceInfo, 0, &vrtFrames[ i ].hostSyncFence ) );

	//VkSemaphoreTypeCreateInfo timelineInfo = {};
	//timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	//timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	//timelineInfo.initialValue = 0;
	//VkSemaphoreCreateInfo timelineSemaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	//timelineSemaInfo.pNext = &timelineInfo;
	//VK_CHECK( vkCreateSemaphore( vkDevice, &timelineSemaInfo, 0, hostSyncTimeline ) );
}


// TODO: more locallity ?
#include "r_data_structs.h"

// TODO: pq file
#define FAST_OBJ_IMPLEMENTATION
#include "fast_obj.h"

//#define STBI_MALLOC()
//#define STBI_REALLOC()
//#define STBI_FREE()
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <meshoptimizer.h>

using namespace DirectX;

// TODO: use DirectXMath ?
// TODO: fast ?
inline i16 Snorm8OctahedronEncode( vec3 n, u64 precision = 8 )
{
	float invAbsNorm = 1.0f / ( std::abs( n.x ) + std::abs( n.y ) + std::abs( n.z ) );
	n.x *= invAbsNorm;
	n.y *= invAbsNorm;
	n.z *= invAbsNorm;

	if( n.z < 0 ){
		n.x = std::copysign( 1.0f - std::abs( n.y ), n.x );
		n.y = std::copysign( 1.0f - std::abs( n.x ), n.y );
	}

	n.x = n.x * 0.5f + 0.5f;
	n.y = n.y * 0.5f + 0.5f;
	
#ifdef FAST
	XMVECTOR _n128 = XMLoadFloat3( &n );
	float invAbsNorm = 1.0f / XMVectorGetX( XMVector3Dot( XMVectorAbs( _n128 ), { 1,1,1,0 } ) );
	_n128 = XMVectorScale( _n128, invAbsNorm );

	if( XMVectorGetZ( _n128 ) < 0 ){

	}
#endif // FAST

	i16 res = 
		( meshopt_quantizeSnorm( n.y, precision ) << precision ) | 
		meshopt_quantizeSnorm( n.x, precision );
	return res;
}

inline float EncodeTanToAngle( vec3 n, vec3 t )
{
	// NOTE: inspired by Doom Eternal
	vec3 tanRef = ( std::abs( n.x ) > std::abs( n.z ) ) ?
		vec3{ -n.y, n.x, 0.0f } :
		vec3{ 0.0f, -n.z, n.y };

	// TODO: use angle between normals ?
	float tanRefAngle = XMVectorGetX( XMVector3AngleBetweenVectors( XMLoadFloat3( &t ), 
																	XMLoadFloat3( &tanRef ) ) );
	return XMScalarModAngle( tanRefAngle ) * XM_1DIVPI;
}

// TODO: compressed coords u8, u16
struct vertex
{
	float px, py, pz;
	float nx, ny, nz;// i16 n;
	float tnx, tny, tnz;
	//float tangentAngle;
	float tu, tv;
	u32 mi;
};


// TODO: textures don't load with abs path "\\" ?
constexpr const char* MODEL_FILES[] = {
	"D:/3d models/cyberdemon/0.obj",
	//"D:/3d models/sibenik/sibenik.obj",
	//"D:/3d models/doom-hell-knight-/source/doomxxtt.obj",
	//"D:/3d models/doom-eternal-slayer/doom eternal slayer.obj",
	//"D:/3d models/doom-cacodemon/source/Cacodemon/cacodemon.obj"
};

struct meshlets_data
{
	std::vector<meshlet> meshlets;
	std::vector<u32> vtxIndirBuf;
	std::vector<u8> triangleBuf;
};

// TODO: better way to handle materials ?
// TODO: compress
struct vertex_attributes
{
	vector<vec3> positions;
	vector<vec4> normals;
	vector<vec3> uvms;
	u64 count = 0;

	vertex_attributes() = default;
	vertex_attributes( u64 size ) 
		: positions( size ), normals( size ), uvms( size ){}
};

inline void DeinterleaveVertexBuffer( const vector<vertex>& vtx, vertex_attributes& verts )
{
	verts.count += std::size( vtx );
	verts.positions.reserve( verts.count );
	verts.normals.reserve( verts.count );
	verts.uvms.reserve( verts.count );

	for( const vertex& v : vtx ){
		verts.positions.push_back( { v.px, v.py, v.pz } );

		float tanAngle = EncodeTanToAngle( { v.nx,v.ny,v.nz }, { v.tnx,v.tny,v.tnz } );

		verts.normals.push_back( { v.nx,v.ny,v.nz, tanAngle } );
		verts.uvms.push_back( vec3( v.tu, v.tv, v.mi ) );
	}
}

// TODO: rename
// TODO: enum type ?
struct raw_image_info
{
	constexpr static i32 desiredChannels = STBI_rgb_alpha;

	i32 width;
	i32 height;
	i32 texChannels;
	u64 sizeInBytes;
	u64 bufferOffset;
	VkFormat format;
};

// TODO: use own mem api
// TODO: write data inplace in a mega-buffer
// TODO: assert ?
inline static raw_image_info StbLoadImageFromFileToBuffer( FILE* imgFile, std::vector<u8>& bin )
{
	raw_image_info img = {};

	u8* data = stbi_load_from_file( imgFile, &img.width, &img.height, &img.texChannels, img.desiredChannels );
	assert( data );
	img.sizeInBytes = u64( img.width * img.height * img.texChannels );
	img.bufferOffset = std::size( bin );

	bin.resize( img.bufferOffset + img.sizeInBytes );
	std::memcpy( &bin[ 0 ] + img.bufferOffset, data, img.sizeInBytes );

	stbi_image_free( data );
	return img;
}

inline static raw_image_info StbLoadImageFromMem( u8 const* imgData, u32 imgSize, std::vector<u8>& bin )
{
	raw_image_info img = {};

	u8* data = stbi_load_from_memory( imgData, imgSize, &img.width, &img.height, &img.texChannels, img.desiredChannels );
	assert( data );
	img.sizeInBytes = u64( img.width * img.height * img.texChannels );
	img.bufferOffset = std::size( bin );

	bin.resize( img.bufferOffset + img.sizeInBytes );
	std::memcpy( std::data( bin ) + img.bufferOffset, data, img.sizeInBytes );

	stbi_image_free( data );
	return img;
}

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

__forceinline DirectX::XMMATRIX CgltfNodeGetTransf( const cgltf_node* node )
{
	XMMATRIX t = {};
	if( node->has_rotation || node->has_translation || node->has_scale )
	{
		XMVECTOR move = XMLoadFloat3( (const XMFLOAT3*) node->translation );
		XMVECTOR rot = XMLoadFloat4( (const XMFLOAT4*) node->rotation );
		XMVECTOR scale = XMLoadFloat3( (const XMFLOAT3*) node->scale );
		t = XMMatrixAffineTransformation( scale, XMVectorSet( 0, 0, 0, 1 ), rot, move );
	}
	else if( node->has_matrix )
	{
		// NOTE: gltf matrices are stored in col maj
		t = XMMatrixTranspose( XMLoadFloat4x4( (const XMFLOAT4X4*) node->matrix ) );
	}

	return t;
}

// TODO: improve
enum gltf_sampler_filter : u16
{
	GLTF_SAMPLER_FILTER_NEAREST = 9728,
	GLTF_SAMPLER_FILTER_LINEAR = 9729,
	GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST = 9984,
	GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST = 9985,
	GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR = 9986,
	GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR = 9987
};

enum gltf_sampler_address_mode : u16
{
	GLTF_SAMPLER_ADDRESS_MODE_REPEAT = 10497,
	GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 33071,
	GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 33648
};

// TODO: default types ?
inline VkFilter VkGetFilterTypeFromGltf( cgltf_int f )
{
	switch( f )
	{
	case GLTF_SAMPLER_FILTER_NEAREST:
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	return VK_FILTER_NEAREST;

	case GLTF_SAMPLER_FILTER_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	default: 
	return VK_FILTER_LINEAR;
	}
}
inline VkSamplerMipmapMode VkGetMipmapTypeFromGltf( cgltf_int m )
{
	switch( m )
	{
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	return VK_SAMPLER_MIPMAP_MODE_LINEAR;

	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	default:
	return VK_SAMPLER_MIPMAP_MODE_NEAREST;
	}
}
inline VkSamplerAddressMode VkGetAddressModeFromGltf( cgltf_int a )
{
	switch( a )
	{
	case GLTF_SAMPLER_ADDRESS_MODE_REPEAT: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
	default: 
	return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	}
}

struct vk_sampler_config
{
	VkFilter				min; // 2 bits
	VkFilter				mag; // 2 bits
	VkSamplerMipmapMode		mipMode; // 1 bit
	VkSamplerAddressMode	addrU; // 3 bits
	VkSamplerAddressMode	addrV; // 3 bits
	//VkSamplerAddressMode	addrW;

	vk_sampler_config() = default;
	vk_sampler_config( cgltf_int minF, cgltf_int magF,
					   cgltf_int u, cgltf_int v )
		:
		min{ VkGetFilterTypeFromGltf( minF ) },
		mag{ VkGetFilterTypeFromGltf( magF ) },
		mipMode{ VkGetMipmapTypeFromGltf( std::max( minF,magF ) ) },
		addrU{ VkGetAddressModeFromGltf( u ) },
		addrV{ VkGetAddressModeFromGltf( v ) }
	{}
};
// TODO: use u16 idx
// TODO: pass samplers
// TODO: better file api
// TODO: use own mem
// TODO: deinterleave from the start ?
// TODO: quantize data
// TODO: sampler
static void
LoadGlbModel(
	const char*				path,

	vector<vertex>&			vertices,
	vector<u32>&			indices,
	vector<u8>&				textureData,
	vector<raw_image_info>&	album,
	vector<material_data>&	materials,
	DirectX::BoundingBox&	box )
{
	FILE* fpGlbData = 0;
	VK_CHECK( VK_INTERNAL_ERROR( fopen_s( &fpGlbData, path, "rb" ) ) );
	fseek( fpGlbData, 0L, SEEK_END );
	u32 size = ftell( fpGlbData );
	rewind( fpGlbData );

	vector<u8> glbData( size );
	fread( std::data( glbData ), std::size( glbData ), 1, fpGlbData );
	fclose( fpGlbData );


	cgltf_options options = { .type = cgltf_file_type_glb };
	cgltf_data* data = 0;
	VK_CHECK( VK_INTERNAL_ERROR( cgltf_parse( &options, &glbData[ 0 ], std::size( glbData ), &data ) ) );
	VK_CHECK( VK_INTERNAL_ERROR( cgltf_validate( data ) ) );

	const u8* pBin = (const u8*) data->bin;

	std::vector<DirectX::XMFLOAT4X4> nodeTransf( data->nodes_count );
	for( u64 n = 0; n < data->nodes_count; ++n )
	{
		const cgltf_node* node = data->nodes + n;

		XMMATRIX t = CgltfNodeGetTransf( node );

		for( const cgltf_node* parent = node->parent; 
			 parent; 
			 parent = parent->parent )
		{
			t = XMMatrixMultiply( t, CgltfNodeGetTransf( parent ) );
		}
		XMStoreFloat4x4( &nodeTransf[ n ], t );
	}

	std::vector<DirectX::BoundingBox> aabbs( data->nodes_count );
	for( u64 n = 0; n < data->nodes_count; ++n )
	{
		const cgltf_mesh& mesh = *data->nodes[ n ].mesh;

		// TODO: rewrite this loop
		for( u64 p = 0; p < mesh.primitives_count; ++p )
		{
			assert( mesh.primitives_count == 1 );

			const cgltf_primitive& prim = mesh.primitives[ p ];

			// TODO: after prim.material ?
			u64 materialsOffset = std::size( materials );
			if( prim.material )
			{
				materials.push_back( {} );
				material_data& mtl = materials[ materialsOffset ];
				// TODO: check mimeType ?
				vk_sampler_config samplerConfig = {};
				const cgltf_texture* pbrBaseColor = prim.material->pbr_metallic_roughness.base_color_texture.texture;
				if( pbrBaseColor )
				{
					u64 imgOffset = pbrBaseColor->image->buffer_view->offset;
					u64 imgSize = pbrBaseColor->image->buffer_view->size;
					raw_image_info imgInfo = StbLoadImageFromMem( pBin + imgOffset, imgSize, textureData );
					imgInfo.format = VK_FORMAT_B8G8R8A8_SRGB;
					album.emplace_back( imgInfo );
					mtl.diffuseIdx = std::size( album ) - 1;

					//samplerConfig = {
					//	pbrMetallicRoughnessMap->sampler->min_filter,
					//		pbrMetallicRoughnessMap->sampler->mag_filter,
					//		pbrMetallicRoughnessMap->sampler->wrap_s,
					//		pbrMetallicRoughnessMap->sampler->wrap_t
					//};
				}

				const cgltf_texture* normalMap = prim.material->normal_texture.texture;
				if( normalMap )
				{
					u64 imgOffset = normalMap->image->buffer_view->offset;
					u64 imgSize = normalMap->image->buffer_view->size;
					raw_image_info imgInfo = StbLoadImageFromMem( pBin + imgOffset, imgSize, textureData );
					imgInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
					album.emplace_back( imgInfo );
					mtl.bumpIdx = std::size( album ) - 1;

					//samplerConfig = {
					//	pbrMetallicRoughnessMap->sampler->min_filter,
					//		pbrMetallicRoughnessMap->sampler->mag_filter,
					//		pbrMetallicRoughnessMap->sampler->wrap_s,
					//		pbrMetallicRoughnessMap->sampler->wrap_t
					//};
				}
			}


			u64 vtxDstOffset = std::size( vertices );
			u64 vtxLocalCount = prim.attributes[ 0 ].data->count;
			vertices.resize( vtxDstOffset + vtxLocalCount );
			for( u64 a = 0; a < prim.attributes_count; ++a )
			{
				const cgltf_attribute& vtxAttr = prim.attributes[ a ];
				assert( vtxLocalCount == vtxAttr.data->count );

				u64 attrOffset = vtxAttr.data->offset;
				u64 vtxSrcOffset = vtxAttr.data->buffer_view->offset;
				u64 vtxAttrStride = vtxAttr.data->stride;


				u64 compSize = cgltf_component_size( vtxAttr.data->component_type );
				b32 alignmentReq = !( attrOffset % compSize ) && !( ( attrOffset + vtxSrcOffset ) % compSize );
				assert( alignmentReq );

				// TODO: not all vtx data is float
				u64 compNum = cgltf_num_components( vtxAttr.data->type );

				if( ( vtxAttr.type == cgltf_attribute_type_position ) && vtxAttr.data->has_min && vtxAttr.data->has_min )
				{
					XMVECTOR min = XMLoadFloat3( (const vec3*) vtxAttr.data->min );
					XMVECTOR max = XMLoadFloat3( (const vec3*) vtxAttr.data->max );

					XMStoreFloat3( &aabbs[ n ].Center, XMVectorScale( XMVectorAdd( max, min ), 0.5f ) );
					XMStoreFloat3( &aabbs[ n ].Extents, XMVectorScale( XMVectorSubtract( max, min ), 0.5f ) );
				}
				else if( vtxAttr.type == cgltf_attribute_type_position )
				{
					aabbs.resize( std::size( aabbs ) - 1 );
				}

				for( u64 v = 0; v < vtxLocalCount; ++v )
				{
					vertex& vtx = vertices[ vtxDstOffset + v ];

					const u8* attrData = pBin + vtxSrcOffset + attrOffset + vtxAttrStride * v;
					// TODO: format checking ?
					switch( vtxAttr.type )
					{
					case cgltf_attribute_type_position:
					{
						vec3 pos = *(const vec3*) attrData;
						vtx.px = -pos.x;
						vtx.py = pos.y;
						vtx.pz = pos.z;
					} break;
					case cgltf_attribute_type_normal:
					{
						vec3 normal = *(const vec3*) attrData;
						vtx.nx = normal.x;
						vtx.ny = normal.y;
						vtx.nz = normal.z;
					} break;
					case cgltf_attribute_type_tangent:
					{
						vec4 tan = *(const vec4*) attrData;
						float handedness = tan.w;
						vtx.tnx = tan.x * handedness;
						vtx.tny = tan.y * handedness;
						vtx.tnz = tan.z * handedness;
					} break;
					case cgltf_attribute_type_texcoord:
					{
						vec2 uv = *(const vec2*) attrData;
						vtx.tu = uv.x;
						vtx.tv = uv.y;
						vtx.mi = materialsOffset;
					} break;

					case cgltf_attribute_type_color:
					case cgltf_attribute_type_joints:
					case cgltf_attribute_type_weights:
					case cgltf_attribute_type_invalid: break;
					}
				}
			}


			u64 idxDstOffset = std::size( indices );
			indices.resize( idxDstOffset + prim.indices->count );
			u64 idxSrcOffset = prim.indices->buffer_view->offset;
			u64 idxStride = prim.indices->stride;
			for( u64 i = 0; i < prim.indices->count; ++i )
			{
				// TODO: not all indices are u32/u16
				u64 idx = cgltf_component_read_index( pBin + idxSrcOffset + idxStride * i, prim.indices->component_type );
				indices[ i + idxDstOffset ] = u32( idx + vtxDstOffset );
			}
		}
	}

	// TODO: transform this
	// TODO: worldLeftHanded

	// TODO: more elegant soln
	DirectX::BoundingBox aabb = aabbs[ 0 ];
	for( u64 b = 1; b < std::size( aabbs ); ++b )
	{
		BoundingBox::CreateMerged( aabb, aabb, aabbs[ b ] );
	}
		
	if( std::size( aabbs ) == 0 )
	{
		DirectX::BoundingBox::CreateFromPoints( aabb,
												std::size( vertices ),
												(const DirectX::XMFLOAT3*) &vertices[ 0 ],
												sizeof( vertices[ 0 ] ) );
	}
	box = aabb;

	cgltf_free( data );
}

// NOTE: don't use
// TODO: compression/quantization
// TODO: assert ?
// TODO: output aabb
b32 LoadObjModel( 
	const char*				path, 
	vector<vertex>&			vertices, 
	vector<u8>&				textureData, 
	vector<raw_image_info>&		album, 
	vector<material_data>&	materials )
{
	fastObjMesh* obj = fast_obj_read( path );
	if( !obj ) return false;

	u64 idxCount = 0;
	for( u64 i = 0; i < obj->face_count; ++i ) idxCount += 3 * ( obj->face_vertices[ i ] - 2 );

	vertices.resize( idxCount );

	u64 vtxOffset = 0;
	u64 idxOffset = 0;
	for( u64 i = 0; i < obj->face_count; ++i ){
		u32 faceMaterialIdx = obj->face_materials[ i ];
		assert( faceMaterialIdx < obj->material_count );

		u64 triangleOffset = vtxOffset;
		for( u64 j = 0; j < obj->face_vertices[ i ]; ++j ){
			fastObjIndex gi = obj->indices[ idxOffset + j ];

			// NOTE: triangulate polygon on the fly; offset-3 is always the first polygon vertex
			if( j >= 3 ){
				vertices[ vtxOffset + 0 ] = vertices[ vtxOffset - 3 ];
				vertices[ vtxOffset + 1 ] = vertices[ vtxOffset - 1 ];
				vtxOffset += 2;
			}

			vertex& v = vertices[ vtxOffset++ ];
			v.px = obj->positions[ gi.p * 3 + 0 ];
			v.py = obj->positions[ gi.p * 3 + 1 ];
			v.pz = obj->positions[ gi.p * 3 + 2 ];
			v.pz *= worldLeftHanded ? -1.0f : 1.0f;

			// NOTE: per-vertex normals
			v.nx = obj->normals[ gi.n * 3 + 0 ];
			v.ny = obj->normals[ gi.n * 3 + 1 ];
			v.nz = obj->normals[ gi.n * 3 + 2 ];

			v.tu = obj->texcoords[ gi.t * 2 + 0 ];
			v.tv = obj->texcoords[ gi.t * 2 + 1 ];

			v.mi = faceMaterialIdx;
		}

		idxOffset += obj->face_vertices[ i ];
	}

	assert( vtxOffset == idxCount );


	vector<material_data> mtlData( obj->material_count );
	std::memset( (void*) &mtlData[ 0 ], 0, std::size( mtlData ) );
	vector<u8> imageBin;

	FILE* imgFile = 0;
	for( u64 i = 0; i < obj->material_count; ++i ){
		fastObjMaterial& mtl = obj->materials[ i ];
		mtlData[ i ].diffuseK = *(const DirectX::XMFLOAT3*) mtl.Kd;
		mtlData[ i ].shininess = mtl.Ns;
		mtlData[ i ].dissolve = mtl.d;

		if( mtl.map_Kd.path ){
			fopen_s( &imgFile, mtl.map_Kd.path, "rb" );

			album.emplace_back( StbLoadImageFromFileToBuffer( imgFile, imageBin ) );
			mtlData[ i ].diffuseIdx = std::size( album ) - 1;

			fclose( imgFile );
		}
		if( mtl.map_bump.path ){
			fopen_s( &imgFile, mtl.map_bump.path, "rb" );

			album.emplace_back( StbLoadImageFromFileToBuffer( imgFile, imageBin ) );
			mtlData[ i ].bumpIdx = std::size( album ) - 1;

			fclose( imgFile );
		}
	}

	textureData.insert( textureData.end(), imageBin.begin(), imageBin.end() );
	materials.insert( materials.end(), mtlData.begin(), mtlData.end() );

	DirectX::BoundingBox aabb;
	DirectX::BoundingBox::CreateFromPoints( aabb,
											std::size( vertices ),
											(const DirectX::XMFLOAT3*) &vertices[ 0 ],
											sizeof( vertices[ 0 ] ) );

	fast_obj_destroy( obj );
	return true;
}


u32 MeshoptBuildMeshlets( const vector<vertex>& vtx, vector<u32>& indices, meshlets_data& mlets )
{
	constexpr u64 MAX_VERTICES = 128;
	constexpr u64 MAX_TRIANGLES = 256;
	constexpr float CONE_WEIGHT = 0.8f;

	u64 maxMeshletCount = meshopt_buildMeshletsBound( std::size( indices ), MAX_VERTICES, MAX_TRIANGLES );
	std::vector<meshopt_Meshlet> meshlets( maxMeshletCount );
	std::vector<u32> meshletVertices( maxMeshletCount * MAX_VERTICES );
	std::vector<u8> meshletTriangles( maxMeshletCount * MAX_TRIANGLES * 3 );

	u64 meshletCount = meshopt_buildMeshlets( std::data( meshlets ), 
											  std::data( meshletVertices ),
											  std::data( meshletTriangles ),
											  std::data( indices ),
											  std::size( indices ), 
											  &vtx[ 0 ].px, 
											  std::size( vtx ),
											  sizeof( vertex ), 
											  MAX_VERTICES,
											  MAX_TRIANGLES,
											  CONE_WEIGHT );


	meshopt_Meshlet& last = meshlets[ meshletCount - 1 ];
	meshletVertices.resize( last.vertex_offset + last.vertex_count );
	meshletTriangles.resize( last.triangle_offset + ( ( last.triangle_count * 3 + 3 ) & ~3 ) );
	meshlets.resize( meshletCount );

	mlets.meshlets.reserve( mlets.meshlets.size() + meshletCount );

	for( meshopt_Meshlet& m : meshlets ){

		
		meshopt_Bounds bounds = meshopt_computeMeshletBounds( &meshletVertices[ m.vertex_offset ], 
															  &meshletTriangles[ m.triangle_offset ],
															  m.triangle_count, 
															  &vtx[ 0 ].px,
															  std::size( vtx ),
															  sizeof( vertex ) );

		meshlet data;
		data.center = vec3( bounds.center );
		data.radius = bounds.radius;
		data.coneX = bounds.cone_axis_s8[ 0 ];
		data.coneY = bounds.cone_axis_s8[ 1 ];
		data.coneZ = bounds.cone_axis_s8[ 2 ];
		data.coneCutoff = bounds.cone_cutoff_s8;
		data.vtxBufOffset = std::size( mlets.vtxIndirBuf );
		data.triBufOffset = std::size( mlets.triangleBuf );
		data.vertexCount = m.vertex_count;
		data.triangleCount = m.triangle_count;

		mlets.meshlets.push_back( data );
	}

	mlets.vtxIndirBuf.insert( mlets.vtxIndirBuf.end(), meshletVertices.begin(), meshletVertices.end() );
	mlets.triangleBuf.insert( mlets.triangleBuf.end(), meshletTriangles.begin(), meshletTriangles.end() );

	return meshlets.size();
}

inline void MeshoptRemapping( vector<vertex>& triCache, vector<u32>& indices )
{
	u64 initialSize = std::size( triCache );
	i64 padding = 3 - std::size( triCache ) % 3;
	for( i64 i = 0; i < padding; ++i ) triCache.push_back( triCache.back() );

	assert( std::size( triCache ) % 3 == 0 );

	u64 idxCount = std::size( triCache );
	std::vector<u32> remap( idxCount );
	u64 vtxCount = meshopt_generateVertexRemap( std::data( remap ), 0,
												idxCount,
												std::data( triCache ),
												idxCount,
												sizeof( vertex ) );

	if( vtxCount < initialSize )
	{
		vector<vertex> vertices( vtxCount );
		indices.resize( idxCount );

		meshopt_remapVertexBuffer( std::data( vertices ), std::data( triCache ), idxCount, sizeof( vertex ), std::data( remap ) );
		meshopt_remapIndexBuffer( std::data( indices ), 0, idxCount, std::data( remap ) );
		triCache = std::move( vertices );
	}
	else
	{
		triCache.resize( initialSize );
	}
}


void MeshoptOptimizeAndLodMesh( 
	DirectX::BoundingBox box,
	vector<vertex>&		vertices, 
	vector<u32>&		indices,
	vertex_attributes&	vtxBuffer, 
	vector<u32>&		idxBuffer, 
	vector<mesh>&		meshes, 
	meshlets_data&		mlets )
{
	meshopt_optimizeVertexCache( std::data( indices ), std::data( indices ), std::size( indices ), std::size( vertices ) );
	meshopt_optimizeOverdraw( std::data( indices ), 
							  std::data( indices ), 
							  std::size( indices ), 
							  &vertices[ 0 ].px, 
							  std::size( vertices ), 
							  sizeof( vertex ), 
							  1.05f );
	meshopt_optimizeVertexFetch( std::data( vertices ), 
								 std::data( indices ),
								 std::size( indices ),
								 std::data( vertices ), 
								 std::size( vertices ),
								 sizeof( vertex ) );

	mesh m = {};
	m.vertexCount = std::size( vertices );
	m.vertexOffset = vtxBuffer.count;
	m.center = box.Center;
	m.radius = XMVectorGetX( XMVector3Length( XMLoadFloat3( &box.Extents ) ) );

	constexpr float ERROR_THRESHOLD = 1e-2f;
	constexpr double expDecay = 0.85;
	std::vector<u32>& lodIndices = indices;
	// TODO: loop better
	for( mesh_lod& lod : m.lods ){
		++m.lodCount;

		lod.indexOffset = std::size( idxBuffer );
		lod.indexCount = std::size( lodIndices );

		idxBuffer.insert( idxBuffer.end(), lodIndices.begin(), lodIndices.end() );

		lod.meshletOffset = std::size( mlets.meshlets );
		// TODO: use last lod for meshlet culling ?
		lod.meshletCount = MeshoptBuildMeshlets( vertices, lodIndices, mlets );

		if( m.lodCount < std::size( m.lods ) )
		{
			u64 nextIndicesTarget = u64( double( std::size( lodIndices ) ) * expDecay );
			u64 nextIndices = meshopt_simplify( std::data( lodIndices ), 
												std::data( lodIndices ), 
												std::size( lodIndices ),
												&vertices[ 0 ].px, 
												std::size( vertices ), 
												sizeof( vertex ), 
												nextIndicesTarget, 
												ERROR_THRESHOLD );

			assert( nextIndices <= std::size( lodIndices ) );

			// NOTE: reached the error bound
			if( nextIndices == std::size( lodIndices ) ) break;

			lodIndices.resize( nextIndices );
			meshopt_optimizeVertexCache( std::data( lodIndices ), std::data( lodIndices ), std::size( lodIndices ), std::size( vertices ) );
		}
	}

	meshes.push_back( m );
}

// TODO:
static constexpr b32 IsLeftHanded( vec3 a, vec3 b, vec3 c )
{
	vec3 e0 = { b.x - a.x, b.y - a.y, b.z - a.z };
	vec3 e1 = { c.x - b.x, c.y - b.y, c.z - b.z };

	return 1;
}
static constexpr void ReverseTriangleWinding( u32* indices, u64 count )
{
	for( u64 t = 0; t < count; t += 3 ) std::swap( indices[ t ], indices[ t + 2 ] );
}
// TODO: memory stuff
// TODO: faster ?
static void GenerateIcosphere( vector<vertex>& vtxData, vector<u32>& idxData, u64 numIters )
{
	constexpr u64 ICOSAHEDRON_FACE_NUM = 20;
	constexpr u64 ICOSAHEDRON_VTX_NUM = 12;

	constexpr float X = 0.525731112119133606f;
	constexpr float Z = 0.850650808352039932f;
	constexpr float N = 0;

	constexpr vec3 vertices[ ICOSAHEDRON_VTX_NUM ] =
	{
		{-X,N,Z}, {X,N,Z}, {-X,N,-Z}, {X,N,-Z},
		{N,Z,X}, {N,Z,-X}, {N,-Z,X}, {N,-Z,-X},
		{Z,X,N}, {-Z,X, N}, {Z,-X,N}, {-Z,-X, N}
	};

	u32 triangles[ 3 * ICOSAHEDRON_FACE_NUM ] =
	{
		0,4,1,	0,9,4,	9,5,4,	4,5,8,	4,8,1,
		8,10,1,	8,3,10,	5,3,8,	5,2,3,	2,7,3,
		7,10,3,	7,6,10,	7,11,6,	11,0,6,	0,1,6,
		6,1,10,	9,0,11,	9,11,2,	9,2,5,	7,2,11
	};

	if constexpr( worldLeftHanded ) ReverseTriangleWinding( triangles, std::size( triangles ) );

	vector<vec3> vtxCache;
	vector<u32> idxCache;

	vtxCache = { std::begin( vertices ), std::end( vertices ) };
	idxData = { std::begin( triangles ),std::end( triangles ) };

	//vtxCache.reserve( ICOSAHEDRON_VTX_NUM * std::exp2( numIters ) );
	idxCache.reserve( 3 * ICOSAHEDRON_FACE_NUM * std::exp2( 2 * numIters ) );
	idxData.reserve( 3 * ICOSAHEDRON_FACE_NUM * std::exp2( 2 * numIters ) );
	

	for( u64 i = 0; i < numIters; ++i ){
		for( u64 t = 0; t < idxData.size(); t += 3 ){
			u32 i0 = idxData[ t ];
			u32 i1 = idxData[ t + 1 ];
			u32 i2 = idxData[ t + 2 ];

			vec3 v0 = vtxCache[ i0 ];
			vec3 v1 = vtxCache[ i1 ];
			vec3 v2 = vtxCache[ i2 ];

			vec3 m01, m12, m20;
			DirectX::XMStoreFloat3( &m01, DirectX::XMVector3Normalize( DirectX::XMVectorAdd(
				DirectX::XMLoadFloat3( &v0 ), DirectX::XMLoadFloat3( &v1 ) ) ) );
			DirectX::XMStoreFloat3( &m12, DirectX::XMVector3Normalize( DirectX::XMVectorAdd( 
				DirectX::XMLoadFloat3( &v1 ), DirectX::XMLoadFloat3( &v2 ) ) ) );
			DirectX::XMStoreFloat3( &m20, DirectX::XMVector3Normalize( DirectX::XMVectorAdd( 
				DirectX::XMLoadFloat3( &v2 ), DirectX::XMLoadFloat3( &v0 ) ) ) );

			u32 idxOffset = vtxCache.size() - 1;

			vtxCache.push_back( m01 );
			vtxCache.push_back( m12 );
			vtxCache.push_back( m20 );

			if constexpr( !worldLeftHanded ){
				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( i0 );

				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( i2 );
				idxCache.push_back( idxOffset + 3 );

				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 3 );
				
				idxCache.push_back( i1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 1 );
			} else{
				idxCache.push_back( i0 );
				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( idxOffset + 1 );

				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( i2 );
				idxCache.push_back( idxOffset + 2 );

				idxCache.push_back( idxOffset + 3 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( idxOffset + 1 );

				idxCache.push_back( idxOffset + 1 );
				idxCache.push_back( idxOffset + 2 );
				idxCache.push_back( i1 );
			}
		}

		idxData = idxCache;
	}

	// TODO: 2 normals per vertex ? how ?
	vector<vec3> normals( std::size( vtxCache ), vec3{} );

	for( u64 t = 0; t < idxData.size(); t += 3 ){
		u32 i0 = idxData[ t ];
		u32 i1 = idxData[ t + 1 ];
		u32 i2 = idxData[ t + 2 ];

		vec3 v0 = vtxCache[ i0 ];
		vec3 v1 = vtxCache[ i1 ];
		vec3 v2 = vtxCache[ i2 ];

		DirectX::XMVECTOR common = DirectX::XMLoadFloat3( &v0 );
		DirectX::XMVECTOR v10 = DirectX::XMVectorSubtract( DirectX::XMLoadFloat3( &v1 ), common );
		DirectX::XMVECTOR v20 = DirectX::XMVectorSubtract( DirectX::XMLoadFloat3( &v2 ), common );
		DirectX::XMVECTOR normal = DirectX::XMVector3Cross( v10, v20 );

		DirectX::XMStoreFloat3( &normals[ i0 ],
								DirectX::XMVector3Normalize( DirectX::XMVectorAdd( normal, DirectX::XMLoadFloat3( &normals[ i0 ] ) ) ) );
		DirectX::XMStoreFloat3( &normals[ i1 ],
								DirectX::XMVector3Normalize( DirectX::XMVectorAdd( normal, DirectX::XMLoadFloat3( &normals[ i1 ] ) ) ) );
		DirectX::XMStoreFloat3( &normals[ i2 ],
								DirectX::XMVector3Normalize( DirectX::XMVectorAdd( normal, DirectX::XMLoadFloat3( &normals[ i2 ] ) ) ) );
	}

	vtxData.resize( vtxCache.size() );

	for( u64 i = 0; i < vtxCache.size(); ++i ){
		vtxData[ i ] = {};
		vtxData[ i ].px = vtxCache[ i ].x;
		vtxData[ i ].py = vtxCache[ i ].y;
		vtxData[ i ].pz = vtxCache[ i ].z;
		//vtxData[ i ].n = Snorm8OctahedronEncode( normals[ i ] );
		vtxData[ i ].nx = normals[ i ].x;
		vtxData[ i ].ny = normals[ i ].y;
		vtxData[ i ].nz = normals[ i ].z;
	}
}
static void GenerateBox( 
	vector<vertex>& vtx, 
	vector<u32>&	idx, 
	float			width = 1.0f, 
	float			height = 1.0f, 
	float			thickenss = 1.0f )
{
#if 1
	float w = width * 0.5f;
	float h = height * 0.5f;
	float t = thickenss * 0.5f;

	vec3 c0 = { w, h,-t };
	vec3 c1 = {-w, h,-t };
	vec3 c2 = {-w,-h,-t };
	vec3 c3 = { w,-h,-t };

	vec3 c4 = { w, h, t };
	vec3 c5 = {-w, h, t };
	vec3 c6 = {-w,-h, t };
	vec3 c7 = { w,-h, t };

	constexpr vec3 u = { 0,1,0 };
	constexpr vec3 d = { 0,-1,0 };
	constexpr vec3 f = { 0,0,1 };
	constexpr vec3 b = { 0,0,-1 };
	constexpr vec3 l = { -1,0,0 };
	constexpr vec3 r = { 1,0,0 };

	vertex vertices[] = {
		// Bottom
		{c0.x,c0.y,c0.z,d.x,d.y,d.z,0,0},
		{c1.x,c1.y,c1.z,d.x,d.y,d.z,0,0},
		{c2.x,c2.y,c2.z,d.x,d.y,d.z,0,0},
		{c3.x,c3.y,c3.z,d.x,d.y,d.z,0,0},
		// Left
		{c7.x,c7.y,c7.z,l.x,l.y,l.z,0,0},
		{c4.x,c4.y,c4.z,l.x,l.y,l.z,0,0},
		{c0.x,c0.y,c0.z,l.x,l.y,l.z,0,0},
		{c3.x,c3.y,c3.z,l.x,l.y,l.z,0,0},
		// Front
		{c4.x,c4.y,c4.z,f.x,f.y,f.z,0,0},
		{c5.x,c5.y,c5.z,f.x,f.y,f.z,0,0},
		{c1.x,c1.y,c1.z,f.x,f.y,f.z,0,0},
		{c0.x,c0.y,c0.z,f.x,f.y,f.z,0,0},
		// Back
		{c6.x,c6.y,c6.z,b.x,b.y,b.z,0,0},
		{c7.x,c7.y,c7.z,b.x,b.y,b.z,0,0},
		{c3.x,c3.y,c3.z,b.x,b.y,b.z,0,0},
		{c2.x,c2.y,c2.z,b.x,b.y,b.z,0,0},
		// Right
		{c5.x,c5.y,c5.z,r.x,r.y,r.z,0,0},
		{c6.x,c6.y,c6.z,r.x,r.y,r.z,0,0},
		{c2.x,c2.y,c2.z,r.x,r.y,r.z,0,0},
		{c1.x,c1.y,c1.z,r.x,r.y,r.z,0,0},
		// Top
		{c7.x,c7.y,c7.z,u.x,u.y,u.z,0,0},
		{c6.x,c6.y,c6.z,u.x,u.y,u.z,0,0},
		{c5.x,c5.y,c5.z,u.x,u.y,u.z,0,0},
		{c4.x,c4.y,c4.z,u.x,u.y,u.z,0,0},
	};

	u32 indices[] = {
			0, 1, 3,        1, 2, 3,        // Bottom	
			4, 5, 7,        5, 6, 7,        // Left
			8, 9, 11,       9, 10, 11,      // Front
			12, 13, 15,     13, 14, 15,     // Back
			16, 17, 19,     17, 18, 19,	    // Right
			20, 21, 23,     21, 22, 23	    // Top
	};

	if constexpr( !worldLeftHanded ) ReverseTriangleWinding( indices, std::size( indices ) );

	vtx.insert( vtx.end(), std::begin( vertices ), std::end( vertices ) );
	idx.insert( idx.end(), std::begin( indices ), std::end( indices ) );
#endif
}

// TODO: pass buff_data ?
__forceinline VkBufferMemoryBarrier
VkMakeBufferBarrier( 
	VkBuffer		hBuff,
	VkAccessFlags	srcAccess, 
	VkAccessFlags	dstAccess,
	VkDeviceSize	buffOffset = 0,
	VkDeviceSize	buffSize = VK_WHOLE_SIZE,
	u32				srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32				dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED )
{
	VkBufferMemoryBarrier memBarrier = {};
	memBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	memBarrier.srcAccessMask = srcAccess;
	memBarrier.dstAccessMask = dstAccess;
	memBarrier.srcQueueFamilyIndex = srcQueueFamIdx;
	memBarrier.dstQueueFamilyIndex = dstQueueFamIdx;
	memBarrier.buffer = hBuff;
	memBarrier.offset = buffOffset;
	memBarrier.size = buffSize;

	return memBarrier;
}

// TODO: must place somewhere else, less global

static buffer_data stagingBuff;
static buffer_data hostComBuff;
static buffer_data geometryBuff;

// TODO: rename these
// TODO: struct buffer_view
// TODO: how many sub-buffers ?
// TODO: make distinction between parent and child buffer ?
static buffer_data vtxPosBuff;
static buffer_data vtxNormBuff;
static buffer_data vtxUvBuff;

// TODO: use indirect merged index buffer
static buffer_data indexBuff;

static buffer_data meshBuff;
static buffer_data meshletBuff;
// TODO: store u8x4 vtx in here ?
static buffer_data meshletVtxBuff;
static buffer_data meshletTrisBuff;

static buffer_data materialsBuff;


static buffer_data drawArgsBuff;


// TODO: mega-buff ?
static buffer_data dispatchCmdBuff;

static buffer_data drawIdxBuff;

static buffer_data drawCmdBuff;
static buffer_data drawCountBuff;

static buffer_data drawCmdDbgBuff;
static buffer_data drawCountDbgBuff;

static buffer_data drawVisibilityBuff;
static buffer_data objectVisibilityBuff;

static buffer_data depthAtomicCounterBuff;

// TODO: must not be static
// TODO: resource manager
static std::vector<image> textures;
static std::vector<VkBufferImageCopy> imgCopyRegions;
static std::vector<buffer_data*> geoMegaBuffPtrs;

// TODO: make sep buffer for dbg
// TODO: mesh dbg selector smth
static void MakeDebugGeometryBuffer( vertex_attributes& vtx, vector<u32>& idx, vector<mesh>& meshes )
{
	vector<vertex> vtxData;
	vector<u32> idxData;

	GenerateIcosphere( vtxData, idxData, 1 );

	mesh mIco = {};
	mIco.center = { 0,0,0 };
	mIco.radius = 1.0f;
	if( std::size( meshes ) )
	{
		mIco.center = meshes[ 0 ].center;
		mIco.radius = meshes[ 0 ].radius;

		for( vertex& v : vtxData )
		{
			v.px *= mIco.radius;
			v.py *= mIco.radius;
			v.pz *= mIco.radius;

			v.px += mIco.center.x;
			v.py += mIco.center.y;
			v.pz += mIco.center.z;
		}
	}
	mIco.vertexOffset = vtx.count;
	mIco.vertexCount = std::size( vtxData );
	mIco.lodCount = 1;
	mIco.lods[ 0 ].indexOffset = std::size( idx );
	mIco.lods[ 0 ].indexCount = std::size( idxData );

	DeinterleaveVertexBuffer( vtxData, vtx );
	idx.insert( idx.end(), idxData.begin(), idxData.end() );
	meshes.push_back( mIco );


	vtxData.resize( 0 );
	idxData.resize( 0 );


	GenerateBox( vtxData, idxData, 1.0f, 0.8f, 0.2f );

	mesh mBox = {};
	mBox.center = { 0,0,0 };
	mBox.radius = 1.0f;
	mBox.vertexOffset = vtx.count;
	mBox.vertexCount = std::size( vtxData );
	mBox.lodCount = 1;
	mBox.lods[ 0 ].indexOffset = std::size( idx );
	mBox.lods[ 0 ].indexCount = std::size( idxData );

	DeinterleaveVertexBuffer( vtxData, vtx );
	idx.insert( idx.end(), idxData.begin(), idxData.end() );
	meshes.push_back( mBox );
}


// TODO: remake, async rsc upload, transfer queue, etc
// TODO: linear alloc rsc mem on CPU side at hostVisible ?
// TODO: sexier ?
struct buffer_region
{
	u64			minStorageBufferOffsetAlignment;
	u64			offset;
	u64			sizeInBytes;
	const void* data;

	template<typename T>
	buffer_region( const T& buff )
		: 
		minStorageBufferOffsetAlignment( dc.gpuProps.limits.minStorageBufferOffsetAlignment ),
		offset( 0 ), 
		sizeInBytes( FwdAlign( std::size( buff ) * sizeof( buff[ 0 ] ), minStorageBufferOffsetAlignment ) ),
		data( (void*) &buff[ 0 ] ){}
};

constexpr char glbPath[] = "D:\\3d models\\cyberdemon\\0.glb";
//constexpr char glbPath[] = "WaterBottle.glb";

static void VkInitAndUploadResources( VkDevice vkDevice )// const buffer_data& stagingBuff )
{
	vertex_attributes vertexBuffer;
	vector<u32> indexBuffer;
	vector<mesh> meshes;
	meshlets_data meshlets;
	vector<u8> textureData; 
	vector<raw_image_info> album;
	// TODO: store material offset and count 
	// TODO: doom eternal style 2 mtls/triangle store in meshlets ?
	vector<material_data> materials;

	DirectX::BoundingBox aabb = {};
	vector<vertex> modelVertices;
	vector<u32> modelIndices;

	LoadGlbModel( glbPath, modelVertices, modelIndices, textureData, album, materials, aabb );
	//MeshoptRemapping( modelVertices, modelIndices );
	MeshoptOptimizeAndLodMesh( aabb, modelVertices, modelIndices, vertexBuffer, indexBuffer, meshes, meshlets );
	DeinterleaveVertexBuffer( modelVertices, vertexBuffer );
	// TODO: write to file compressed
	// TODO: proceed 
	

	// TODO: multi-thread
	//for( const char* model : MODEL_FILES ){
	//	VK_CHECK( VK_INTERNAL_ERROR( !LoadObjModel( model, trianglesCache, textureData, album, materials ) ) );
	//	MeshoptRemapping( trianglesCache, indexCache );
	//	MeshoptPipeline( trianglesCache, indexCache, vertexBuffer, indexBuffer, meshes, meshlets );
	//}
	// 
	// TODO: move to independent buffer 
	MakeDebugGeometryBuffer( vertexBuffer, indexBuffer, meshes );
	
	constexpr u32 meshCount = 1;// std::size( MODEL_FILES );

	u32 drawCount = 4;
	float sceneRadius = 60.0f;

	vector<draw_data> drawArgs( drawCount );

	srand( 42 );
	constexpr double RAND_MAX_SCALE = 1.0 / double( RAND_MAX );

	for( draw_data& d : drawArgs ){
		d.meshIdx = 0;// rand() % meshCount;
		d.bndVolMeshIdx = 1;
		d.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		d.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		d.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius + 40;
		d.scale = 40.0f * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		DirectX::XMVECTOR axis = DirectX::XMVector3Normalize( 
			DirectX::XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
								  float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
								  float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
								  0 ) );
		float angle = DirectX::XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationNormal( axis, angle );
		DirectX::XMStoreFloat4( &d.rot, quat );
	}
	// TODO: draw from transparent only pipe ?
	if constexpr( 0 ){
		draw_data& d = drawArgs[ drawArgs.size() - 1 ];
		d.meshIdx = 5;

		d.pos.x = 0;
		d.pos.y = 0;
		d.pos.z = -50.0f;
		d.scale = 100.0f;

		DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationNormal( DirectX::XMVectorSet( 1, 0, 0, 0 ), 0 );
		DirectX::XMStoreFloat4( &d.rot, quat );
	}

	// TODO: based on what ?
	stagingBuff = VkCreateAllocBindBuffer( 256 * MB, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );

	// TODO: based on what ?
	// TODO: must be buffered as per frame in flight 
	hostComBuff = VkCreateAllocBindBuffer( 1 * MB,
										   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
										   VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
										   &vkHostComArena );

	dispatchCmdBuff = VkCreateAllocBindBuffer( 1 * sizeof( dispatch_command ),
											   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
											   &vkRscArena );

	drawCmdBuff = VkCreateAllocBindBuffer( drawCount * sizeof( draw_command ),
										   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
										   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
										   &vkRscArena );
	// TODO: seems overkill
	drawCountBuff = VkCreateAllocBindBuffer( 4,
											 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
											 &vkRscArena );


	drawCmdDbgBuff = VkCreateAllocBindBuffer( drawCount * sizeof( draw_command ),
											  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
											  &vkRscArena );
	// TODO: seems overkill
	drawCountDbgBuff = VkCreateAllocBindBuffer( 4,
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
												&vkRscArena );

	// TODO: seems overkill
	drawVisibilityBuff = VkCreateAllocBindBuffer( sizeof( u32 ) * drawCount,
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												  &vkRscArena );

	objectVisibilityBuff = VkCreateAllocBindBuffer( ( dc.waveSize / 8 ) * std::ceil( float( drawCount ) / dc.waveSize ),
													VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
													VK_BUFFER_USAGE_TRANSFER_DST_BIT,
													&vkRscArena );
	// TODO: seems overkill
	// TODO: no transfer bit ?
	depthAtomicCounterBuff = VkCreateAllocBindBuffer( 4,
													  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
													  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
													  &vkRscArena );


	buffer_region buffRegs[] = { 
		vertexBuffer.positions, vertexBuffer.normals, vertexBuffer.uvms,
		indexBuffer, 
		meshes, 
		meshlets.meshlets, meshlets.vtxIndirBuf, meshlets.triangleBuf, 
		materials, 
		drawArgs
	};

	u64 geoBuffSize = buffRegs[ 0 ].sizeInBytes;
	for( u64 i = 1; i < std::size( buffRegs ); ++i ){
		buffRegs[ i ].offset = buffRegs[ i - 1 ].offset + buffRegs[ i - 1 ].sizeInBytes;
		geoBuffSize += buffRegs[ i ].sizeInBytes;
	}
	for( const buffer_region& reg : buffRegs ){
		std::memcpy( stagingBuff.hostVisible + reg.offset, reg.data, reg.sizeInBytes );
	}

	geometryBuff = VkCreateAllocBindBuffer( geoBuffSize,
											VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
											VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											&vkRscArena );
	VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, u64( geometryBuff.hndl ), "Geometry_Buff" );

	// TODO: make sexyer
	// TODO: enforce order somehow
	geoMegaBuffPtrs = {
		&vtxPosBuff, &vtxNormBuff, &vtxUvBuff,
		&indexBuff,
		&meshBuff,
		&meshletBuff, &meshletVtxBuff, &meshletTrisBuff,
		&materialsBuff,
		&drawArgsBuff
	};

	for( u64 i = 0; i < std::size( buffRegs ); ++i ){
		geoMegaBuffPtrs[ i ]->hndl = geometryBuff.hndl;
		geoMegaBuffPtrs[ i ]->mem = geometryBuff.mem;
		geoMegaBuffPtrs[ i ]->size = buffRegs[ i ].sizeInBytes;
		geoMegaBuffPtrs[ i ]->offset = buffRegs[ i ].offset;
	}


	// TODO: find better soln
	u64 stagingOffset = geoBuffSize;
	std::memcpy( stagingBuff.hostVisible + stagingOffset, textureData.data(), textureData.size() );

	imgCopyRegions.resize( std::size( album ) );
	textures.resize( std::size( album ) );
	for( u64 i = 0; i < std::size( album ); ++i ){
		VkExtent3D size = { u32( album[ i ].width ), u32( album[ i ].height ), 1 };
		assert( size.width && size.height );
		textures[ i ] = ( VkCreateAllocBindImage( album[ i ].format,
												  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
												  size, 1,
												  &vkAlbumArena ) );
		imgCopyRegions[ i ].bufferOffset = stagingOffset + album[ i ].bufferOffset;
		imgCopyRegions[ i ].bufferRowLength = 0;
		imgCopyRegions[ i ].bufferImageHeight = 0;
		imgCopyRegions[ i ].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imgCopyRegions[ i ].imageSubresource.mipLevel = 0;
		imgCopyRegions[ i ].imageSubresource.baseArrayLayer = 0;
		imgCopyRegions[ i ].imageSubresource.layerCount = 1;
		imgCopyRegions[ i ].imageOffset = VkOffset3D{};
		imgCopyRegions[ i ].imageExtent = size;
	}
}

__forceinline VkSampler VkMakeSampler( 
	VkDevice				vkDevice, 
	float					lodCount = 1.0f, 
	VkSamplerReductionMode	reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX_ENUM,
	VkFilter				filter = VK_FILTER_LINEAR,
	VkSamplerAddressMode	addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	VkSamplerMipmapMode		mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST )
{
	VkSamplerReductionModeCreateInfo reduxInfo = {};
	reduxInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO;
	reduxInfo.reductionMode = reductionMode;

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.pNext = ( reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX_ENUM ) ? 0 : &reduxInfo;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipmapMode = mipmapMode;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = lodCount - 1.0f;

	VkSampler sampler;
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


// TODO: move out of global/static
static u64			VK_DLL = 0;

static vk_program	gfxOpaqueProgram = {};
static vk_program	debugGfxProgram = {};
static vk_program	drawcullCompProgram = {};
static vk_program	depthPyramidCompProgram = {};

static vk_shader	vs = {};
static vk_shader	fs = {};
static vk_shader	drawCullCs = {};
static vk_shader	depthPyramidCs = {};

static vk_program	depthPyramidMultiProgram = {};
static vk_shader	depthMultiCs = {};

// TODO: gfx_api_instance ?
struct vk_instance
{
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMsg;
};
inline static vk_instance VkMakeInstance()
{
	constexpr const char* ENABLED_INST_EXTS[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
	#ifdef VK_USE_PLATFORM_WIN32_KHR
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	#endif // VK_USE_PLATFORM_WIN32_KHR
	#ifdef _VK_DEBUG_
		VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
	#endif // _VK_DEBUG_
	};

	constexpr const char* LAYERS[] =
	{
	#ifdef _VK_DEBUG_
		"VK_LAYER_KHRONOS_validation"
	#endif // _VK_DEBUG_
	};

	VK_CHECK( VK_INTERNAL_ERROR( !( VK_DLL = SysDllLoad( "vulkan-1.dll" ) ) ) );

	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SysGetProcAddr( VK_DLL, "vkGetInstanceProcAddr" );
	GET_VK_GLOBAL_PROC( vkCreateInstance );
	GET_VK_GLOBAL_PROC( vkEnumerateInstanceExtensionProperties );
	GET_VK_GLOBAL_PROC( vkEnumerateInstanceLayerProperties );
	GET_VK_GLOBAL_PROC( vkEnumerateInstanceVersion );

	u32 vkExtsNum = 0;
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, 0 ) );
	vector<VkExtensionProperties> givenExts( vkExtsNum );
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, givenExts.data() ) );
	for( u32 i = 0; i < POPULATION( ENABLED_INST_EXTS ); ++i ){
		b32 foundExt = false;
		for( u32 j = 0; j < vkExtsNum; ++j )
			if( !strcmp( ENABLED_INST_EXTS[ i ], givenExts[ j ].extensionName ) ){
				foundExt = true;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !foundExt ) );
	}

	u32 layerCount = 0;
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, 0 ) );
	vector<VkLayerProperties> layersAvailable( layerCount );
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, layersAvailable.data() ) );
	for( u32 i = 0; i < POPULATION( LAYERS ); ++i ){
		b32 foundLayer = false;
		for( u32 j = 0; j < layerCount; ++j )
			if( !strcmp( LAYERS[ i ], layersAvailable[ j ].layerName ) ){
				foundLayer = true;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !foundLayer ) );
	}

	VkInstance vkInstance = 0;
	VkDebugUtilsMessengerEXT vkDbgUtilsMsgExt = 0;

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	VK_CHECK( vkEnumerateInstanceVersion( &appInfo.apiVersion ) );
	VK_CHECK( VK_INTERNAL_ERROR( appInfo.apiVersion < VK_API_VERSION_1_2 ) );

	VkInstanceCreateInfo instInfo = {};
	instInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
#ifdef _VK_DEBUG_
	VkValidationFeatureEnableEXT enabled = VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT;
	VkValidationFeaturesEXT vkValidationFeatures = {};
	vkValidationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
	vkValidationFeatures.enabledValidationFeatureCount = 1;
	vkValidationFeatures.pEnabledValidationFeatures = &enabled;

	VkDebugUtilsMessengerCreateInfoEXT vkDbgExt = {};
	vkDbgExt.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	vkDbgExt.pNext = vkValidationLayerFeatures ? &vkValidationFeatures : 0;
	vkDbgExt.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
	vkDbgExt.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	vkDbgExt.pfnUserCallback = VkDbgUtilsMsgCallback;

	instInfo.pNext = &vkDbgExt;
#endif
	instInfo.pApplicationInfo = &appInfo;
	instInfo.enabledLayerCount = POPULATION( LAYERS );
	instInfo.ppEnabledLayerNames = LAYERS;
	instInfo.enabledExtensionCount = POPULATION( ENABLED_INST_EXTS );
	instInfo.ppEnabledExtensionNames = ENABLED_INST_EXTS;
	VK_CHECK( vkCreateInstance( &instInfo, 0, &vkInstance ) );

	VkLoadInstanceProcs( vkInstance, *vkGetInstanceProcAddr );

#ifdef _VK_DEBUG_
	VK_CHECK( vkCreateDebugUtilsMessengerEXT( vkInstance, &vkDbgExt, 0, &vkDbgUtilsMsgExt ) );
#endif

	return { vkInstance, vkDbgUtilsMsgExt };
}

// TODO: no structured binding
static void VkBackendInit()
{
	auto [vkInst, vkDbgMsg] = VkMakeInstance();

	vkSurf = VkMakeSurfWin32( vkInst, hInst, hWnd );
	VkMakeDeviceContext( vkInst, vkSurf, &dc );

	VkInitMemory( dc.gpu, dc.device  );

	sc = VkMakeSwapchain( dc.device,dc.gpu, vkSurf, dc.gfxQueueIdx );

	rndCtx.renderPass = VkMakeRndPass( dc.device, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat, false );
	rndCtx.render2ndPass = VkMakeRndPass( dc.device, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat, true );

	VkPipelineCache pipelineCache = 0;

	constexpr char vertPath[] = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\shdr.vert.glsl.spv";
	constexpr char fragPath[] = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\shdr.frag.glsl.spv";
	constexpr char drawCullPath[] = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\draw_cull.comp.spv";
	constexpr char depthPyramidPath[] = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\depth_pyramid.comp.spv";
	constexpr char pow2DownsamplerPath[] = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\pow2_downsampler.comp.spv";

	vs = VkLoadShader( vertPath, dc.device );
	fs = VkLoadShader( fragPath, dc.device );
	drawCullCs = VkLoadShader( drawCullPath, dc.device );
	depthPyramidCs = VkLoadShader( depthPyramidPath, dc.device );

	globBindlessDesc = VkMakeBindlessGlobalDescriptor( dc.device, dc.gpuProps );

	gfxOpaqueProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vs, &fs } );
	drawcullCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &drawCullCs } );
	depthPyramidCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &depthPyramidCs } );


	rndCtx.gfxPipeline = VkMakeGfxPipeline( dc.device, pipelineCache, rndCtx.renderPass, gfxOpaqueProgram.pipeLayout, vs.module, fs.module );
	rndCtx.compPipeline = VkMakeComputePipeline( dc.device, 
												 pipelineCache, 
												 drawcullCompProgram.pipeLayout, 
												 drawCullCs.module, 
												 {OBJ_CULL_WORKSIZE,0 } );
	rndCtx.compLatePipeline = VkMakeComputePipeline( dc.device, 
													 pipelineCache, 
													 drawcullCompProgram.pipeLayout, 
													 drawCullCs.module, 
													 {OBJ_CULL_WORKSIZE,1 } );

	if constexpr( !multiShaderDepthPyramid ){
		rndCtx.compHiZPipeline = VkMakeComputePipeline( dc.device, 
													  pipelineCache, 
													  depthPyramidCompProgram.pipeLayout, 
													  depthPyramidCs.module, {} );
	} else{
		depthMultiCs = VkLoadShader( pow2DownsamplerPath, dc.device );
		depthPyramidMultiProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &depthMultiCs } );
		rndCtx.compHiZPipeline = VkMakeComputePipeline( dc.device,
														pipelineCache,
														depthPyramidMultiProgram.pipeLayout,
														depthMultiCs.module, {} );
	}

	debugGfxProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vs, &fs } );
	rndCtx.gfxBVDbgDrawPipeline = VkMakeGfxPipeline( dc.device,
												 pipelineCache,
												 rndCtx.renderPass,
												 debugGfxProgram.pipeLayout,
												 vs.module,
												 fs.module,
												 VK_POLYGON_MODE_FILL,
												 1,0 );

	VkInitVirutalFrames( dc.device, dc.gfxQueueIdx, rndCtx.vrtFrames, rndCtx.framesInFlight );

	VkInitAndUploadResources( dc.device );
}

__forceinline VkImageMemoryBarrier
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
	u32					layerCount = 0 )
{
	VkImageMemoryBarrier imgMemBarrier = {};
	imgMemBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
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

__forceinline u64 VkGetGroupCount( u64 invocationCount, u64 subgroupSize )
{
	return ( invocationCount + subgroupSize - 1 ) / subgroupSize;
}

#include <intrin.h>
// TODO: math_uitl file
__forceinline u64 FloorPowOf2( u64 size )
{
	// NOTE: use Hacker's Delight for bit-tickery
	constexpr u64 ONE_LEFT_MOST = u64( 1ULL << ( sizeof( u64 ) * 8 - 1 ) );
	return ( size ) ? ONE_LEFT_MOST >> __lzcnt64( size ) : 0;
}
__forceinline u64 GetImgMipCountForPow2( u64 width, u64 height )
{
	// NOTE: log2 == position of the highest bit set (or most significant bit set, MSB)
	// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogObvious
	constexpr u64 TYPE_BIT_COUNT = sizeof( u64 ) * 8 - 1;

	u64 maxDim = max( width, height );
	assert( IsPowOf2( maxDim ) );
	u64 log2MaxDim = TYPE_BIT_COUNT - __lzcnt64( maxDim );

	return min( log2MaxDim, MAX_MIP_LEVELS );
}
__forceinline u64 GetImgMipCount( u64 width, u64 height )
{
	assert( width && height );
	u64 maxDim = max( width, height );

	return min( (u64) floor( log2( maxDim ) ), MAX_MIP_LEVELS );
}

// TODO: more params ?
inline static void 
CullPass( 
	VkCommandBuffer			cmdBuff, 
	VkPipeline				vkPipeline, 
	const vk_program&		program,
	cull_info&				cullInfo )
{
	//cull_info cullInfo = {};
	cullInfo.drawCallsCount = drawArgsBuff.size / sizeof( draw_data );
	cullInfo.pyramidWidthPixels = float( rndCtx.depthPyramid.nativeRes.width );
	cullInfo.pyramidHeightPixels = float( rndCtx.depthPyramid.nativeRes.height );

	VkBufferMemoryBarrier drawCountResetBarriers[] = {
		VkMakeBufferBarrier( drawCountBuff.hndl,
							 VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
							 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT ),

		VkMakeBufferBarrier( drawCmdBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT ),
		VkMakeBufferBarrier( dispatchCmdBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT ),

		VkMakeBufferBarrier( drawCountDbgBuff.hndl,
							 VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
							 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT )
	};

	VkImageMemoryBarrier depthPyramidReadBarrier =
		VkMakeImgBarrier( rndCtx.depthPyramid.img,
						  0,
						  VK_ACCESS_SHADER_READ_BIT,
						  VK_IMAGE_LAYOUT_UNDEFINED,
						  VK_IMAGE_LAYOUT_GENERAL,
						  VK_IMAGE_ASPECT_COLOR_BIT );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  0, 0, 0,
						  POPULATION( drawCountResetBarriers ),
						  drawCountResetBarriers,
						  1, &depthPyramidReadBarrier );

	vkCmdBindPipeline( cmdBuff, program.bindPoint, vkPipeline );

	vkCmdBindDescriptorSets( cmdBuff, program.bindPoint, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

	VkDescriptorImageInfo depthPyramidInfo = { rndCtx.linearMinSampler, rndCtx.depthPyramid.view, VK_IMAGE_LAYOUT_GENERAL };

	vk_descriptor_info drawcullDescs[] = {  
		drawCmdBuff.descriptor(),
		drawCountBuff.descriptor(), 
		drawVisibilityBuff.descriptor(), 
		dispatchCmdBuff.descriptor(),
		objectVisibilityBuff.descriptor(),
		depthPyramidInfo, 
		drawCmdDbgBuff.descriptor(),
		drawCountDbgBuff.descriptor() };

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, drawcullDescs );
	
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( cullInfo ), &cullInfo );

	vkCmdDispatch( cmdBuff, VkGetGroupCount( drawArgsBuff.size / sizeof( draw_data ), program.groupSize.localSizeX ), 1, 1 );

	VkBufferMemoryBarrier drawCmdReadyBarriers[] = {
		VkMakeBufferBarrier( drawCmdBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
		VkMakeBufferBarrier( dispatchCmdBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
		VkMakeBufferBarrier( drawCountBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),

		VkMakeBufferBarrier( drawCmdDbgBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
		VkMakeBufferBarrier( drawCountDbgBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
	};

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, 0,
						  POPULATION( drawCmdReadyBarriers ),
						  drawCmdReadyBarriers, 0, 0 );
}

// TODO: more params ?
inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	VkBuffer				drawCmds,
	VkBuffer				drawCmdCount,
	const VkClearValue*		clearVals,
	vk_program&				program )
{
	VkViewport viewport = { 0, (float) sc.imgHeight, (float) sc.imgWidth, -(float) sc.imgHeight, 0, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { sc.imgWidth, sc.imgHeight } };

	VkRenderPassBeginInfo rndPassBegInfo = {};
	rndPassBegInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;
	rndPassBegInfo.clearValueCount = clearVals ? 2 : 0;
	rndPassBegInfo.pClearValues = clearVals;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetViewport( cmdBuff, 0, 1, &viewport );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, program.bindPoint, vkPipeline );

	vkCmdBindDescriptorSets( cmdBuff, program.bindPoint, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

	vk_descriptor_info descriptors[] = { 
		//vtxPosBuff.descriptor(), 
		//vtxNormBuff.descriptor(),
		//vtxUvBuff.descriptor(),
		drawArgsBuff.descriptor(),
		drawCmdBuff.descriptor()
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

	vkCmdBindIndexBuffer( cmdBuff, indexBuff.hndl, indexBuff.offset, VK_INDEX_TYPE_UINT32 );

	vkCmdDrawIndexedIndirectCount( cmdBuff,
								   drawCmds,
								   offsetof( draw_command, cmd ),
								   drawCmdCount, 0,
								   drawArgsBuff.size / sizeof( draw_data ),
								   sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}

inline static void
DepthPyramidPass(
	VkCommandBuffer cmdBuff,
	VkPipeline		vkPipeline,
	u64				mipLevelsCount,
	VkSampler		linearMinSampler,
	VkImageView		( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const vk_program&		program )
{
	u32 dispatchGroupX = ( ( depthTarget.nativeRes.width + 63 ) >> 6 );
	u32 dispatchGroupY = ( ( depthTarget.nativeRes.height + 63 ) >> 6 );

	downsample_info dsInfo = {};
	dsInfo.mips = mipLevelsCount;
	dsInfo.invRes.x = 1.0f / float( depthTarget.nativeRes.width );
	dsInfo.invRes.y = 1.0f / float( depthTarget.nativeRes.height );
	dsInfo.workGroupCount = dispatchGroupX * dispatchGroupY;


	VkImageMemoryBarrier depthReadBarrier = VkMakeImgBarrier( depthTarget.img,
															  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
															  VK_ACCESS_SHADER_READ_BIT,
															  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
															  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															  VK_IMAGE_ASPECT_DEPTH_BIT,
															  0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,  
						  1, &depthReadBarrier );

	vkCmdBindPipeline( cmdBuff, program.bindPoint, vkPipeline );

	VkDescriptorImageInfo srcImgInfo = { 0, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo samplerInfo = { linearMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	VkDescriptorBufferInfo counterInfo = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	vector<vk_descriptor_info> depthPyramidDescs( MAX_MIP_LEVELS + 4 );
	depthPyramidDescs[ 0 ] = srcImgInfo;
	depthPyramidDescs[ 1 ] = samplerInfo;
	depthPyramidDescs[ 2 ] = { 0, depthMips[ 4 ], VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 3 ] = counterInfo;
	for( u64 i = 0; i < rndCtx.depthPyramidMipCount; ++i ){
		VkDescriptorImageInfo destImgInfo = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
		depthPyramidDescs[ i + 4 ] = destImgInfo;
	}

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, &depthPyramidDescs[ 0 ] );

	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( dsInfo ), &dsInfo );

	vkCmdDispatch( cmdBuff, dispatchGroupX, dispatchGroupY, 1 );

	VkImageMemoryBarrier depthWriteBarrier = VkMakeImgBarrier( depthTarget.img,
															   VK_ACCESS_SHADER_READ_BIT,
															   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
															   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
															   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
															   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
															   VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
						  1, &depthWriteBarrier );
}


inline static void
DepthPyramidMultiPass(
	VkCommandBuffer cmdBuff,
	VkPipeline		vkPipeline,
	VkExtent2D		depthTargetExt,
	u64				mipLevelsCount,
	VkSampler		linearMinSampler,
	VkImageView		( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const vk_program&		program )
{
	VkImageMemoryBarrier depthReadBarriers[] =
	{
		VkMakeImgBarrier( depthTarget.img,
							VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
							VK_ACCESS_SHADER_READ_BIT,
							VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
							VK_IMAGE_ASPECT_DEPTH_BIT ),
	};

	vkCmdPipelineBarrier( cmdBuff, 
						  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 
						  POPULATION( depthReadBarriers ), depthReadBarriers );

	vkCmdBindPipeline( cmdBuff, program.bindPoint, vkPipeline );

	VkDescriptorImageInfo sourceDepth = { linearMinSampler, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	for( u64 i = 0; i < mipLevelsCount; ++i ){
		if( i != 0 ) sourceDepth = { linearMinSampler, depthMips[ i - 1 ], VK_IMAGE_LAYOUT_GENERAL };

		VkDescriptorImageInfo destDepth = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
		vk_descriptor_info descriptors[] = { destDepth, sourceDepth };

		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

		u32 levelWidth = std::max( 1u, depthTargetExt.width >> i );
		u32 levelHeight = std::max( 1u, depthTargetExt.height >> i );

		vec2 reduceData;
		reduceData.x = levelWidth;
		reduceData.y = levelHeight;

		vkCmdPushConstants( cmdBuff, 
							program.pipeLayout, 
							program.pushConstStages, 0, 
							sizeof( reduceData ),
							&reduceData );

		vkCmdDispatch( cmdBuff, 
					   VkGetGroupCount( levelWidth, program.groupSize.localSizeX ),
					   VkGetGroupCount( levelHeight, program.groupSize.localSizeY ),
					   1 );

		VkImageMemoryBarrier reduceBarrier = 
			VkMakeImgBarrier( rndCtx.depthPyramid.img, 
							  VK_ACCESS_SHADER_WRITE_BIT,
							  VK_ACCESS_SHADER_READ_BIT, 
							  VK_IMAGE_LAYOUT_GENERAL, 
							  VK_IMAGE_LAYOUT_GENERAL, 
							  VK_IMAGE_ASPECT_COLOR_BIT );

		vkCmdPipelineBarrier( cmdBuff, 
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
							  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 
							  1, &reduceBarrier );
	}

	VkImageMemoryBarrier depthWriteBarrier = 
		VkMakeImgBarrier( depthTarget.img, 
						  VK_ACCESS_SHADER_READ_BIT, 
						  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, 
						  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
						  VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 
						  VK_IMAGE_ASPECT_DEPTH_BIT );

	vkCmdPipelineBarrier( cmdBuff, 
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 
						  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 
						  1, &depthWriteBarrier );
}

// TODO: submit/flush queues earlier ?
// TODO: no cull_info param
// TODO: VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT  ?
static void HostFrames( const global_data* globs,  cull_info cullInfo, b32 bvDraw )
{
	//VkSemaphoreWaitInfo semaWaitInfo = {};
	//semaWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	//semaWaitInfo.semaphoreCount = 1;
	//semaWaitInfo.pSemaphores = &rndCtx.hostSyncTimeline;
	//semaWaitInfo.pValues = &rndCtx.hostSyncWait;
	//++rndCtx.hostSyncWait;
	//// TODO: what do we do if it takes too long ? can it take too long ?
	//VkResult hostSync = vkWaitSemaphores( dc.device, &semaWaitInfo, 1'000'000'000 );
	//VK_CHECK( ( hostSync - VK_TIMEOUT ) > 0 );

	u64 currentFrameIdx = rndCtx.vFrameIdx;
	virtual_frame& currentVFrame = rndCtx.vrtFrames[ rndCtx.vFrameIdx ];
	rndCtx.vFrameIdx = ( rndCtx.vFrameIdx + 1 ) % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;

	VK_CHECK( VK_INTERNAL_ERROR( ( 
		vkWaitForFences( dc.device, 1, &currentVFrame.hostSyncFence, true, 1'000'000'000 ) - VK_TIMEOUT ) > 0 ) );
	VK_CHECK( vkResetFences( dc.device, 1, &currentVFrame.hostSyncFence ) );

	VK_CHECK( vkResetCommandPool( dc.device, currentVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

	u32 imgIdx;
	VK_CHECK( vkAcquireNextImageKHR( dc.device, sc.swapchain, UINT64_MAX, currentVFrame.canGetImgSema, 0, &imgIdx ) );

	// TODO: swapchain resize ?
	if( !rndCtx.offscreenFbo ){
		if( !rndCtx.depthTarget.img ){
			rndCtx.depthTarget = VkCreateAllocBindImage( rndCtx.desiredDepthFormat,
														 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
														 VK_IMAGE_USAGE_SAMPLED_BIT,
														 VkExtent3D{ sc.imgWidth, sc.imgHeight, 1 }, 1,
														 &vkAlbumArena );
			if constexpr( !multiShaderDepthPyramid ){
				// TODO: make conservative ?
				rndCtx.depthPyramidWidth = ( sc.imgWidth ) / 2;
				rndCtx.depthPyramidHeight = ( sc.imgHeight ) / 2;
				rndCtx.depthPyramidMipCount = GetImgMipCount( rndCtx.depthPyramidWidth, rndCtx.depthPyramidHeight );
			} else{
				rndCtx.depthPyramidWidth = FloorPowOf2( sc.imgWidth );
				rndCtx.depthPyramidHeight = FloorPowOf2( sc.imgHeight );
				rndCtx.depthPyramidMipCount = GetImgMipCountForPow2( rndCtx.depthPyramidWidth, rndCtx.depthPyramidHeight );
			}
			VK_CHECK( VK_INTERNAL_ERROR( !( rndCtx.depthPyramidMipCount < MAX_MIP_LEVELS ) ) );
			rndCtx.depthPyramid = VkCreateAllocBindImage( VK_FORMAT_R32_SFLOAT,
														  VK_IMAGE_USAGE_SAMPLED_BIT | 
														  VK_IMAGE_USAGE_STORAGE_BIT |
														  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
														  VkExtent3D{ rndCtx.depthPyramidWidth, rndCtx.depthPyramidHeight,1 },
														  rndCtx.depthPyramidMipCount,
														  &vkAlbumArena );

			VkDbgNameObj( dc.device, VK_OBJECT_TYPE_IMAGE, (u64) rndCtx.depthPyramid.img, "Depth_Pyramid" );

			for( u64 i = 0; i < rndCtx.depthPyramidMipCount; ++i ){
				rndCtx.depthPyramidChain[ i ] =
					VkMakeImgView( dc.device, rndCtx.depthPyramid.img, VK_FORMAT_R32_SFLOAT, i, 1 );
			}

			rndCtx.linearMinSampler = VkMakeSampler( dc.device, rndCtx.depthPyramidMipCount, VK_SAMPLER_REDUCTION_MODE_MIN );
		}

		if( !rndCtx.colorTarget.img )
			rndCtx.colorTarget = VkCreateAllocBindImage( rndCtx.desiredColorFormat,
														 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
														 VkExtent3D{ sc.imgWidth, sc.imgHeight, 1 }, 1,
														 &vkAlbumArena );

		VkImageView attachements[] = { rndCtx.colorTarget.view, rndCtx.depthTarget.view };
		rndCtx.offscreenFbo = VkMakeFramebuffer( dc.device,
												 rndCtx.renderPass,
												 attachements,
												 POPULATION( attachements ),
												 sc.imgWidth,
												 sc.imgHeight );
	}

	assert( ( currentFrameIdx == 0 ) || ( currentFrameIdx == 1 ) );
	
	// TODO: barrier ?
	// TODO: manage/abstract this somehow ?
	// TODO: cache offset ?
	u64 doubleBufferOffset = FwdAlign( sizeof( global_data ), dc.gpuProps.limits.minUniformBufferOffsetAlignment );
	std::memcpy( hostComBuff.hostVisible + doubleBufferOffset * currentFrameIdx, (u8*) globs, sizeof( global_data ) );
	
	VkDescriptorBufferInfo uboInfo = { hostComBuff.hndl, doubleBufferOffset * currentFrameIdx, sizeof( global_data ) };
	VkWriteDescriptorSet globalDataUpdate = VkMakeBindlessGlobalUpdate( &uboInfo, 1, VK_GLOBAL_SLOT_UNIFORM_BUFFER );
	vkUpdateDescriptorSets( dc.device, 1, &globalDataUpdate, 0, 0 );

	VkCommandBufferBeginInfo cmdBufBegInfo = {};
	cmdBufBegInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufBegInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
	vkBeginCommandBuffer( currentVFrame.cmdBuf, &cmdBufBegInfo );

	// TODO: where to place this ?
	// TODO: async, multi-threaded, etc
	// TODO: must framebuffer staging
	static b32 rescUploaded = 0;
	if( !rescUploaded ){
		VkBufferMemoryBarrier copyBarrier = 
			VkMakeBufferBarrier( geometryBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT );

		VkBufferCopy geometryBuffCopyRegion = { 0,0,geometryBuff.size };
		vkCmdCopyBuffer( currentVFrame.cmdBuf, stagingBuff.hndl, geometryBuff.hndl, 1, &geometryBuffCopyRegion );

		vector<VkImageMemoryBarrier> barriers( std::size( textures ) );
		for( u64 i = 0; i < std::size( textures ); ++i ){
			barriers[ i ] = VkMakeImgBarrier( textures[ i ].img, 0,
											  VK_ACCESS_TRANSFER_WRITE_BIT,
											  VK_IMAGE_LAYOUT_UNDEFINED,
											  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											  VK_IMAGE_ASPECT_COLOR_BIT );
		}

		vkCmdPipelineBarrier( currentVFrame.cmdBuf,
							  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							  VK_PIPELINE_STAGE_TRANSFER_BIT,
							  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
							  std::size( barriers ), barriers.data() );

		for( u64 i = 0; i < std::size( textures ); ++i ){
			vkCmdCopyBufferToImage( currentVFrame.cmdBuf,
									stagingBuff.hndl,
									textures[ i ].img,
									VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
									1, &imgCopyRegions[ i ] );

			barriers[ i ] = VkMakeImgBarrier( textures[ i ].img,
											  VK_ACCESS_TRANSFER_WRITE_BIT,
											  VK_ACCESS_SHADER_READ_BIT,
											  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
											  VK_IMAGE_ASPECT_COLOR_BIT );
		}
		
		vkCmdPipelineBarrier( currentVFrame.cmdBuf,
							  VK_PIPELINE_STAGE_TRANSFER_BIT,
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
							  VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
							  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
							  VK_DEPENDENCY_BY_REGION_BIT, 0, 0,
							  1, &copyBarrier,
							  std::size( barriers ), std::data( barriers ) );

		// TODO: use push consts ?
		geometry_buffer_info geomBuffInfo = {};
		geomBuffInfo.addr = geometryBuff.devicePointer;

		assert( ( sizeof( geomBuffInfo ) / sizeof( u64 ) - 1 ) == std::size( geoMegaBuffPtrs ) );
		u64* pGeoInfo = (u64*) &geomBuffInfo + 1;
		for( u64 i = 0; i < std::size( geoMegaBuffPtrs ); ++i ){
			pGeoInfo[ i ] = geoMegaBuffPtrs[ i ]->offset;
		}
		
		// TODO: barrier ?
		std::memcpy( hostComBuff.hostVisible + doubleBufferOffset * VK_MAX_FRAMES_IN_FLIGHT_ALLOWED, 
					 (u8*) &geomBuffInfo, 
					 sizeof( geomBuffInfo ) );

		VkDescriptorBufferInfo geomUbo = { 
			hostComBuff.hndl,
			VK_MAX_FRAMES_IN_FLIGHT_ALLOWED * doubleBufferOffset,
			sizeof( geomBuffInfo ) };

		vector<VkDescriptorImageInfo> texDesc;
		texDesc.reserve( std::size( textures ) );
		for( const image& t : textures ) texDesc.push_back( { 0, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } );

		rndCtx.linearTextureSampler = VkMakeSampler( dc.device, 1, 
													 VK_SAMPLER_REDUCTION_MODE_MAX_ENUM, VK_FILTER_LINEAR, 
													 VK_SAMPLER_ADDRESS_MODE_REPEAT );
		VkDescriptorImageInfo samplerDesc = {};
		samplerDesc.sampler = rndCtx.linearTextureSampler;

		u64 updatesCount = 0;
		VkWriteDescriptorSet updates[ VK_GLOBAL_SLOT_COUNT ] = {};
		if( geomUbo.buffer ){
			updates[ updatesCount++ ] = VkMakeBindlessGlobalUpdate( &geomUbo, 1, VK_GLOBAL_SLOT_UNIFORM_BUFFER, 1 );
		}
		if( std::data( texDesc ) ){
			updates[ updatesCount++ ] = 
				VkMakeBindlessGlobalUpdate( std::data( texDesc ), std::size( texDesc ), VK_GLOBAL_SLOT_SAMPLED_IMAGE );
		}
		if( samplerDesc.sampler ){
			updates[ updatesCount++ ] = VkMakeBindlessGlobalUpdate( &samplerDesc, 1, VK_GLOBAL_SLOT_SAMPLER );
		}

		vkUpdateDescriptorSets( dc.device, updatesCount, updates, 0, 0 );

		rescUploaded = 1;
	}

	static b32 clearedBuffers = 0;
	if( !clearedBuffers ){
		vkCmdFillBuffer( currentVFrame.cmdBuf, drawVisibilityBuff.hndl, 0, drawVisibilityBuff.size, 1U );
		vkCmdFillBuffer( currentVFrame.cmdBuf, objectVisibilityBuff.hndl, 0, objectVisibilityBuff.size, 1U  );
		vkCmdFillBuffer( currentVFrame.cmdBuf, depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size, 0 );

		VkBufferMemoryBarrier clearedVisibilityBarrier[] = {
			VkMakeBufferBarrier( drawVisibilityBuff.hndl,
								 VK_ACCESS_TRANSFER_WRITE_BIT,
								 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT ),
			VkMakeBufferBarrier( objectVisibilityBuff.hndl,
								 VK_ACCESS_TRANSFER_WRITE_BIT,
								 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT ),
			VkMakeBufferBarrier( depthAtomicCounterBuff.hndl,
								 VK_ACCESS_TRANSFER_WRITE_BIT,
								 VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT )
		};

		vkCmdPipelineBarrier( currentVFrame.cmdBuf,
							  VK_PIPELINE_STAGE_TRANSFER_BIT,
							  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0,
							  2, clearedVisibilityBarrier, 0, 0 );
		clearedBuffers = 1;
	}

	VkImageMemoryBarrier renderBeginBarriers[] =
	{
		VkMakeImgBarrier(
			rndCtx.colorTarget.img, 0, 0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 ),
		VkMakeImgBarrier(
			rndCtx.depthTarget.img, 0, 0,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0 ),
	};

	vkCmdPipelineBarrier( currentVFrame.cmdBuf,
						  VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
						  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
						  POPULATION( renderBeginBarriers ),
						  renderBeginBarriers );


	// FIRST PASS
	cullInfo.mipLevelsCount = rndCtx.depthPyramidMipCount;
	CullPass( currentVFrame.cmdBuf,
			  rndCtx.compPipeline,
			  drawcullCompProgram,
			  cullInfo );

	VkClearValue clearVals[ 2 ] = {};
	DrawIndirectPass( currentVFrame.cmdBuf,
					  rndCtx.gfxPipeline,
					  rndCtx.renderPass,
					  rndCtx.offscreenFbo,
					  drawCmdBuff.hndl,
					  drawCountBuff.hndl,
					  clearVals,
					  gfxOpaqueProgram );

	
	if constexpr( !multiShaderDepthPyramid )
	{
		DepthPyramidPass( currentVFrame.cmdBuf,
						  rndCtx.compHiZPipeline,
						  rndCtx.depthPyramidMipCount,
						  rndCtx.linearMinSampler,
						  rndCtx.depthPyramidChain,
						  rndCtx.depthTarget,
						  depthPyramidCompProgram );
	}
	else
	{
		DepthPyramidMultiPass(
			currentVFrame.cmdBuf,
			rndCtx.compHiZPipeline,
			{ rndCtx.depthPyramid.nativeRes.width, rndCtx.depthPyramid.nativeRes.height },
			rndCtx.depthPyramidMipCount,
			rndCtx.linearMinSampler,
			rndCtx.depthPyramidChain,
			rndCtx.depthTarget,
			depthPyramidMultiProgram );
	}

	if( boundingSphereDbgDraw && bvDraw )
	{
		DrawIndirectPass( currentVFrame.cmdBuf,
						  rndCtx.gfxBVDbgDrawPipeline,
						  rndCtx.render2ndPass,
						  rndCtx.offscreenFbo,
						  drawCmdDbgBuff.hndl,
						  drawCountDbgBuff.hndl,
						  0,
						  debugGfxProgram );
	}

	// SECOND PASS
	if( occlusionCullingPass ){
		CullPass( currentVFrame.cmdBuf,
				  rndCtx.compLatePipeline,
				  drawcullCompProgram,
				  cullInfo );


		DrawIndirectPass( currentVFrame.cmdBuf,
						  rndCtx.gfxPipeline,
						  rndCtx.render2ndPass,
						  rndCtx.offscreenFbo,
						  drawCmdBuff.hndl,
						  drawCountBuff.hndl,
						  0,
						  gfxOpaqueProgram );
	}


	VkImageMemoryBarrier copyBarriers[] =
	{
		VkMakeImgBarrier(
			rndCtx.colorTarget.img,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT,0,0 ),
		VkMakeImgBarrier(
			sc.imgs[ imgIdx ], 0,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT,0,0 ),
	};

	vkCmdPipelineBarrier( currentVFrame.cmdBuf, 
						  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
						  VK_PIPELINE_STAGE_TRANSFER_BIT, 
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 
						  POPULATION( copyBarriers ), 
						  copyBarriers );

	// TODO: use render pass attch if MSAA, else output from compute shader ( VRS, CMAA2, and post processing ) 
	VkImageCopy copyRegion = {};
	copyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.srcSubresource.layerCount = 1;
	copyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	copyRegion.dstSubresource.layerCount = 1;
	copyRegion.extent = { sc.imgWidth, sc.imgHeight, 1 };

	vkCmdCopyImage( currentVFrame.cmdBuf,
					rndCtx.colorTarget.img, 
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 
					sc.imgs[ imgIdx ],
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 
					&copyRegion );

	VkImageMemoryBarrier presentBarrier = VkMakeImgBarrier( sc.imgs[ imgIdx ], 
															VK_ACCESS_TRANSFER_WRITE_BIT, 0, 
															VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
															VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
															VK_IMAGE_ASPECT_COLOR_BIT,0,0 );
	vkCmdPipelineBarrier( currentVFrame.cmdBuf, 
						  VK_PIPELINE_STAGE_TRANSFER_BIT, 
						  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, 
						  &presentBarrier );

	VK_CHECK( vkEndCommandBuffer( currentVFrame.cmdBuf ) );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &currentVFrame.canGetImgSema;

	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_TRANSFER_BIT; // VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.pWaitDstStageMask = &waitDstStageMsk;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &currentVFrame.cmdBuf;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &currentVFrame.canPresentSema;
	VK_CHECK( vkQueueSubmit( dc.gfxQueue, 1, &submitInfo, currentVFrame.hostSyncFence ) );

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &currentVFrame.canPresentSema;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &sc.swapchain;
	presentInfo.pImageIndices = &imgIdx;
	VK_CHECK( vkQueuePresentKHR( dc.gfxQueue, &presentInfo ) );

	//VkSemaphoreSignalInfo signalInfo = {};
	//signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
	//signalInfo.semaphore = rndCtx.hostSyncTimeline;
	//signalInfo.value = rndCtx.hostSyncWait;
	//VK_CHECK( vkSignalSemaphore( dc.device, &signalInfo ) );
}

static void VkBackendKill()
{
	// NOTE: SHOULDN'T need to check if(VkObj) can't create -> app fail
	vkDeviceWaitIdle( dc.device );
	for( auto& queued : deviceGlobalDeletionQueue ) queued();
	deviceGlobalDeletionQueue.clear();
	
	vkDestroyDevice( dc.device, 0 );
#ifdef _VK_DEBUG_
	vkDestroyDebugUtilsMessengerEXT( vkInst, vkDbgMsg, 0 );
#endif
	vkDestroySurfaceKHR( vkInst, vkSurf, 0 );
	vkDestroyInstance( vkInst, 0 );
	// TODO: does os unload dll after program termination ?
	SysDllUnload ( VK_DLL );
}

#undef VK_APPEND_DESTROYER
#undef VK_CHECK
#undef GET_VK_GLOBAL_PROC

