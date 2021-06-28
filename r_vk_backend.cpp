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
#include <span>

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
inline std::string_view VkResErrorString( VkResult errorCode )
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

inline VkResult VkResFromStatement( b32 statement )
{
	return !statement ? VK_SUCCESS : VkResult( int( 0x8FFFFFFF ) );
}
// TODO: keep ?
#define VK_INTERNAL_ERROR( vk ) VkResFromStatement( b32( vk ) )

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

//====================CONSTS====================//
constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 256 * MB;
constexpr u64 MAX_MIP_LEVELS = 12;
constexpr u32 NOT_USED_IDX = -1;
constexpr u64 OBJ_CULL_WORKSIZE = 64;
constexpr u64 MLET_CULL_WORKSIZE = 256;
//==============================================//
// TODO: cvars
//====================CVARS====================//
static b32 boundingVolDbgDraw = 1;
static b32 colorBlending = 0;
static b32 occlusionCullingPass = 0;
//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//
constexpr b32 multiShaderDepthPyramid = 1;
// TODO: enable gfx debug outside of VS Debug
constexpr b32 vkValidationLayerFeatures = 1;
constexpr b32 worldLeftHanded = 1;
constexpr b32 objectNaming = 1;
//==============================================//


// TODO: make an allocator and let it manage arenas and dedicated allocs
// TODO: multi gpu ?
// TODO: multi threaded
// TODO: better alloc strategy ?
// TODO: alloc vector for debug only
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

static vk_mem_arena vkRscArena, vkStagingArena, vkAlbumArena, vkHostComArena, vkDbgArena;

inline b32 IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
inline u64 FwdAlign( u64 addr, u64 alignment )
{
	assert( IsPowOf2( alignment ) );
	u64 mod = addr & ( alignment - 1 );
	return mod ? addr + ( alignment - mod ) : addr;
}


inline i32
VkFindMemTypeIdx(
	const VkPhysicalDeviceMemoryProperties* pVkMemProps,
	VkMemoryPropertyFlags				requiredProps,
	u32									memTypeBitsRequirement
){
	for( u64 memIdx = 0; memIdx < pVkMemProps->memoryTypeCount; ++memIdx )
	{
		u32 memTypeBits = ( 1 << memIdx );
		b32 isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps->memoryTypes[ memIdx ].propertyFlags;
		b32 hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps ) return (i32) memIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( "Memory type unmatch !" ) );

	return -1;
}

// TODO: move alloc flags ?
inline static VkDeviceMemory
VkTryAllocDeviceMem(
	VkDevice								vkDevice,
	u64										size,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo* dedicated
){
	VkMemoryAllocateFlagsInfo allocFlagsInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	allocFlagsInfo.pNext = dedicated;
	allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;// allocFlags;

	VkMemoryAllocateInfo memoryAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
	memoryAllocateInfo.pNext = &allocFlagsInfo;
	memoryAllocateInfo.allocationSize = size;
	memoryAllocateInfo.memoryTypeIndex = memTypeIdx;

	VkDeviceMemory mem;
	VK_CHECK( vkAllocateMemory( vkDevice, &memoryAllocateInfo, 0, &mem ) );

	return mem;
}

inline static void
VkArenaInit(
	vk_mem_arena* vkArena,
	const VkPhysicalDeviceMemoryProperties& memProps,
	VkMemoryPropertyFlags				memType,
	VkDevice							vkDevice 
){
	i32 i = 0;
	for( ; i < memProps.memoryTypeCount; ++i )
	{
		if( memProps.memoryTypes[ i ].propertyFlags == memType ) break;
	}
		
	VK_CHECK( VK_INTERNAL_ERROR( i == memProps.memoryTypeCount ) );

	VkMemoryHeap backingHeap = memProps.memoryHeaps[ memProps.memoryTypes[ i ].heapIndex ];

	vkArena->allocated = 0;
	vkArena->size = 0;
	vkArena->memTypeIdx = i;
	vkArena->memTypeProperties = memProps.memoryTypes[ i ].propertyFlags;
	vkArena->maxParentHeapSize = backingHeap.size;
	vkArena->minVkAllocationSize =
		( backingHeap.size < VK_MIN_DEVICE_BLOCK_SIZE ) ? ( 1 * MB ) : VK_MIN_DEVICE_BLOCK_SIZE;
	vkArena->device = vkDevice;
}

// TODO: assert vs VK_CHECK vs default + warning
// TODO: must alloc in block with BUFFER_ADDR
inline vk_allocation
VkArenaAlignAlloc(
	vk_mem_arena* vkArena,
	u64										size,
	u64										align,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated
){
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
		return { lastDedicated.device, (u8*) lastDedicated.host, 0 };
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
	vk_allocation allocId = { lastBlock.device, (u8*) lastBlock.host, vkArena->allocated };
	vkArena->allocated += size;

	return allocId;
}

inline void
VkArenaTerimate( vk_mem_arena* vkArena )
{
	for( u64 i = 0; i < std::size( vkArena->mem ); ++i )
		vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
	for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
}

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

inline u64 VkGetBufferDeviceAddress( VkDevice vkDevice, VkBuffer hndl )
{
	static_assert( std::is_same<VkDeviceAddress, u64>::value );

	VkBufferDeviceAddressInfo deviceAddrInfo = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	deviceAddrInfo.buffer = hndl;

	return vkGetBufferDeviceAddress( vkDevice, &deviceAddrInfo );
}

// TODO: keep memory in buffer ?
// TODO: use buffer offset ?
struct buffer_data
{
	VkBuffer		hndl = 0;
	VkDeviceMemory	mem = 0;
	u64				size = 0;
	//u64				offset = 0;
	u8* hostVisible = 0;
	u64				devicePointer = 0;
	u32				magicId;

	inline VkDescriptorBufferInfo descriptor() const
	{
		//return VkDescriptorBufferInfo{ hndl,offset,size };
		return VkDescriptorBufferInfo{ hndl,0,size };
	}
};

// TODO: use a texture_desc struct
// TODO: add more data ?
// TODO: VkDescriptorImageInfo 
// TODO: rsc don't directly store the memory, rsc manager refrences it ?
struct image
{
	VkImage			img;
	VkImageView		view;
	VkDeviceMemory	mem;
	VkFormat		nativeFormat;
	u32				magicId;
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



// TODO: objType from objHandle ?
inline void VkDbgNameObj( VkDevice vkDevice, VkObjectType objType, u64 objHandle, const char* name )
{
#ifdef _VK_DEBUG_
	if constexpr( !objectNaming ) return;

	VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	nameInfo.objectType = objType;
	nameInfo.objectHandle = objHandle;
	nameInfo.pObjectName = name;

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
#endif
}


// NOTE: inspired by Chili Framework
struct color32
{
	u32 dword;

	constexpr color32() : dword(){}
	//constexpr color32( const color32& col ) : dword( col.dword ){}
	constexpr color32( u32 dw ) : dword( dw ){}
	constexpr color32( u8 x, u8 r, u8 g, u8 b ) : dword( ( x << 24u ) | ( r << 16u ) | ( g << 8u ) | b ){}
	constexpr color32( u8 r, u8 g, u8 b ) : dword( ( r << 16u ) | ( g << 8u ) | b ){}
	constexpr color32( color32 col, u8 x ) : color32( ( x << 24u ) | col.dword ){}
	//color32& operator =( color32 color )
	//{
	//	dword = color.dword;
	//	return *this;
	//}
};

constexpr u8 Col32GetA( color32 c ){ return c.dword >> 24u; }
constexpr u8 Col32GetR( color32 c ){ return ( c.dword >> 16u ) & 0xFFu; }
constexpr u8 Col32GetG( color32 c ){ return ( c.dword >> 8u ) & 0xFFu; }
constexpr u8 Col32GetB( color32 c ){ return c.dword & 0xFFu; }
constexpr void GetCol32AsFloat( color32 c, float* col )
{
	float r = float( Col32GetR( c ) ) / float( u8( -1 ) );
	float g = float( Col32GetG( c ) ) / float( u8( -1 ) );
	float b = float( Col32GetB( c ) ) / float( u8( -1 ) );
	float a = float( Col32GetA( c ) ) / float( u8( -1 ) );

	col[ 0 ] = r;
	col[ 1 ] = g;
	col[ 2 ] = b;
	col[ 3 ] = a;
}
//void SetA( unsigned char x )
//{
//	dword = ( dword & 0xFFFFFFu ) | ( x << 24u );
//}
//void SetR( unsigned char r )
//{
//	dword = ( dword & 0xFF00FFFFu ) | ( r << 16u );
//}
//void SetG( unsigned char g )
//{
//	dword = ( dword & 0xFFFF00FFu ) | ( g << 8u );
//}
//void SetB( unsigned char b )
//{
//	dword = ( dword & 0xFFFFFF00u ) | b;
//}

namespace Colors
{
	static constexpr color32 MakeRGB( u8 r, u8 g, u8 b )
	{
		return ( r << 16 ) | ( g << 8 ) | b;
	}
	static constexpr color32 white = MakeRGB( 255u, 255u, 255u );
	static constexpr color32 black = MakeRGB( 0u, 0u, 0u );
	static constexpr color32 gray = MakeRGB( 0x80u, 0x80u, 0x80u );
	static constexpr color32 lightGray = MakeRGB( 0xD3u, 0xD3u, 0xD3u );
	static constexpr color32 red = MakeRGB( 255u, 0u, 0u );
	static constexpr color32 green = MakeRGB( 0u, 255u, 0u );
	static constexpr color32 blue = MakeRGB( 0u, 0u, 255u );
	static constexpr color32 yellow = MakeRGB( 255u, 255u, 0u );
	static constexpr color32 cyan = MakeRGB( 0u, 255u, 255u );
	static constexpr color32 magenta = MakeRGB( 255u, 0u, 255u );
}
// TODO: degub-on-off
struct vk_label
{
	const VkCommandBuffer& cmdBuf;

	inline vk_label( const VkCommandBuffer& cmdBuff, const char* labelName, color32 col )
		: cmdBuf{ cmdBuff }
	{
		assert( cmdBuf );
		VkDebugUtilsLabelEXT dbgLabel = {};
		dbgLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		dbgLabel.pLabelName = labelName;
		assert( std::size( dbgLabel.color ) == 4 );
		GetCol32AsFloat( col, dbgLabel.color );
		vkCmdBeginDebugUtilsLabelEXT( cmdBuf, &dbgLabel );
	}
	inline ~vk_label()
	{
		assert( cmdBuf );
		vkCmdEndDebugUtilsLabelEXT( cmdBuf );
	}
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

// TODO: add VkFramebuffer here ?
struct virtual_frame
{
	buffer_data		frameData;
	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuf;
	VkSemaphore		canGetImgSema;
	VkSemaphore		canPresentSema;
	VkFence			hostSyncFence;
};

struct swapchain
{
	VkSwapchainKHR	swapchain;
	VkImageView		imgViews[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkImage			imgs[ VK_SWAPCHAIN_MAX_IMG_ALLOWED ];
	VkFormat		imgFormat;
	u16				width;
	u16				height;
	u8				imgCount;
};
// TODO: remake
struct render_context
{
	VkPipeline		gfxPipeline;
	VkPipeline		compPipeline;
	VkPipeline		compHiZPipeline;
	VkPipeline		gfxBVDbgDrawPipeline;
	VkPipeline		gfxTranspPipe;
	VkPipeline		compAvgLumPipe;
	VkPipeline		compTonemapPipe;
	VkRenderPass	renderPass;
	VkRenderPass	render2ndPass;

	VkSampler		linearMinSampler;
	VkSampler		linearTextureSampler;
	image			depthTarget;
	image			colorTarget;

	image			depthPyramid;
	VkImageView		depthPyramidChain[ MAX_MIP_LEVELS ];
	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
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

// TODO: move to memory section
// TODO: pass VkPhysicalDevice differently

// TODO: pass device to rscCreate functions ?
// TODO: no dedicated alloc for buffers at all ?
static buffer_data
VkCreateAllocBindBuffer(
	u64					sizeInBytes,
	VkBufferUsageFlags	usage,
	vk_mem_arena*		vkArena
){
	buffer_data buffData = {};

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = sizeInBytes;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK( vkCreateBuffer( vkArena->device, &bufferInfo, 0, &buffData.hndl ) );

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	VkBufferMemoryRequirementsInfo2 buffMemReqs2 = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
	buffMemReqs2.buffer = buffData.hndl;
	vkGetBufferMemoryRequirements2( vkArena->device, &buffMemReqs2, &memReqs2 );

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena->memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena->memTypeIdx ) ) );
	assert( memTypeIdx == vkArena->memTypeIdx );
#endif

	VkMemoryAllocateFlags allocFlags =
		( usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT ) ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT : 0;

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.buffer = buffMemReqs2.buffer;

	//b32 dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation; 
	b32 dedicatedAlloc = dedicatedReqs.requiresDedicatedAllocation;

	vk_allocation bufferMem = VkArenaAlignAlloc( vkArena,
												 memReqs2.memoryRequirements.size,
												 memReqs2.memoryRequirements.alignment,
												 vkArena->memTypeIdx,
												 allocFlags,
												 dedicatedAlloc ? &dedicatedAllocateInfo : 0 );
	buffData.mem = bufferMem.deviceMem;
	buffData.hostVisible = ( bufferMem.hostVisible ) ? ( bufferMem.hostVisible + bufferMem.dataOffset ) : 0;
	buffData.size = sizeInBytes;

	VK_CHECK( vkBindBufferMemory( vkArena->device, buffData.hndl, buffData.mem, bufferMem.dataOffset ) );

	if( allocFlags == VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT )
	{
		buffData.devicePointer = VkGetBufferDeviceAddress( vkArena->device, buffData.hndl );
	}

	return buffData;
}
// TODO: better check on resource creation ? 
// TODO: check elsewhere ? 
static image
VkCreateAllocBindImage(
	const VkImageCreateInfo& imgInfo,
	vk_mem_arena* vkArena = &vkAlbumArena,
	VkPhysicalDevice			gpu = dc.gpu
){
	VkFormatFeatureFlags formatFeatures = 0;
	if( imgInfo.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( imgInfo.usage & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, imgInfo.format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );

	image img = {};
	img.nativeFormat = imgInfo.format;
	img.width = imgInfo.extent.width;
	img.height = imgInfo.extent.height;
	img.mipCount = imgInfo.mipLevels;
	img.layerCount = imgInfo.arrayLayers;
	VK_CHECK( vkCreateImage( vkArena->device, &imgInfo, 0, &img.img ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.img;
	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkArena->device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	b32 dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena->memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	VK_CHECK( VK_INTERNAL_ERROR( !( memTypeIdx == vkArena->memTypeIdx ) ) );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc( vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  vkArena->memTypeIdx,
											  0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkArena->device, img.img, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena->device, img.img, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

static image
VkCreateAllocBindImage(
	VkFormat			format,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			extent,
	u32					mipCount,
	vk_mem_arena* vkArena,
	VkImageType			vkImgType = VK_IMAGE_TYPE_2D,
	VkPhysicalDevice	gpu = dc.gpu
){
	VkFormatFeatureFlags formatFeatures = 0;
	if( usageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ) formatFeatures |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
	if( usageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT ) formatFeatures |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
	if( usageFlags & VK_IMAGE_USAGE_SAMPLED_BIT ) formatFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;

	VkFormatProperties formatProps;
	vkGetPhysicalDeviceFormatProperties( gpu, format, &formatProps );
	VK_CHECK( VK_INTERNAL_ERROR( ( formatProps.optimalTilingFeatures & formatFeatures ) != formatFeatures ) );


	image img = {};

	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = vkImgType;
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
	VK_CHECK( vkCreateImage( vkArena->device, &imgInfo, 0, &img.img ) );

	VkImageMemoryRequirementsInfo2 imgReqs2 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2 };
	imgReqs2.image = img.img;

	VkMemoryDedicatedRequirements dedicatedReqs = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS_KHR };
	VkMemoryRequirements2 memReqs2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs };
	vkGetImageMemoryRequirements2( vkArena->device, &imgReqs2, &memReqs2 );

	VkMemoryDedicatedAllocateInfo dedicatedAllocateInfo = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
	dedicatedAllocateInfo.image = imgReqs2.image;

	b32 dedicatedAlloc = dedicatedReqs.prefersDedicatedAllocation || dedicatedReqs.requiresDedicatedAllocation;

#ifdef _VK_DEBUG_
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
	i32 memTypeIdx = VkFindMemTypeIdx( &memProps, vkArena->memTypeProperties, memReqs2.memoryRequirements.memoryTypeBits );
	//VK_CHECK( VK_INTERNAL_ERROR( memTypeIdx == vkArena->memTypeIdx ) );
	assert( memTypeIdx == vkArena->memTypeIdx );
#endif

	vk_allocation imgMem = VkArenaAlignAlloc( vkArena,
											  memReqs2.memoryRequirements.size,
											  memReqs2.memoryRequirements.alignment,
											  vkArena->memTypeIdx,
											  0,
											  dedicatedAlloc ? &dedicatedAllocateInfo : 0 );

	img.mem = imgMem.deviceMem;

	VK_CHECK( vkBindImageMemory( vkArena->device, img.img, img.mem, imgMem.dataOffset ) );

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena->device, img.img, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

// TODO: tewak ? make own ?
// TODO: provide with own allocators
#define WIN32
#include "spirv_reflect.h"
#undef WIN32
// TODO: variable entry point
constexpr char SHADER_ENTRY_POINT[] = "main";

// TODO: cache shader ?
struct vk_shader
{
	VkShaderModule	module;
	std::vector<u8>	spvByteCode;
	// TODO: use this ? or keep hardcoded in MakePipeline func
	//VkShaderStageFlagBits	stage;
	u64				timestamp;
	char			entryPointName[ 32 ];
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
	const vk_descriptor_count&			poolSizes = {} 
){
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

	VkDescriptorPoolCreateInfo descPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
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

	VkDescriptorBindingFlags flags[ std::size( bindlessLayout ) ] = {};
	for( VkDescriptorBindingFlags& f : flags )
		f = VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

	VkDescriptorSetLayoutBindingFlagsCreateInfo descSetFalgs = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	descSetFalgs.bindingCount = std::size( flags );
	descSetFalgs.pBindingFlags = flags;
	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetLayoutInfo.pNext = &descSetFalgs;
	descSetLayoutInfo.bindingCount = std::size( bindlessLayout );
	descSetLayoutInfo.pBindings = bindlessLayout;
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &desc.setLayout ) );


	VkDescriptorSetAllocateInfo descSetInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	descSetInfo.descriptorPool = desc.pool;
	descSetInfo.descriptorSetCount = 1;
	descSetInfo.pSetLayouts = &desc.setLayout;
	VK_CHECK( vkAllocateDescriptorSets( vkDevice, &descSetInfo, &desc.set ) );

	return desc;
}

// TODO: remove
template<typename T>
inline VkWriteDescriptorSet VkMakeBindlessGlobalUpdate( 
	const T*					descInfo, 
	u64							descInfoCount,
	vk_global_descriptor_slot	bindingSlot,
	u64							dstAarryElem = 0,
	const vk_global_descriptor& desc = globBindlessDesc 
){
	VkWriteDescriptorSet update = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	update.dstSet = desc.set;
	update.dstBinding = bindingSlot;
	update.dstArrayElement = dstAarryElem;
	update.descriptorCount = descInfoCount;
	update.descriptorType = globalDescTable[ bindingSlot ];

	if constexpr( std::is_same<T, VkDescriptorBufferInfo>::value )
	{
		update.pBufferInfo = (const VkDescriptorBufferInfo*) descInfo;
	}
	else if constexpr( std::is_same<T, VkDescriptorImageInfo>::value )
	{
		update.pImageInfo = (const VkDescriptorImageInfo*) descInfo;
	}

	return update;
}

// TODO: 
constexpr std::string_view shadersFolder = "D:\\EichenRepos\\QiY\\QiY\\Shaders\\"sv;
constexpr std::string_view shaderExtension = ".spv"sv;

inline static VkShaderModule VkMakeShaderModule( VkDevice vkDevice, const u32* spv, u64 size )
{
	VkShaderModuleCreateInfo shaderModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shaderModuleInfo.codeSize = size;
	shaderModuleInfo.pCode = spv;

	VkShaderModule sm = {};
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &sm ) );
	return sm;
}

// TODO: fuck C++ and vector
inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	std::vector<u8> binSpvShader = SysReadFile( shaderPath );

	vk_shader shader = {};
	shader.spvByteCode = std::move( binSpvShader );
	shader.module = VkMakeShaderModule( vkDevice, 
										  (const u32*) std::data( shader.spvByteCode ), 
										  std::size( shader.spvByteCode ) );
	
	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shadersFolder ) );
	shaderName.remove_suffix( std::size( shaderExtension ) - 1 );
	VkDbgNameObj( vkDevice, VK_OBJECT_TYPE_SHADER_MODULE, (u64) shader.module, &shaderName[ 0 ] );

	return shader;
}

// TODO: rewrite the whole shader pipe system
// TODO: rewrite 
inline static void VkReflectShaderLayout(
	const VkPhysicalDeviceProperties&			gpuProps,
	const std::vector<u8>&						spvByteCode,
	std::vector<VkDescriptorSetLayoutBinding>&	descSetBindings,
	std::vector<VkPushConstantRange>&			pushConstRanges,
	group_size&									gs,
	char*										entryPointName,
	u64											entryPointNameStrLen 
){
	SpvReflectShaderModule shaderReflection;
	VK_CHECK( (VkResult) spvReflectCreateShaderModule( std::size( spvByteCode ) * sizeof( spvByteCode[ 0 ] ),
													   std::data( spvByteCode ),
													   &shaderReflection ) );

	SpvReflectDescriptorSet& set = shaderReflection.descriptor_sets[ 0 ];

	for( u64 bindingIdx = 0; bindingIdx < set.binding_count; ++bindingIdx )
	{
		if( set.set > 0 ) continue;

		const SpvReflectDescriptorBinding& descBinding = *set.bindings[ bindingIdx ];

		if( bindingIdx < std::size( descSetBindings ) )
		{
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

	for( u64 pci = 0; pci < shaderReflection.push_constant_block_count; ++pci )
	{
		VkPushConstantRange pushConstRange = {};
		pushConstRange.offset = shaderReflection.push_constant_blocks[ pci ].offset;
		pushConstRange.size = shaderReflection.push_constant_blocks[ pci ].size;
		// TODO: what about this ?
		pushConstRange.stageFlags = shaderReflection.shader_stage;
		VK_CHECK( VK_INTERNAL_ERROR( pushConstRange.size > gpuProps.limits.maxPushConstantsSize ) );

		pushConstRanges.push_back( pushConstRange );
	}

	assert( shaderReflection.entry_point_count == 1 );
	const SpvReflectEntryPoint& entryPoint = shaderReflection.entry_points[ 0 ];
	assert( std::strlen( entryPoint.name ) <= entryPointNameStrLen );
	std::memcpy( entryPointName, entryPoint.name, entryPointNameStrLen );
	if( VkShaderStageFlags( shaderReflection.shader_stage ) == VK_SHADER_STAGE_COMPUTE_BIT )
		gs = { entryPoint.local_size.x, entryPoint.local_size.y, entryPoint.local_size.z };

	spvReflectDestroyShaderModule( &shaderReflection );
}

// TODO: map spec consts ?
using vk_shader_list = std::initializer_list<vk_shader*>;
using vk_specializations = std::initializer_list<u64>;

// TODO: bindlessLayout only for the shaders that use it ?
vk_program VkMakePipelineProgram(
	VkDevice							vkDevice,
	const VkPhysicalDeviceProperties&	gpuProps,
	VkPipelineBindPoint					bindPoint,
	vk_shader_list						shaders,
	VkDescriptorSetLayout				bindlessLayout = globBindlessDesc.setLayout 
){
	assert( std::size( shaders ) );
	
	vk_program program = {};

	std::vector<VkDescriptorSetLayoutBinding> bindings;
	std::vector<VkPushConstantRange>	pushConstRanges;
	group_size gs = {};
	
	for( vk_shader* s : shaders )
	{
		VkReflectShaderLayout( gpuProps,
							   s->spvByteCode,
							   bindings,
							   pushConstRanges,
							   gs,
							   s->entryPointName,
							   std::size( s->entryPointName ) );
	}
		

	VkDescriptorSetLayoutCreateInfo descSetLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	descSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
	descSetLayoutInfo.bindingCount = std::size( bindings );
	descSetLayoutInfo.pBindings = &bindings[ 0 ];
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &program.descSetLayout ) );

	VkDescriptorSetLayout setLayouts[] = { program.descSetLayout, bindlessLayout };

	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = std::size( setLayouts );
	pipeLayoutInfo.pSetLayouts = setLayouts;
	pipeLayoutInfo.pushConstantRangeCount = std::size( pushConstRanges );
	pipeLayoutInfo.pPushConstantRanges = std::data( pushConstRanges );
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &program.pipeLayout ) );


	std::vector<VkDescriptorUpdateTemplateEntry> entries;
	entries.reserve( std::size( bindings ) );
	entries.resize( 0 );
	for( const VkDescriptorSetLayoutBinding& binding : bindings )
	{
		VkDescriptorUpdateTemplateEntry entry = {};
		entry.dstBinding = binding.binding;
		entry.dstArrayElement = 0;
		entry.descriptorCount = binding.descriptorCount;
		entry.descriptorType = binding.descriptorType;
		entry.offset = std::size( entries ) * sizeof( vk_descriptor_info );
		entry.stride = sizeof( vk_descriptor_info );
		entries.emplace_back( entry );
	}

	VkDescriptorUpdateTemplateCreateInfo templateInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO };
	templateInfo.descriptorUpdateEntryCount = std::size( entries );
	templateInfo.pDescriptorUpdateEntries = &entries[ 0 ];
	templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
	templateInfo.descriptorSetLayout = program.descSetLayout;
	templateInfo.pipelineBindPoint = bindPoint;
	templateInfo.pipelineLayout = program.pipeLayout;
	templateInfo.set = 0;
	VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &program.descUpdateTemplate ) );


	program.pushConstStages = std::size( pushConstRanges ) ? pushConstRanges[ 0 ].stageFlags : 0;
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

inline static VkSpecializationInfo
VkMakeSpecializationInfo(
	std::vector<VkSpecializationMapEntry>& specializations,
	const vk_specializations& consts )
{
	specializations.resize( std::size( consts ) );
	u64 sizeOfASpecConst = sizeof( *std::cbegin( consts ) );
	for( u64 i = 0; i < std::size( consts ); ++i )
		specializations[ i ] = { u32( i ), u32( i * sizeOfASpecConst ), u32( sizeOfASpecConst ) };

	VkSpecializationInfo specInfo = {};
	specInfo.mapEntryCount = std::size( specializations );
	specInfo.pMapEntries = std::data( specializations );
	specInfo.dataSize = std::size( consts ) * sizeOfASpecConst;
	specInfo.pData = std::cbegin( consts );

	return specInfo;
}

// TODO: specialization for gfx ?
// TODO: depth clamp ?
// TODO: entry point name
VkPipeline VkMakeGfxPipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkRenderPass		vkRndPass,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		vs,
	VkShaderModule		fs,
	VkPolygonMode		polyMode = VK_POLYGON_MODE_FILL,
	b32					blendCol = colorBlending,
	b32					depthWrite = true,
	VkCullModeFlags		cullFlags = VK_CULL_MODE_BACK_BIT
){
	VkPipelineShaderStageCreateInfo shaderStagesInfo[ 2 ] = {};
	shaderStagesInfo[ 0 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStagesInfo[ 0 ].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStagesInfo[ 0 ].module = vs;
	shaderStagesInfo[ 0 ].pName = SHADER_ENTRY_POINT;
	shaderStagesInfo[ 1 ].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStagesInfo[ 1 ].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStagesInfo[ 1 ].module = fs;
	shaderStagesInfo[ 1 ].pName = SHADER_ENTRY_POINT;


	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inAsmStateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inAsmStateInfo.primitiveRestartEnable = 0;

	VkPipelineViewportStateCreateInfo viewportInfo = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	viewportInfo.viewportCount = 1;
	viewportInfo.scissorCount = 1;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateInfo.dynamicStateCount = std::size( dynamicStates );
	dynamicStateInfo.pDynamicStates = dynamicStates;

	VkPipelineRasterizationStateCreateInfo rasterInfo = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	rasterInfo.depthClampEnable = 0;
	rasterInfo.rasterizerDiscardEnable = 0;
	rasterInfo.polygonMode = polyMode;

	//rasterInfo.cullMode = worldLeftHanded ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
	rasterInfo.cullMode = cullFlags;
	rasterInfo.frontFace = worldLeftHanded ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;

	rasterInfo.depthBiasEnable = 0;
	rasterInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
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

	VkPipelineColorBlendStateCreateInfo colorBlendStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
	colorBlendStateInfo.logicOpEnable = 0;
	colorBlendStateInfo.attachmentCount = 1;
	colorBlendStateInfo.pAttachments = &blendConfig;


	VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	pipelineInfo.stageCount = std::size( shaderStagesInfo );
	pipelineInfo.pStages = shaderStagesInfo;

	VkPipelineVertexInputStateCreateInfo vtxInCreateInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
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
	vk_specializations	consts,
	const char*			pEntryPointName = SHADER_ENTRY_POINT )
{
	std::vector<VkSpecializationMapEntry> specializations;
	VkSpecializationInfo specInfo = VkMakeSpecializationInfo( specializations, consts );

	VkPipelineShaderStageCreateInfo stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = cs;
	stage.pName = pEntryPointName;
	stage.pSpecializationInfo = &specInfo;

	VkComputePipelineCreateInfo compPipelineInfo = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	compPipelineInfo.stage = stage;
	compPipelineInfo.layout = vkPipelineLayout;

	VkPipeline pipeline = 0;
	VK_CHECK( vkCreateComputePipelines( vkDevice, vkPipelineCache, 1, &compPipelineInfo, 0, &pipeline ) );

	return pipeline;
}


// TODO: stretchy buffer ?
// TODO: remove std::stuff
//#include <functional>
//static vector<std::function<void()>> deviceGlobalDeletionQueue;

//#define VK_APPEND_DESTROYER( VkObjectDestroyerLambda ) deviceGlobalDeletionQueue.push_back( [=](){ VkObjectDestroyer; } )


#ifdef _VK_DEBUG_

#include <iostream>

VKAPI_ATTR VkBool32 VKAPI_CALL 
VkDbgUtilsMsgCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT		msgSeverity,
	VkDebugUtilsMessageTypeFlagsEXT				msgType,
	const VkDebugUtilsMessengerCallbackDataEXT*	callbackData,
	void*										userData 
){
	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT )
	{
		std::string_view msgView = { callbackData->pMessage };
		std::cout << msgView.substr( msgView.rfind( "| " ) + 2 ) << "\n";

		return VK_FALSE;
	}

	if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
	{
		std::cout << ">>> VK_WARNING <<<\n";
	}
	else if( msgSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
	{
		std::cout << ">>> VK_ERROR <<<\n";
	}
	std::cout << callbackData->pMessageIdName << "\n" << callbackData->pMessage << "\n" << "\n";

	return VK_FALSE;
}

#endif


inline static VkSurfaceKHR VkMakeSurfWin32( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
{
#ifdef VK_USE_PLATFORM_WIN32_KHR
	VkWin32SurfaceCreateInfoKHR surfInfo = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
	surfInfo.hinstance = hInst;
	surfInfo.hwnd = hWnd;

	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;

#else
#error Must provide OS specific Surface
#endif // VK_USE_PLATFORM_WIN32_KHR
}

// TODO: separate logical and physical device ?
// TODO: return val ?
inline static void VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf, device* dc )
{
	// TODO: what about integrated devices ?
	constexpr VkPhysicalDeviceType PREFFERED_PHYSICAL_DEVICE_TYPE = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

	constexpr const char* ENABLED_DEVICE_EXTS[] =
	{
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,

		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,

		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
		
		VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	VkPhysicalDeviceSubgroupProperties waveProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
	VkPhysicalDeviceVulkan12Properties gpuProps12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, &waveProps };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps12 };

	// TODO: GPU specific features NV/AMD/whatever?
	VkPhysicalDevice16BitStorageFeatures _16BitStorageFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES };
	VkPhysicalDeviceVulkan12Features gpuFeatures12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &_16BitStorageFeatures };
	VkPhysicalDeviceFeatures2 gpuFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &gpuFeatures12 };

	VkPhysicalDevice gpu = 0;
	for( u64 i = 0; i < numDevices; ++i ){

		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, 0 ) || !extsNum ) continue;
		std::vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, std::data( availableExts ) ) ) continue;

		for( u64 i = 0; i < std::size( ENABLED_DEVICE_EXTS ); ++i )
		{
			b32 foundExt = false;
			for( u64 j = 0; j < extsNum; ++j )
			{
				if( !strcmp( ENABLED_DEVICE_EXTS[ i ], availableExts[ j ].extensionName ) )
				{
					foundExt = true;
					break;
				}
			}
			if( !foundExt ) goto NEXT_DEVICE;

		}

		vkGetPhysicalDeviceProperties2( availableDevices[ i ], &gpuProps2 );
		if( gpuProps2.properties.apiVersion < VK_API_VERSION_1_2 ) continue;
		if( gpuProps2.properties.deviceType != PREFFERED_PHYSICAL_DEVICE_TYPE ) continue;

		vkGetPhysicalDeviceFeatures2( availableDevices[ i ], &gpuFeatures );

		gpu = availableDevices[ i ];

		break;

	NEXT_DEVICE:;
	}
	VK_CHECK( VK_INTERNAL_ERROR( !gpu ) );

	gpuFeatures.features.geometryShader = 0;


	u32 queueFamNum = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, 0 );
	VK_CHECK( VK_INTERNAL_ERROR( !queueFamNum ) );
	std::vector<VkQueueFamilyProperties>  queueFamProps( queueFamNum );
	vkGetPhysicalDeviceQueueFamilyProperties( gpu, &queueFamNum, std::data( queueFamProps ) );

	constexpr VkQueueFlags careAboutQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
	constexpr VkQueueFlags presentQueueFlags = careAboutQueueTypes ^ VK_QUEUE_TRANSFER_BIT;

	u32 qGfxIdx = -1, qCompIdx = -1, qTransfIdx = -1;

	for( u32 qIdx = 0; qIdx < queueFamNum; ++qIdx )
	{
		if( queueFamProps[ qIdx ].queueCount == 0 ) continue;

		VkQueueFlags familyFlags = queueFamProps[ qIdx ].queueFlags & careAboutQueueTypes;
		if( familyFlags & presentQueueFlags )
		{
			VkBool32 present = 0;
			vkGetPhysicalDeviceSurfaceSupportKHR( gpu, qIdx, vkSurf, &present );
			VK_CHECK( VK_INTERNAL_ERROR( !present ) );
		}
		if( familyFlags & VK_QUEUE_GRAPHICS_BIT ) qGfxIdx = qIdx;
		else if( familyFlags & VK_QUEUE_COMPUTE_BIT ) qCompIdx = qIdx;
		else if( familyFlags & VK_QUEUE_TRANSFER_BIT ) qTransfIdx = qIdx;
	}
	VK_CHECK( VK_INTERNAL_ERROR( 
		( qGfxIdx == u32( -1 ) ) || ( qCompIdx == u32( -1 ) ) || ( qTransfIdx == u32( -1 ) ) ) );
	// TODO: less hackyer ?
	assert( std::max( qGfxIdx, std::max( qCompIdx, qTransfIdx ) ) == 2 );
	assert( std::min( qGfxIdx, std::min( qCompIdx, qTransfIdx ) ) == 0 );

	float queuePriorities = 1.0f;
	VkDeviceQueueCreateInfo queueInfos[ 3 ] = {};
	for( u64 qi = 0; qi < std::size( queueInfos ); ++qi )
	{
		queueInfos[ qi ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[ qi ].queueFamilyIndex = qi;
		queueInfos[ qi ].queueCount = 1;
		queueInfos[ qi ].pQueuePriorities = &queuePriorities;
	}
	
	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.pNext = &gpuFeatures;
	deviceInfo.queueCreateInfoCount = std::size( queueInfos );
	deviceInfo.pQueueCreateInfos = &queueInfos[ 0 ];
	deviceInfo.enabledExtensionCount = std::size( ENABLED_DEVICE_EXTS );
	deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS;
	VK_CHECK( vkCreateDevice( gpu, &deviceInfo, 0, &dc->device ) );

	// TODO: move to BackendInit ?
	VkLoadDeviceProcs( dc->device );

	vkGetDeviceQueue( dc->device, queueInfos[ qGfxIdx ].queueFamilyIndex, 0, &dc->gfxQueue );
	vkGetDeviceQueue( dc->device, queueInfos[ qCompIdx ].queueFamilyIndex, 0, &dc->compQueue );
	vkGetDeviceQueue( dc->device, queueInfos[ qTransfIdx ].queueFamilyIndex, 0, &dc->transfQueue );
	VK_CHECK( VK_INTERNAL_ERROR( !dc->gfxQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !dc->compQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !dc->transfQueue ) );

	dc->gfxQueueIdx = queueInfos[ qGfxIdx ].queueFamilyIndex;
	dc->compQueueIdx = queueInfos[ qCompIdx ].queueFamilyIndex;
	dc->transfQueueIdx = queueInfos[ qTransfIdx ].queueFamilyIndex;
	dc->gpu = gpu;
	dc->gpuProps = gpuProps2.properties;
	dc->waveSize = waveProps.subgroupSize;
}

// TODO: sep initial validation form sc creation when resize ?
// TODO: tweak settings/config
// TODO: present from compute and graphics
inline static swapchain
VkMakeSwapchain(
	VkDevice			vkDevice,
	VkPhysicalDevice	vkPhysicalDevice,
	VkSurfaceKHR		vkSurf,
	u32					queueFamIdx,
	VkFormat			scDesiredFormat
){
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
		std::vector<VkSurfaceFormatKHR> formats( formatCount );
		VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( vkPhysicalDevice, vkSurf, &formatCount, std::data( formats ) ) );

		for( u64 i = 0; i < formatCount; ++i )
			if( formats[ i ].format == scDesiredFormat )
			{
				scFormatAndColSpace = formats[ i ];
				break;
			}

		VK_CHECK( VK_INTERNAL_ERROR( !scFormatAndColSpace.format ) );
	}

	VkPresentModeKHR presentMode = VkPresentModeKHR( 0 );
	{
		u32 numPresentModes;
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, 0 ) );
		std::vector<VkPresentModeKHR> presentModes( numPresentModes );
		VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( vkPhysicalDevice, vkSurf, &numPresentModes, std::data( presentModes ) ) );

		constexpr VkPresentModeKHR desiredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		for( u32 j = 0; j < numPresentModes; ++j )
			if( presentModes[ j ] == desiredPresentMode )
			{
				presentMode = desiredPresentMode;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !presentMode ) );
	}


	u32 scImgCount = VK_SWAPCHAIN_MAX_IMG_ALLOWED;
	assert( ( scImgCount > surfaceCaps.minImageCount ) && ( scImgCount < surfaceCaps.maxImageCount ) );
	assert( ( surfaceCaps.currentExtent.width <= surfaceCaps.maxImageExtent.width ) &&
			( surfaceCaps.currentExtent.height <= surfaceCaps.maxImageExtent.height ) );

	VkImageUsageFlags scImgUsage =
		VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT;
	VK_CHECK( VK_INTERNAL_ERROR( ( surfaceCaps.supportedUsageFlags & scImgUsage ) != scImgUsage ) );


	VkSwapchainCreateInfoKHR scInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	scInfo.surface = vkSurf;
	scInfo.minImageCount = scImgCount;
	scInfo.imageFormat = scFormatAndColSpace.format;
	scInfo.imageColorSpace = scFormatAndColSpace.colorSpace;
	scInfo.imageExtent = surfaceCaps.currentExtent;
	assert( surfaceCaps.maxImageArrayLayers >= 1 );
	scInfo.imageArrayLayers = 1;
	scInfo.imageUsage = scImgUsage;
	scInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	scInfo.preTransform = surfaceCaps.currentTransform;
	scInfo.compositeAlpha = surfaceComposite;
	scInfo.presentMode = presentMode;
	scInfo.queueFamilyIndexCount = 1;
	scInfo.pQueueFamilyIndices = &queueFamIdx;
	scInfo.clipped = VK_TRUE;
	scInfo.oldSwapchain = 0;

	VkImageFormatProperties scImageProps = {};
	VK_CHECK( vkGetPhysicalDeviceImageFormatProperties( vkPhysicalDevice,
														scInfo.imageFormat,
														VK_IMAGE_TYPE_2D,
														VK_IMAGE_TILING_OPTIMAL,
														scInfo.imageUsage,
														scInfo.flags,
														&scImageProps ) );


	swapchain sc = {};
	VK_CHECK( vkCreateSwapchainKHR( vkDevice, &scInfo, 0, &sc.swapchain ) );

	u32 scImgsNum = 0;
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, 0 ) );
	VK_CHECK( VK_INTERNAL_ERROR( !( scImgsNum == scInfo.minImageCount ) ) );
	VK_CHECK( vkGetSwapchainImagesKHR( vkDevice, sc.swapchain, &scImgsNum, sc.imgs ) );

	for( u64 i = 0; i < scImgsNum; ++i )
	{
		sc.imgViews[i] = VkMakeImgView( vkDevice, sc.imgs[ i ], scInfo.imageFormat, 0, 1, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );
	}

	sc.width = scInfo.imageExtent.width;
	sc.height = scInfo.imageExtent.height;
	sc.imgCount = scInfo.minImageCount;
	sc.imgFormat = scInfo.imageFormat;

	return sc;
}


// TODO: make general ?
inline static VkRenderPass 
VkMakeRndPass( 
	VkDevice	vkDevice, 
	VkFormat	colorFormat, 
	VkFormat	depthFormat, 
	b32			secondPass 
){
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

	VkRenderPassCreateInfo renderPassInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = std::size( attachmentDescriptions );
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
	u32				height 
){
	VkFramebufferCreateInfo fboInfo = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
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

// TODO: buffer_desc ?
// TODO: revisit
static inline virtual_frame VkCreateVirtualFrame(
	VkDevice		vkDevice,
	u32				exectutionQueueIdx,
	u32				bufferSize,
	vk_mem_arena&	arena
){
	virtual_frame vrtFrame = {};

	VkCommandPoolCreateInfo cmdPoolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	cmdPoolInfo.queueFamilyIndex = exectutionQueueIdx;
	VK_CHECK( vkCreateCommandPool( vkDevice, &cmdPoolInfo, 0, &vrtFrame.cmdPool ) );

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	cmdBuffAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	cmdBuffAllocInfo.commandBufferCount = 1;
	cmdBuffAllocInfo.commandPool = vrtFrame.cmdPool;
	VK_CHECK( vkAllocateCommandBuffers( vkDevice, &cmdBuffAllocInfo, &vrtFrame.cmdBuf ) );

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrame.canGetImgSema ) );
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrame.canPresentSema ) );

	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VK_CHECK( vkCreateFence( vkDevice, &fenceInfo, 0, &vrtFrame.hostSyncFence ) );

	vrtFrame.frameData = VkCreateAllocBindBuffer( bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &arena );

	return vrtFrame;
}

// TODO: more locallity ?
#include "r_data_structs.h"

#include "asset_compiler.h"


inline VkFormat GetVkFormat( texture_format t )
{
	switch( t )
	{
	case TEXTURE_FORMAT_RBGA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
	case TEXTURE_FORMAT_RBGA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	case TEXTURE_FORMAT_BC1_RGB_SRGB: return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
	case TEXTURE_FORMAT_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
	case TEXTURE_FORMAT_UNDEFINED: assert( 0 );
	}
}
inline VkImageType GetVkImgType( texture_type t )
{
	switch( t )
	{
	case TEXTURE_TYPE_1D: return VK_IMAGE_TYPE_1D;
	case TEXTURE_TYPE_2D: return VK_IMAGE_TYPE_2D;
	case TEXTURE_TYPE_3D: return VK_IMAGE_TYPE_3D;
	default: assert( 0 ); return VK_IMAGE_TYPE_MAX_ENUM;
	}
}
// TODO: default types ?
inline VkFilter VkGetFilterTypeFromGltf( gltf_sampler_filter f )
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
inline VkSamplerMipmapMode VkGetMipmapTypeFromGltf( gltf_sampler_filter m )
{
	switch( m )
	{
	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST:
	return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR:
	case GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR:
	default:
	return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}
inline VkSamplerAddressMode VkGetAddressModeFromGltf( gltf_sampler_address_mode a )
{
	switch( a )
	{
	case GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case GLTF_SAMPLER_ADDRESS_MODE_REPEAT: default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}
// TODO: ensure mipmapMode in assetcmpl
// TODO: addrModeW ?
// TODO: more stuff ?
inline VkSamplerCreateInfo VkMakeSamplerInfo( sampler_config config )
{
	assert( 0 );
	VkSamplerCreateInfo vkSamplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	vkSamplerInfo.minFilter = VkGetFilterTypeFromGltf( config.min );
	vkSamplerInfo.magFilter = VkGetFilterTypeFromGltf( config.mag );
	vkSamplerInfo.mipmapMode =  VkGetMipmapTypeFromGltf( config.min );
	vkSamplerInfo.addressModeU = VkGetAddressModeFromGltf( config.addrU );
	vkSamplerInfo.addressModeV = VkGetAddressModeFromGltf( config.addrV );
	//vkSamplerInfo.addressModeW = 

	return vkSamplerInfo;
}

inline VkImageCreateInfo
GetVkImageInfo(
	VkFormat			imgFormat,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			imgExtent,
	u32					mipLevels = 1,
	u32					layerCount = 1,
	VkImageType			imgType = VK_IMAGE_TYPE_2D
){
	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = imgType;
	imgInfo.format = imgFormat;
	imgInfo.extent = imgExtent;
	imgInfo.mipLevels = mipLevels;
	imgInfo.arrayLayers = layerCount;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return imgInfo;
}
inline VkImageCreateInfo GetVkImageInfoFromMetadata( const image_metadata& meta, VkImageUsageFlags	usageFlags )
{
	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = GetVkImgType( meta.type );
	imgInfo.format = GetVkFormat( meta.format );
	imgInfo.extent = { u32( meta.width ),u32( meta.height ),1 };
	imgInfo.mipLevels = meta.mipCount;
	imgInfo.arrayLayers = meta.layerCount;
	imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usageFlags;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	return imgInfo;
}

using namespace DirectX;
// TODO: remove duplicates ?
constexpr float SignNonZero( float e )
{
	return ( e >= 0.0f ) ? 1.0f : -1.0f;
}
constexpr float ConstexprFabs( float n )
{
	return ( n >= 0.0f ) ? n : -n;
}
constexpr vec2 OctaNormalEncode( vec3 n )
{
	// NOTE: Project the sphere onto the octahedron, and then onto the xy plane
	float absLen = ConstexprFabs( n.x ) + ConstexprFabs( n.y ) + ConstexprFabs( n.z );
	float absNorm = ( absLen == 0.0f ) ? 0.0f : 1.0f / absLen;
	float nx = n.x * absNorm;
	float ny = n.y * absNorm;

	// NOTE: Reflect the folds of the lower hemisphere over the diagonals
	float octaX = ( n.z < 0.f ) ? ( 1.0f - ConstexprFabs( ny ) ) * SignNonZero( nx ) : nx;
	float octaY = ( n.z < 0.f ) ? ( 1.0f - ConstexprFabs( nx ) ) * SignNonZero( ny ) : ny;

	return { octaX, octaY };
}
inline u8 FloatToSnorm8( float e )
{
	return std::round( 127.5f + e * 127.5f );
}

static constexpr void ReverseTriangleWinding( u32* indices, u64 count )
{
	assert( count % 3 == 0 );
	for( u64 t = 0; t < count; t += 3 ) std::swap( indices[ t ], indices[ t + 2 ] );
}
// TODO: memory stuff
// TODO: constexpr special file
static void GenerateIcosphere( std::vector<vertex>& vtxData, std::vector<u32>& idxData, u64 numIters )
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

	std::vector<vec3> vtxCache;
	std::vector<u32> idxCache;

	vtxCache = { std::begin( vertices ), std::end( vertices ) };
	idxData = { std::begin( triangles ),std::end( triangles ) };

	//vtxCache.reserve( ICOSAHEDRON_VTX_NUM * std::exp2( numIters ) );
	idxCache.reserve( 3 * ICOSAHEDRON_FACE_NUM * exp2( 2 * numIters ) );
	idxData.reserve( 3 * ICOSAHEDRON_FACE_NUM * exp2( 2 * numIters ) );
	

	for( u64 i = 0; i < numIters; ++i ){
		for( u64 t = 0; t < std::size( idxData ); t += 3 ){
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

			u32 idxOffset = std::size( vtxCache ) - 1;

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
	std::vector<vec3> normals( std::size( vtxCache ), vec3{} );

	for( u64 t = 0; t < std::size( idxData ); t += 3 ){
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

	vtxData.resize( std::size( vtxCache ) );

	for( u64 i = 0; i < std::size( vtxCache ); ++i ){
		vtxData[ i ] = {};
		vtxData[ i ].px = vtxCache[ i ].x;
		vtxData[ i ].py = vtxCache[ i ].y;
		vtxData[ i ].pz = vtxCache[ i ].z;
		vec2 n = OctaNormalEncode( normals[ i ] );
		vtxData[ i ].snorm8octNx = FloatToSnorm8( n.x );
		vtxData[ i ].snorm8octNy = FloatToSnorm8( n.y );
	}
}
constexpr void GenerateBoxCube( std::vector<vertex>& vtx, std::vector<u32>& idx )
{
	constexpr float w = 0.5f;
	constexpr float h = 0.5f;
	constexpr float t = 0.5f;

	vec3 c0 = { w, h,-t };
	vec3 c1 = {-w, h,-t };
	vec3 c2 = {-w,-h,-t };
	vec3 c3 = { w,-h,-t };

	vec3 c4 = { w, h, t };
	vec3 c5 = {-w, h, t };
	vec3 c6 = {-w,-h, t };
	vec3 c7 = { w,-h, t };

	constexpr vec2 u = OctaNormalEncode( { 0,1,0 } );
	constexpr vec2 d = OctaNormalEncode( { 0,-1,0 } );
	constexpr vec2 f = OctaNormalEncode( { 0,0,1 } );
	constexpr vec2 b = OctaNormalEncode( { 0,0,-1 } );
	constexpr vec2 l = OctaNormalEncode( { -1,0,0 } );
	constexpr vec2 r = OctaNormalEncode( { 1,0,0 } );

	vertex vertices[] = {
		// Bottom
		{c0.x,c0.y,c0.z,		0,0,0,		FloatToSnorm8( d.x ),FloatToSnorm8( d.y )},
		{c1.x,c1.y,c1.z,		0,0,0,		FloatToSnorm8( d.x ),FloatToSnorm8( d.y )},
		{c2.x,c2.y,c2.z,		0,0,0,		FloatToSnorm8( d.x ),FloatToSnorm8( d.y )},
		{c3.x,c3.y,c3.z,		0,0,0,		FloatToSnorm8( d.x ),FloatToSnorm8( d.y )},
		// Left					
		{c7.x,c7.y,c7.z,		0,0,0,		FloatToSnorm8( l.x ),FloatToSnorm8( l.y )},
		{c4.x,c4.y,c4.z,		0,0,0,		FloatToSnorm8( l.x ),FloatToSnorm8( l.y )},
		{c0.x,c0.y,c0.z,		0,0,0,		FloatToSnorm8( l.x ),FloatToSnorm8( l.y )},
		{c3.x,c3.y,c3.z,		0,0,0,		FloatToSnorm8( l.x ),FloatToSnorm8( l.y )},
		// Front				
		{c4.x,c4.y,c4.z,		0,0,0,		FloatToSnorm8( f.x ),FloatToSnorm8( f.y )},
		{c5.x,c5.y,c5.z,		0,0,0,		FloatToSnorm8( f.x ),FloatToSnorm8( f.y )},
		{c1.x,c1.y,c1.z,		0,0,0,		FloatToSnorm8( f.x ),FloatToSnorm8( f.y )},
		{c0.x,c0.y,c0.z,		0,0,0,		FloatToSnorm8( f.x ),FloatToSnorm8( f.y )},
		// Back					
		{c6.x,c6.y,c6.z,		0,0,0,		FloatToSnorm8( b.x ),FloatToSnorm8( b.y )},
		{c7.x,c7.y,c7.z,		0,0,0,		FloatToSnorm8( b.x ),FloatToSnorm8( b.y )},
		{c3.x,c3.y,c3.z,		0,0,0,		FloatToSnorm8( b.x ),FloatToSnorm8( b.y )},
		{c2.x,c2.y,c2.z,		0,0,0,		FloatToSnorm8( b.x ),FloatToSnorm8( b.y )},
		// Right				
		{c5.x,c5.y,c5.z,		0,0,0,		FloatToSnorm8( r.x ),FloatToSnorm8( r.y )},
		{c6.x,c6.y,c6.z,		0,0,0,		FloatToSnorm8( r.x ),FloatToSnorm8( r.y )},
		{c2.x,c2.y,c2.z,		0,0,0,		FloatToSnorm8( r.x ),FloatToSnorm8( r.y )},
		{c1.x,c1.y,c1.z,		0,0,0,		FloatToSnorm8( r.x ),FloatToSnorm8( r.y )},
		// Top					
		{c7.x,c7.y,c7.z,		0,0,0,		FloatToSnorm8( u.x ),FloatToSnorm8( u.y )},
		{c6.x,c6.y,c6.z,		0,0,0,		FloatToSnorm8( u.x ),FloatToSnorm8( u.y )},
		{c5.x,c5.y,c5.z,		0,0,0,		FloatToSnorm8( u.x ),FloatToSnorm8( u.y )},
		{c4.x,c4.y,c4.z,		0,0,0,		FloatToSnorm8( u.x ),FloatToSnorm8( u.y )},
	};

	u32 indices[] = {
			0, 1, 3,        1, 2, 3,        // Bottom	
			4, 5, 7,        5, 6, 7,        // Left
			8, 9, 11,       9, 10, 11,      // Front
			12, 13, 15,     13, 14, 15,     // Back
			16, 17, 19,     17, 18, 19,	    // Right
			20, 21, 23,     21, 22, 23	    // Top
	};

	if constexpr( worldLeftHanded ) ReverseTriangleWinding( indices, std::size( indices ) );

	vtx.insert( std::end( vtx ), std::begin( vertices ), std::end( vertices ) );
	idx.insert( std::end( idx ), std::begin( indices ), std::end( indices ) );
}

// TODO: pass buff_data ?
inline VkBufferMemoryBarrier
VkMakeBufferBarrier( 
	VkBuffer		hBuff,
	VkAccessFlags	srcAccess, 
	VkAccessFlags	dstAccess,
	VkDeviceSize	buffOffset = 0,
	VkDeviceSize	buffSize = VK_WHOLE_SIZE,
	u32				srcQueueFamIdx = VK_QUEUE_FAMILY_IGNORED,
	u32				dstQueueFamIdx = VK_QUEUE_FAMILY_IGNORED 
){
	VkBufferMemoryBarrier memBarrier = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
	memBarrier.srcAccessMask = srcAccess;
	memBarrier.dstAccessMask = dstAccess;
	memBarrier.srcQueueFamilyIndex = srcQueueFamIdx;
	memBarrier.dstQueueFamilyIndex = dstQueueFamIdx;
	memBarrier.buffer = hBuff;
	memBarrier.offset = buffOffset;
	memBarrier.size = buffSize;

	return memBarrier;
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

static buffer_data bdasUboBuff;

static buffer_data vertexBuff;
// TODO: use indirect merged index buffer
static buffer_data indexBuff;
static buffer_data meshBuff;


static buffer_data meshletBuff;
// TODO: store u8x4 vtx in here ?
static buffer_data meshletVtxBuff;
static buffer_data meshletTrisBuff;


static buffer_data materialsBuff;
static buffer_data instDescBuff;
static buffer_data lightsBuff;


static buffer_data dispatchCmdBuff;

static buffer_data drawIdxBuff;

static buffer_data avgLumBuff;
static buffer_data shaderGlobalsBuff;
static buffer_data shaderGlobalSyncCounterBuff;

static buffer_data drawCmdBuff;
static buffer_data drawCountBuff;

static buffer_data drawCmdDbgBuff;
static buffer_data drawCountDbgBuff;

static buffer_data drawVisibilityBuff;

static buffer_data depthAtomicCounterBuff;


constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "D:\\EichenRepos\\QiY\\QiY\\Assets\\cyberbaron.drak";
//constexpr char glbPath[] = "WaterBottle.glb";


template<typename T>
struct hndl32
{
	u32 h = 0;

	hndl32() = default;
	inline hndl32( u32 magkIdx ) : h{ magkIdx }{}
	inline operator u32() const { return h; }
};

template <typename T>
inline u64 IdxFromHndl32( hndl32<T> h )
{
	constexpr u32 standardIndexMask = ( 1 << 16 ) - 1;
	return h & standardIndexMask;
}
template <typename T>
inline u64 MagicFromHndl32( hndl32<T> h )
{
	constexpr u32 standardIndexMask = ( 1 << 16 ) - 1;
	constexpr u32 standardMagicNumberMask = ~standardIndexMask;
	return ( h & standardMagicNumberMask ) >> 16;
}
template <typename T>
inline hndl32<T> Hndl32FromMagicAndIdx( u64 m, u64 i )
{
	return u32( ( m << 16 ) | i );
}

// TODO: handled container
// TODO: extract magicId from resources ?
template<typename T>
struct resource_container
{
	std::vector<T> rsc;
	u32 magicCounter;
};

template<typename T>
inline const T& GetResourceFromHndl( hndl32<T> h, const resource_container<T>& buf )
{
	assert( std::size( buf ) );
	assert( h );

	const T& entry = buf[ IdxFromHndl32( h ) ];
	assert( entry.magicId == MagicFromHndl32( h ) );

	return entry;
}
template<typename T>
inline hndl32<T> PushResourceToContainer( T& rsc, resource_container<T>& buf )
{
	u64 magicCounter = buf.magicCounter++;
	rsc.magicId = magicCounter;
	buf.rsc.push_back( rsc );

	return Hndl32FromMagicAndIdx<T>( magicCounter, std::size( buf.rsc ) );
}


static resource_container<image> textures;

// TODO: recycle_queue
// TODO: rethink
// TODO: async 
// TODO: MT 
struct staging_manager
{
	struct upload_job
	{
		VkBuffer	buf;
		u64			frameId;
	};
	std::vector<upload_job>		pendingUploads;
	VkSemaphore					timelineUploadSema;
	u64							semaSignalCounter;
};

static staging_manager stagingManager;

inline static staging_manager 
CreateInitStagingManager( VkDevice vkDevice )
{
	staging_manager stgMngr = {};
	
	VkSemaphoreTypeCreateInfo timelineInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
	timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	timelineInfo.initialValue = 0;
	VkSemaphoreCreateInfo timelineSemaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineInfo };
	VK_CHECK( vkCreateSemaphore( vkDevice, &timelineSemaInfo, 0, &stgMngr.timelineUploadSema ) );
}
inline static void
StagingManagerPushForRecycle( VkBuffer stagingBuf, staging_manager& stgMngr )
{
	stgMngr.pendingUploads.push_back( { stagingBuf,stgMngr.semaSignalCounter } );
}

inline static void
RecordTextureUploads(
	VkCommandBuffer			cmdBuf,
	VkBuffer				srcBuf,
	const std::span<image>	dstImgs,
	const std::vector<u64>& copyOffsets
){
	std::vector<VkImageMemoryBarrier> barriers;
	barriers.reserve( std::size( dstImgs ) );
	for( const image& i : dstImgs )
	{
		barriers.push_back( VkMakeImgBarrier( i.img, 0,
											  VK_ACCESS_TRANSFER_WRITE_BIT,
											  VK_IMAGE_LAYOUT_UNDEFINED,
											  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
											  VK_IMAGE_ASPECT_COLOR_BIT ) );
	}
	vkCmdPipelineBarrier( cmdBuf,
						  VK_PIPELINE_STAGE_HOST_BIT,
						  VK_PIPELINE_STAGE_TRANSFER_BIT,
						  0, 0, 0, 0, 0,
						  std::size( barriers ), std::data( barriers ) );

	for( u64 i = 0; i < std::size( dstImgs ); ++i )
	{
		const image& dst = dstImgs[ i ];

		VkBufferImageCopy imgCopyRegion = {};
		imgCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		imgCopyRegion.bufferOffset = copyOffsets[ i ];
		imgCopyRegion.imageSubresource.mipLevel = 0;
		imgCopyRegion.imageSubresource.baseArrayLayer = 0;
		imgCopyRegion.imageSubresource.layerCount = 1;
		imgCopyRegion.imageOffset = VkOffset3D{};
		imgCopyRegion.imageExtent = { u32( dst.width ),u32( dst.height ),1 };

		vkCmdCopyBufferToImage( cmdBuf, srcBuf, dst.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );

		barriers[ i ] = VkMakeImgBarrier( dst.img,
										  VK_ACCESS_TRANSFER_WRITE_BIT,
										  VK_ACCESS_SHADER_READ_BIT,
										  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
										  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
										  VK_IMAGE_ASPECT_COLOR_BIT );
	}
	vkCmdPipelineBarrier( cmdBuf,
						  VK_PIPELINE_STAGE_TRANSFER_BIT,
						  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
						  0, 0, 0, 0, 0,
						  std::size( barriers ), std::data( barriers ) );
}

inline static void
RemakeWholeTextureDescriptors(
	const resource_container<image>&	textures,
	const vk_global_descriptor&			desc,
	std::vector<VkDescriptorImageInfo>& texDescInfo,
	VkWriteDescriptorSet&				writeTexUpdate
){
	std::vector<VkDescriptorImageInfo> texDesc;
	texDesc.reserve( std::size( textures.rsc ) );
	for( const image& i : textures.rsc )
	{
		texDesc.push_back( { 0, i.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } );
	}

	VkWriteDescriptorSet texUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	texUpdate.dstSet = globBindlessDesc.set;
	texUpdate.dstBinding = VK_GLOBAL_SLOT_SAMPLED_IMAGE;
	texUpdate.dstArrayElement = 0;
	texUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];
	texUpdate.descriptorCount = std::size( texDesc );
	texUpdate.pImageInfo = std::data( texDesc );

	texDescInfo = std::move( texDesc );
	writeTexUpdate = texUpdate;
}

// TODO:
inline static void VkInitInternalBuffers()
{
	avgLumBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &vkRscArena );
	shaderGlobalsBuff =
		VkCreateAllocBindBuffer( 64, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &vkRscArena );
	shaderGlobalSyncCounterBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &vkRscArena );
	drawCountBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &vkRscArena );
	VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) drawCountBuff.hndl, "Buff_Draw_Count" );
	drawCountDbgBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, &vkRscArena );
	VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) drawCountDbgBuff.hndl, "Buff_Dbg_Draw_Count" );
	// TODO: no transfer bit ?
	depthAtomicCounterBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, &vkRscArena );

	dispatchCmdBuff =
		VkCreateAllocBindBuffer( sizeof( dispatch_command ),
								 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
								 &vkRscArena );

	bdasUboBuff = VkCreateAllocBindBuffer( sizeof( global_bdas ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &vkHostComArena );
}

constexpr double RAND_MAX_SCALE = 1.0 / double( RAND_MAX );
// TODO: remove
inline static std::vector<instance_desc> SpawnRandomInstances( u64 drawCount, u64 meshCount, u64 mtrlCount, float sceneRadius )
{
	assert( mtrlCount == 1 );
	std::vector<instance_desc> insts( drawCount );
	float scale = 1.0f;
	for( instance_desc& i : insts )
	{
		i.meshIdx = rand() % meshCount;
		i.mtrlIdx = 0;
		//i.bndVolMeshIdx = 1;
		i.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.scale = scale * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		//scale *= 0.001f;

		DirectX::XMVECTOR axis = DirectX::XMVector3Normalize(
			DirectX::XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
								  float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
								  float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
								  0 ) );
		float angle = DirectX::XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		DirectX::XMVECTOR quat = DirectX::XMQuaternionRotationNormal( axis, angle );
		DirectX::XMStoreFloat4( &i.rot, quat );
	}
	// TODO: draw from transparent only pipe ?
	if constexpr( 0 )
	{
		instance_desc& i = insts[ std::size( insts ) - 1 ];
		i.meshIdx = 2;

		i.pos.x = 0;
		i.pos.y = 0;
		i.pos.z = -50.0f;
		i.scale = 100.0f;

		DirectX::XMVECTOR quat = DirectX::XMQuaternionIdentity();
		DirectX::XMStoreFloat4( &i.rot, quat );
	}

	return insts;
}
inline static std::vector<light_data> SpawnRandomLights( u64 lightCount, float sceneRadius)
{
	constexpr b32 drawLightDbgSphere = 0;

	std::vector<light_data> lights( lightCount );
	for( light_data& l : lights )
	{
		l.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.radius = 100.0f * float( rand() * RAND_MAX_SCALE ) + 2.0f;
		l.col = { 600.0f,200.0f,100.0f };
		//if constexpr( drawLightDbgSphere )
		//{
		//	draw_data lightBoundigShpere = {};
		//	lightBoundigShpere.pos = l.pos;
		//	lightBoundigShpere.scale = l.radius;
		//	DirectX::XMStoreFloat4( &lightBoundigShpere.rot, DirectX::XMQuaternionIdentity() );
		//
		//	lightBoundigShpere.meshIdx = meshCount;
		//	drawArgs.push_back( lightBoundigShpere );
		//}
	}

	return lights;
}

// TODO: revisit remake improve
inline static void VkUploadResources( VkCommandBuffer cmdBuf )
{
	std::vector<u8> binaryData;
	// TODO: extra checks and stuff ?
	// TODO: add data offset and use that
	// TODO: ensure SoA layout of data
	// TODO: ensure resources of the same type are contiguous ?
	{
		binaryData = SysReadFile( drakPath );
		if( std::size( binaryData ) == 0 )
		{
			std::vector<u8> fileData = SysReadFile( glbPath );
			CompileGlbAssetToBinary( fileData, binaryData );
			SysWriteToFile( drakPath, std::data( binaryData ), std::size( binaryData ) );
		}
	}
	u64 offset = sizeof( drak_file_header );
	const drak_file_desc& fileDesc = *(drak_file_desc*) ( std::data( binaryData ) + offset );
	offset += sizeof( drak_file_desc );

	const std::span<binary_mesh_desc> meshDesc = { (binary_mesh_desc*) ( std::data( binaryData ) + offset ),fileDesc.meshesCount };
	offset += BYTE_COUNT( meshDesc );
	// TODO - must manage ?
	const std::span<material_data> mtrlDesc = { (material_data*) ( std::data( binaryData ) + offset ),fileDesc.mtrlsCount };
	offset += BYTE_COUNT( mtrlDesc );
	const std::span<image_metadata> imgDesc = { (image_metadata*) ( std::data( binaryData ) + offset ),fileDesc.texCount };

	const std::span<vertex> vtxView =
	{ (vertex*) std::data( binaryData ) + fileDesc.dataOffset + fileDesc.vtxRange.offset,fileDesc.vtxRange.size };

	std::vector<mesh_desc> meshes;
	// TODO: must make mesh and binary_mesh_desc the same
	for( const binary_mesh_desc& m : meshDesc )
	{
		meshes.push_back( {} );
		mesh_desc& out = meshes[ std::size( meshes ) - 1 ];
		out.vertexCount = m.vtxRange.size;
		out.vertexOffset = m.vtxRange.offset;
		
		assert( m.lodCount <= u8( -1 ) );
		out.lodCount = m.lodCount;
		for( u64 i = 0; i < m.lodCount; ++i )
		{
			out.lods[ i ].indexCount = m.lodRanges[ i ].size;
			out.lods[ i ].indexOffset = m.lodRanges[ i ].offset;
		}

		XMFLOAT3 center;
		XMFLOAT3 extent;
		{
			XMVECTOR xmm0 = XMLoadFloat3( (const XMFLOAT3*) &m.aabbMinMax[ 0 ] );
			XMVECTOR xmm1 = XMLoadFloat3( (const XMFLOAT3*) &m.aabbMinMax[ 3 ] );

			XMStoreFloat3( &center, XMVectorScale( XMVectorAdd( xmm1, xmm0 ), 0.5f ) );
			xmm0 = XMVectorScale( XMVectorSubtract( xmm1, xmm0 ), 0.5f );
			XMStoreFloat3( &extent, xmm0 );
		}
		out.center = center;
		out.extent = extent;
	}

	assert( std::size( textures.rsc ) == 0 );
	std::vector<material_data> mtrls = {};
	for( const material_data& m : mtrlDesc )
	{
		mtrls.push_back( m );
		material_data& refM = mtrls[ std::size( mtrls ) - 1 ];
		refM.baseColIdx += std::size( textures.rsc );
		refM.normalMapIdx += std::size( textures.rsc );
		refM.occRoughMetalIdx += std::size( textures.rsc );
	}

	u64 meshCount = std::size( meshes );
	constexpr u64 randSeed = 42;
	constexpr u64 drawCount = 10;
	constexpr u64 lightCount = 4;
	constexpr float sceneRadius = 40.0f;
	std::srand( randSeed );

	std::vector<instance_desc> instDesc = SpawnRandomInstances( drawCount, meshCount, 1, sceneRadius );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRadius );

	// NOTE - upload buffers
	std::vector<VkBufferMemoryBarrier> buffBarriers;
	{
		const u8* pVtxData = std::data( binaryData ) + fileDesc.dataOffset + fileDesc.vtxRange.offset;
		u64 copyDataSize = fileDesc.vtxRange.size;

		vertexBuff = VkCreateAllocBindBuffer( copyDataSize,
											  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											  &vkRscArena );
		VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) vertexBuff.hndl, "Buff_Vtx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, pVtxData, stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuf, stagingBuf.hndl, vertexBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier( vertexBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT ) );
	}
	{
		const u8* pIdxData = std::data( binaryData ) + fileDesc.dataOffset + fileDesc.idxRange.offset;
		u64 copyDataSize = fileDesc.idxRange.size;

		indexBuff = VkCreateAllocBindBuffer( copyDataSize,
											 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
											 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											 &vkRscArena );
		VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) indexBuff.hndl, "Buff_Idx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, pIdxData, stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuf, stagingBuf.hndl, indexBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier( indexBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT ) );
	}
	{
		u64 copyDataSize = BYTE_COUNT( meshes );

		meshBuff = VkCreateAllocBindBuffer( copyDataSize,
											VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											&vkRscArena );
		VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) meshBuff.hndl, "Buff_Mesh_Desc" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( meshes ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuf, stagingBuf.hndl, meshBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier( meshBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT ) );
	}
	{
		u64 copyDataSize = BYTE_COUNT( lights );

		lightsBuff = VkCreateAllocBindBuffer( copyDataSize,
											  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											  &vkRscArena );
		VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) lightsBuff.hndl, "Buff_Lights" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( lights ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuf, stagingBuf.hndl, lightsBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier( lightsBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT ) );
	}
	{
		u64 copyDataSize = BYTE_COUNT( instDesc );

		instDescBuff = VkCreateAllocBindBuffer( copyDataSize,
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												&vkRscArena );
		VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) instDescBuff.hndl, "Buff_Inst_Descs" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( instDesc ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuf, stagingBuf.hndl, instDescBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier( instDescBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT ) );
	}
	{
		u64 copyDataSize = BYTE_COUNT( mtrls );

		materialsBuff = VkCreateAllocBindBuffer( copyDataSize,
												 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												 &vkRscArena );
		VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) materialsBuff.hndl, "Buff_Mtrls" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( mtrls ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuf, stagingBuf.hndl, materialsBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier( materialsBuff.hndl, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT ) );
	}

	vkCmdPipelineBarrier( cmdBuf,
						  VK_PIPELINE_STAGE_TRANSFER_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
						  0, 0, 0,
						  std::size( buffBarriers ), std::data( buffBarriers ), 0, 0 );

	
	drawCmdBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( draw_command ),
										   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
										   &vkRscArena );
	drawCmdDbgBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( draw_indirect ),
											  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
											  &vkRscArena );
	VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) drawCmdDbgBuff.hndl, "Buff_Indirect_Dbg_Draw_Cmds" );

	// TODO: use per wave stuff ?
	drawVisibilityBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( u32 ),
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												  &vkRscArena );
	VkDbgNameObj( dc.device, VK_OBJECT_TYPE_BUFFER, (u64) drawVisibilityBuff.hndl, "Buff_Draw_Vis" );

	// NOTE - create and texture uploads
	{
		const u8* pTexBinData = std::data( binaryData ) + fileDesc.dataOffset + fileDesc.texRange.offset;

		std::vector<u64> copyOffsets;
		u64 newTexturesOffset = std::size( textures.rsc );
		for( const image_metadata& meta : imgDesc )
		{
			VkImageCreateInfo vkImgInfo =
				GetVkImageInfoFromMetadata( meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
			image img = VkCreateAllocBindImage( vkImgInfo );

			hndl32<image> hImg = PushResourceToContainer( img, textures );
			copyOffsets.push_back( meta.texBinRange.offset );
		}

		const std::span<image> newTextures = { std::data( textures.rsc ) + newTexturesOffset,std::size( imgDesc ) };

		buffer_data stagingBuff = VkCreateAllocBindBuffer( fileDesc.texRange.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &vkStagingArena );
		std::memcpy( stagingBuff.hostVisible, pTexBinData, stagingBuff.size );
		StagingManagerPushForRecycle( stagingBuff.hndl, stagingManager );

		RecordTextureUploads( cmdBuf, stagingBuff.hndl, newTextures, copyOffsets );
	}
}
// TODO: compact sampler thing ?
// TODO: lod clamp
inline VkSampler VkMakeSampler( 
	VkDevice				vkDevice, 
	float					lodCount = 1.0f, 
	VkSamplerReductionMode	reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX_ENUM,
	VkFilter				filter = VK_FILTER_LINEAR,
	VkSamplerAddressMode	addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
	VkSamplerMipmapMode		mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST 
){
	VkSamplerReductionModeCreateInfo reduxInfo = { VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO };
	reduxInfo.reductionMode = reductionMode;

	VkSamplerCreateInfo samplerInfo = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.pNext = ( reductionMode == VK_SAMPLER_REDUCTION_MODE_MAX_ENUM ) ? 0 : &reduxInfo;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipmapMode = mipmapMode;
	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.minLod = 0;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

	VkSampler sampler;
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


static u64			VK_DLL = 0;

// TODO: move out of global/static
static vk_program	gfxOpaqueProgram = {};
static vk_program	debugGfxProgram = {};
static vk_program	drawcullCompProgram = {};
static vk_program	depthPyramidCompProgram = {};
static vk_program	avgLumCompProgram = {};
static vk_program	tonemapCompProgram = {};
static vk_program	depthPyramidMultiProgram = {};

// TODO: find better soln
enum shader_idx : u8
{
	VERT_BINDLESS,
	VERT_BOX_DBG, 
	FRAG_PBR,
	COMP_CULL,
	COMP_HIZ_MIPS,
	COMP_HIZ_MIPS_POW2,
	COMP_LUMINANCE,
	COMP_TONE_GAMMA,
	FRAG_NORMAL_COL
};

constexpr char* shaderFiles[] =
{
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\shdr.vert.glsl.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\box_debug.vert.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\pbr.frag.glsl.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\draw_cull.comp.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\depth_pyramid.comp.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\pow2_downsampler.comp.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\avg_luminance.comp.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\tonemap_gamma.comp.spv",
	"D:\\EichenRepos\\QiY\\QiY\\Shaders\\normal_col.frag.spv"
};


// TODO: gfx_api_instance ?
struct vk_instance
{
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMsg;
};
// TODO: turn on sync validation and fix errors
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
		"VK_LAYER_KHRONOS_validation",
		//"VK_LAYER_LUNARG_api_dump"
	#endif // _VK_DEBUG_
	};

	VK_CHECK( VK_INTERNAL_ERROR( !( VK_DLL = SysDllLoad( "vulkan-1.dll" ) ) ) );

	// TODO: full vk file generation ?
#define GET_VK_GLOBAL_PROC( VkProc ) VkProc = (PFN_##VkProc) vkGetInstanceProcAddr( 0, #VkProc )

	vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) SysGetProcAddr( VK_DLL, "vkGetInstanceProcAddr" );
	GET_VK_GLOBAL_PROC( vkCreateInstance );
	GET_VK_GLOBAL_PROC( vkEnumerateInstanceExtensionProperties );
	GET_VK_GLOBAL_PROC( vkEnumerateInstanceLayerProperties );
	GET_VK_GLOBAL_PROC( vkEnumerateInstanceVersion );
#undef GET_VK_GLOBAL_PROC

	u32 vkExtsNum = 0;
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, 0 ) );
	std::vector<VkExtensionProperties> givenExts( vkExtsNum );
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, std::data( givenExts ) ) );
	for( u32 i = 0; i < std::size( ENABLED_INST_EXTS ); ++i )
	{
		b32 foundExt = false;
		for( u32 j = 0; j < vkExtsNum; ++j )
			if( !strcmp( ENABLED_INST_EXTS[ i ], givenExts[ j ].extensionName ) )
			{
				foundExt = true;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !foundExt ) );
	};

	u32 layerCount = 0;
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, 0 ) );
	std::vector<VkLayerProperties> layersAvailable( layerCount );
	VK_CHECK( vkEnumerateInstanceLayerProperties( &layerCount, std::data( layersAvailable ) ) );
	for( u32 i = 0; i < std::size( LAYERS ); ++i )
	{
		b32 foundLayer = false;
		for( u32 j = 0; j < layerCount; ++j )
			if( !strcmp( LAYERS[ i ], layersAvailable[ j ].layerName ) )
			{
				foundLayer = true;
				break;
			}
		VK_CHECK( VK_INTERNAL_ERROR( !foundLayer ) );
	}

	VkInstance vkInstance = 0;
	VkDebugUtilsMessengerEXT vkDbgUtilsMsgExt = 0;

	VkApplicationInfo appInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	VK_CHECK( vkEnumerateInstanceVersion( &appInfo.apiVersion ) );
	VK_CHECK( VK_INTERNAL_ERROR( appInfo.apiVersion < VK_API_VERSION_1_2 ) );

	VkInstanceCreateInfo instInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
#ifdef _VK_DEBUG_

	VkValidationFeatureEnableEXT enabled[] = { 
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
	};

	VkValidationFeaturesEXT vkValidationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	vkValidationFeatures.enabledValidationFeatureCount = std::size( enabled );
	vkValidationFeatures.pEnabledValidationFeatures = enabled;

	VkDebugUtilsMessengerCreateInfoEXT vkDbgExt = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
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
	instInfo.enabledLayerCount = std::size( LAYERS );
	instInfo.ppEnabledLayerNames = LAYERS;
	instInfo.enabledExtensionCount = std::size( ENABLED_INST_EXTS );
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

	sc = VkMakeSwapchain( dc.device,dc.gpu, vkSurf, dc.gfxQueueIdx, VK_FORMAT_B8G8R8A8_UNORM );

	rndCtx.renderPass = VkMakeRndPass( dc.device, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat, false );
	rndCtx.render2ndPass = VkMakeRndPass( dc.device, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat, true );

	VkPipelineCache pipelineCache = 0;

	std::vector<vk_shader> shaders( std::size( shaderFiles ) );
	for( u64 i = 0; i < std::size( shaders ); ++i ) shaders[ i ] = VkLoadShader( shaderFiles[ i ], dc.device );


	globBindlessDesc = VkMakeBindlessGlobalDescriptor( dc.device, dc.gpuProps );

	gfxOpaqueProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS,
											  { &shaders[ VERT_BINDLESS ], &shaders[ FRAG_PBR ] } );
	drawcullCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE,
												 { &shaders[ COMP_CULL ] } );
	depthPyramidCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, 
													 { &shaders[ COMP_HIZ_MIPS ] } );
	avgLumCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, 
											   { &shaders[ COMP_LUMINANCE ] } );
	tonemapCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, 
												{ &shaders[ COMP_TONE_GAMMA ] } );


	rndCtx.gfxPipeline = 
		VkMakeGfxPipeline( dc.device, pipelineCache, rndCtx.renderPass, gfxOpaqueProgram.pipeLayout, 
						   shaders[ VERT_BINDLESS ].module, shaders[ FRAG_PBR ].module );
	rndCtx.compPipeline =
		VkMakeComputePipeline( dc.device, pipelineCache, drawcullCompProgram.pipeLayout,
							   shaders[ COMP_CULL ].module, { OBJ_CULL_WORKSIZE,occlusionCullingPass } );

	if constexpr( !multiShaderDepthPyramid )
	{
		rndCtx.compHiZPipeline =
			VkMakeComputePipeline( dc.device, pipelineCache, depthPyramidCompProgram.pipeLayout, 
								   shaders[ COMP_HIZ_MIPS ].module, {} );
	}
	else
	{
		depthPyramidMultiProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, 
														  { &shaders[ COMP_HIZ_MIPS_POW2 ] } );
		rndCtx.compHiZPipeline = 
			VkMakeComputePipeline( dc.device, pipelineCache, depthPyramidMultiProgram.pipeLayout, 
								   shaders[ COMP_HIZ_MIPS_POW2 ].module, {} );
	}


	debugGfxProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, 
											 { &shaders[ VERT_BOX_DBG ], &shaders[ FRAG_NORMAL_COL ] } );
	rndCtx.gfxBVDbgDrawPipeline = VkMakeGfxPipeline( dc.device,
													 pipelineCache,
													 rndCtx.renderPass,
													 debugGfxProgram.pipeLayout,
													 shaders[ VERT_BOX_DBG ].module,
													 shaders[ FRAG_NORMAL_COL ].module,
													 VK_POLYGON_MODE_LINE,//VK_POLYGON_MODE_FILL,
													 1, 0, VK_CULL_MODE_NONE );

	rndCtx.compAvgLumPipe = 
		VkMakeComputePipeline( dc.device, pipelineCache, avgLumCompProgram.pipeLayout, 
							   shaders[ COMP_LUMINANCE ].module, { dc.waveSize } );
	rndCtx.compTonemapPipe =
		VkMakeComputePipeline( dc.device, pipelineCache, tonemapCompProgram.pipeLayout, 
							   shaders[ COMP_TONE_GAMMA ].module, {} );

	for( vk_shader& s : shaders ) vkDestroyShaderModule( dc.device, s.module, 0 );

	for( u64 vfi = 0; vfi < rndCtx.framesInFlight; ++vfi )
	{
		rndCtx.vrtFrames[ vfi ] = VkCreateVirtualFrame( dc.device, dc.gfxQueueIdx, 1 * MB, vkHostComArena );
	}

	VkInitInternalBuffers();
}

inline u64 VkGetGroupCount( u64 invocationCount, u64 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}

#include <intrin.h>
// TODO: math_uitl file
inline u64 FloorPowOf2( u64 size )
{
	// NOTE: use Hacker's Delight for bit-tickery
	constexpr u64 ONE_LEFT_MOST = u64( 1ULL << ( sizeof( u64 ) * 8 - 1 ) );
	return ( size ) ? ONE_LEFT_MOST >> __lzcnt64( size ) : 0;
}
inline u64 GetImgMipCountForPow2( u64 width, u64 height )
{
	// NOTE: log2 == position of the highest bit set (or most significant bit set, MSB)
	// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogObvious
	constexpr u64 TYPE_BIT_COUNT = sizeof( u64 ) * 8 - 1;

	u64 maxDim = std::max( width, height );
	assert( IsPowOf2( maxDim ) );
	u64 log2MaxDim = TYPE_BIT_COUNT - __lzcnt64( maxDim );

	return std::min( log2MaxDim, MAX_MIP_LEVELS );
}
inline u64 GetImgMipCount( u64 width, u64 height )
{
	assert( width && height );
	u64 maxDim = std::max( width, height );

	return std::min( (u64) floor( log2( maxDim ) ), MAX_MIP_LEVELS );
}

// TODO: pass dpethPyr directly
inline static void 
CullPass( 
	VkCommandBuffer			cmdBuff, 
	VkPipeline				vkPipeline, 
	const vk_program&		program,
	const cam_frustum&		camFrust,
	u64						pyramidMipLevels 
){
	vk_label label = { cmdBuff,"Cull Pass",{} };

	cull_info cullInfo = {};
	cullInfo.planes[ 0 ] = camFrust.planes[ 0 ];
	cullInfo.planes[ 1 ] = camFrust.planes[ 1 ];
	cullInfo.planes[ 2 ] = camFrust.planes[ 2 ];
	cullInfo.planes[ 3 ] = camFrust.planes[ 3 ];
	
	cullInfo.frustum[ 0 ] = camFrust.frustum[ 0 ];
	cullInfo.frustum[ 1 ] = camFrust.frustum[ 1 ];
	cullInfo.frustum[ 2 ] = camFrust.frustum[ 2 ];
	cullInfo.frustum[ 3 ] = camFrust.frustum[ 3 ];

	cullInfo.zNear = camFrust.zNear;
	cullInfo.drawDistance = camFrust.drawDistance;
	cullInfo.projWidth = camFrust.projWidth;
	cullInfo.projHeight = camFrust.projHeight;

	cullInfo.pyramidWidthPixels = float( rndCtx.depthPyramid.width );
	cullInfo.pyramidHeightPixels = float( rndCtx.depthPyramid.height );

	cullInfo.drawCallsCount = instDescBuff.size / sizeof( instance_desc );
	

	VkBufferMemoryBarrier drawCountResetBarriers[] = {
		VkMakeBufferBarrier( drawCmdBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT ),
		VkMakeBufferBarrier( 
			drawCountBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT ),

		VkMakeBufferBarrier( dispatchCmdBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT ),
		
		VkMakeBufferBarrier( drawCmdDbgBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT ),
		VkMakeBufferBarrier( 
			drawCountDbgBuff.hndl, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT )
	};

	// TODO: should use img layout readonly optimal
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
						  std::size( drawCountResetBarriers ),
						  drawCountResetBarriers,
						  1, &depthPyramidReadBarrier );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

	VkDescriptorImageInfo depthPyramidInfo = { rndCtx.linearMinSampler, rndCtx.depthPyramid.view, VK_IMAGE_LAYOUT_GENERAL };
	vk_descriptor_info drawcullDescs[] = {  
		drawCmdBuff.descriptor(),
		drawCountBuff.descriptor(), 
		drawVisibilityBuff.descriptor(), 
		dispatchCmdBuff.descriptor(),
		depthPyramidInfo, 
		drawCmdDbgBuff.descriptor(),
		drawCountDbgBuff.descriptor() 
	};
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, drawcullDescs );
	
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( cullInfo ), &cullInfo );

	vkCmdDispatch( cmdBuff, VkGetGroupCount( cullInfo.drawCallsCount, program.groupSize.localSizeX ), 1, 1 );

	VkBufferMemoryBarrier drawCmdReadyBarriers[] = {
		VkMakeBufferBarrier( drawCmdBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
		VkMakeBufferBarrier( drawCountBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),

		VkMakeBufferBarrier( dispatchCmdBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),

		VkMakeBufferBarrier( drawCmdDbgBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
		VkMakeBufferBarrier( drawCountDbgBuff.hndl, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT ),
	};

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, 0, 0, 0,
						  std::size( drawCmdReadyBarriers ),
						  drawCmdReadyBarriers, 0, 0 );
}

// TODO: pass lights buff differently
inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&		drawCmds,
	VkBuffer				drawCmdCount,
	const VkClearValue*		clearVals,
	const vk_program&		program,
	b32						dbgCam
){
	vk_label label = { cmdBuff,"DrawIndirect Pass",{} };

	VkViewport viewport = { 0, (float) sc.height, (float) sc.width, -(float) sc.height, 0, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;
	rndPassBegInfo.clearValueCount = clearVals ? 2 : 0;
	rndPassBegInfo.pClearValues = clearVals;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetViewport( cmdBuff, 0, 1, &viewport );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

	vk_descriptor_info descriptors[] = { instDescBuff.descriptor(), drawCmds.descriptor() };
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );
	// TODO: multiple push const stages ?
	//vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( dbgCam ), &dbgCam );

	vkCmdBindIndexBuffer( cmdBuff, indexBuff.hndl, 0, VK_INDEX_TYPE_UINT32 );

	u32 maxDrawCount = instDescBuff.size / sizeof( instance_desc );
	vkCmdDrawIndexedIndirectCount( 
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCmdCount, 0, maxDrawCount, sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}

// TODO: sexier
inline static void
DebugDrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&		drawCmds,
	VkBuffer				drawCmdCount,
	const vk_program&		program,
	b32						dbgCam
){
	vk_label label = { cmdBuff,"Dbg Draw Indirect Pass",{} };

	VkViewport viewport = { 0, (float) sc.height, (float) sc.width, -(float) sc.height, 0, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetViewport( cmdBuff, 0, 1, &viewport );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

	vk_descriptor_info descriptors[] = { drawCmds.descriptor() };// , meshBuff.descriptor(), instDescBuff.descriptor()};
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

	//vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( dbgCam ), &dbgCam );

	u32 maxDrawCnt = instDescBuff.size / sizeof( instance_desc );
	vkCmdDrawIndirectCount( 
		cmdBuff, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCmdCount, 0, maxDrawCnt, sizeof( draw_indirect ) );
	
	vkCmdEndRenderPass( cmdBuff );
}


inline static void
DepthPyramidPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	u64						mipLevelsCount,
	VkSampler				linearMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const vk_program&		program 
){
	u32 dispatchGroupX = ( ( depthTarget.width + 63 ) >> 6 );
	u32 dispatchGroupY = ( ( depthTarget.height + 63 ) >> 6 );

	downsample_info dsInfo = {};
	dsInfo.mips = mipLevelsCount;
	dsInfo.invRes.x = 1.0f / float( depthTarget.width );
	dsInfo.invRes.y = 1.0f / float( depthTarget.height );
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

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	std::vector<vk_descriptor_info> depthPyramidDescs( MAX_MIP_LEVELS + 3 );
	depthPyramidDescs[ 0 ] = { 0, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };;
	depthPyramidDescs[ 1 ] = { linearMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 2 ] = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	for( u64 i = 0; i < rndCtx.depthPyramid.mipCount; ++i )
	{
		depthPyramidDescs[ i + 3 ] = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
	}

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, std::data( depthPyramidDescs ) );

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
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkExtent2D				depthTargetExt,
	u64						mipLevelsCount,
	VkSampler				linearMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const vk_program&		program 
){
	vk_label label = { cmdBuff,"HiZ Multi Pass",{} };

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
						  std::size( depthReadBarriers ), depthReadBarriers );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	VkDescriptorImageInfo sourceDepth = { linearMinSampler, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	for( u64 i = 0; i < mipLevelsCount; ++i )
	{
		if( i != 0 ) sourceDepth = { linearMinSampler, depthMips[ i - 1 ], VK_IMAGE_LAYOUT_GENERAL };

		VkDescriptorImageInfo destDepth = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
		vk_descriptor_info descriptors[] = { destDepth, sourceDepth };

		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

		u32 levelWidth = std::max( 1u, depthTargetExt.width >> i );
		u32 levelHeight = std::max( 1u, depthTargetExt.height >> i );

		vec2 reduceData = {};
		reduceData.x = levelWidth;
		reduceData.y = levelHeight;

		vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( reduceData ), &reduceData );

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

inline static void
ToneMappingWithSrgb(
	VkCommandBuffer		cmdBuff,
	VkPipeline			avgPipe,
	VkPipeline			tonePipe,
	const vk_program&	avgProg,
	const image&		fboHdrColTrg,
	const vk_program&	tonemapProg,
	VkImage				scImg,
	VkImageView			scView,
	float				dt 
){
	vk_label label = { cmdBuff,"AvgLum && Tonemap Pass",{} };
	// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
	avg_luminance_info avgLumInfo = {};
	avgLumInfo.minLogLum = -10.0f;
	avgLumInfo.invLogLumRange = 1.0f / 12.0f;
	avgLumInfo.dt = dt;

	VkImageMemoryBarrier hdrColTrgAcquireBarrier = VkMakeImgBarrier( fboHdrColTrg.img,
																	 0,
																	 VK_ACCESS_SHADER_READ_BIT,
																	 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
																	 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
																	 VK_IMAGE_ASPECT_COLOR_BIT,
																	 0, 0 );

	// AVERAGE LUM
	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
						  1, &hdrColTrgAcquireBarrier );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, avgPipe );

	VkDescriptorImageInfo hdrColTrgInfo = { 0, fboHdrColTrg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	vk_descriptor_info avgLumDescs[] = { 
		hdrColTrgInfo, 
		avgLumBuff.descriptor(), 
		shaderGlobalsBuff.descriptor(),
		shaderGlobalSyncCounterBuff.descriptor() 
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, avgProg.descUpdateTemplate, avgProg.pipeLayout, 0, &avgLumDescs[ 0 ] );

	vkCmdPushConstants( cmdBuff, avgProg.pipeLayout, avgProg.pushConstStages, 0, sizeof( avgLumInfo ), &avgLumInfo );

	vkCmdDispatch( cmdBuff, 
				   VkGetGroupCount( fboHdrColTrg.width, avgProg.groupSize.localSizeX ), 
				   VkGetGroupCount( fboHdrColTrg.height, avgProg.groupSize.localSizeY ), 
				   1 );

	// TONEMAPPING w/ GAMMA sRGB
	VkImageMemoryBarrier scDestAcquireBarrier = VkMakeImgBarrier( scImg, 0,
																  VK_ACCESS_SHADER_WRITE_BIT,
																  VK_IMAGE_LAYOUT_UNDEFINED,
																  VK_IMAGE_LAYOUT_GENERAL,
																  VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0,
						  1, &scDestAcquireBarrier );

	VkBufferMemoryBarrier avgLumAcquireBarrier = VkMakeBufferBarrier( avgLumBuff.hndl,
																	  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
																	  VK_ACCESS_SHADER_READ_BIT );

	vkCmdPipelineBarrier( cmdBuff,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 
						  1, &avgLumAcquireBarrier, 0, 0 );



	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, tonePipe );

	VkDescriptorImageInfo sdrColScInfo = { 0, scView,VK_IMAGE_LAYOUT_GENERAL };
	vk_descriptor_info tonemapDescs[] = {
		hdrColTrgInfo,
		sdrColScInfo,
		avgLumBuff.descriptor()
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, 
										   tonemapProg.descUpdateTemplate, 
										   tonemapProg.pipeLayout, 0,
										   &tonemapDescs[ 0 ] );

	assert( ( fboHdrColTrg.width == sc.width ) && ( fboHdrColTrg.height == sc.height ) );
	vkCmdDispatch( cmdBuff,
				   VkGetGroupCount( fboHdrColTrg.width, avgProg.groupSize.localSizeX ),
				   VkGetGroupCount( fboHdrColTrg.height, avgProg.groupSize.localSizeY ),
				   1 );

}

static void HostFrames( const global_data* globs, const cam_frustum& camFrust, b32 bvDraw, b32 freeCam, float dt )
{
	//u64 timestamp = SysGetFileTimestamp( "D:\\EichenRepos\\QiY\\QiY\\Shaders\\pbr.frag.glsl" );

	u64 currentFrameIdx = rndCtx.vFrameIdx;
	const virtual_frame& currentVFrame = rndCtx.vrtFrames[ rndCtx.vFrameIdx ];
	rndCtx.vFrameIdx = ( rndCtx.vFrameIdx + 1 ) % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;
	assert( ( currentFrameIdx == 0 ) || ( currentFrameIdx == 1 ) );


	VK_CHECK( VK_INTERNAL_ERROR( ( 
		vkWaitForFences( dc.device, 1, &currentVFrame.hostSyncFence, true, 1'000'000'000 ) - VK_TIMEOUT ) > 0 ) );
	VK_CHECK( vkResetFences( dc.device, 1, &currentVFrame.hostSyncFence ) );

	VK_CHECK( vkResetCommandPool( dc.device, currentVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

	u32 imgIdx;
	VK_CHECK( vkAcquireNextImageKHR( dc.device, sc.swapchain, UINT64_MAX, currentVFrame.canGetImgSema, 0, &imgIdx ) );

	// TODO: swapchain resize ?
	if( !rndCtx.offscreenFbo )
	{
		if( !rndCtx.depthTarget.img )
		{
			rndCtx.depthTarget = VkCreateAllocBindImage( rndCtx.desiredDepthFormat,
														 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
														 { sc.width,sc.height,1 }, 1,
														 &vkAlbumArena );
			u32 hiZWidth = 0;
			u32 hiZHeight = 0;
			u32 hiZMipCount = 0;
			// TODO: place depth pyr elsewhere 
			if constexpr( !multiShaderDepthPyramid )
			{
				// TODO: make conservative ?
				hiZWidth = ( sc.width ) / 2;
				hiZHeight = ( sc.height ) / 2;
				hiZMipCount = GetImgMipCount( hiZWidth, hiZHeight );
			}
			else
			{
				hiZWidth = FloorPowOf2( sc.width );
				hiZHeight = FloorPowOf2( sc.height );
				hiZMipCount = GetImgMipCountForPow2( hiZWidth, hiZHeight );
			}
			VK_CHECK( VK_INTERNAL_ERROR( !( hiZMipCount < MAX_MIP_LEVELS ) ) );
			VkImageUsageFlags hiZUsg = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			VkFormat depthFormat = VK_FORMAT_R32_SFLOAT;
			rndCtx.depthPyramid = VkCreateAllocBindImage( depthFormat, hiZUsg,{ hiZWidth, hiZHeight, 1 }, hiZMipCount, &vkAlbumArena );
			VkDbgNameObj( dc.device, VK_OBJECT_TYPE_IMAGE, (u64) rndCtx.depthPyramid.img, "Img_Depth_Pyramid" );

			for( u64 i = 0; i < rndCtx.depthPyramid.mipCount; ++i )
			{
				rndCtx.depthPyramidChain[ i ] = 
					VkMakeImgView( dc.device, rndCtx.depthPyramid.img, rndCtx.depthPyramid.nativeFormat, i, 1 );
			}

			rndCtx.linearMinSampler = VkMakeSampler( dc.device, rndCtx.depthPyramid.mipCount, VK_SAMPLER_REDUCTION_MODE_MIN );
		}

		if( !rndCtx.colorTarget.img )
		{
			VkImageUsageFlags colorTargetUsg =
				VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			rndCtx.colorTarget = 
				VkCreateAllocBindImage( rndCtx.desiredColorFormat, colorTargetUsg, { sc.width,sc.height,1 }, 1, &vkAlbumArena );
		}

		VkImageView fboAttach[] = { rndCtx.colorTarget.view, rndCtx.depthTarget.view };
		rndCtx.offscreenFbo = VkMakeFramebuffer( dc.device, rndCtx.renderPass, fboAttach, std::size( fboAttach ), sc.width, sc.height );
	}
	

	std::memcpy( currentVFrame.frameData.hostVisible, (u8*) globs, sizeof( *globs ) );
	VkDescriptorBufferInfo uboInfo = { currentVFrame.frameData.hndl, 0, sizeof( *globs ) };
	VkWriteDescriptorSet globalDataUpdate = VkMakeBindlessGlobalUpdate( &uboInfo, 1, VK_GLOBAL_SLOT_UNIFORM_BUFFER );
	vkUpdateDescriptorSets( dc.device, 1, &globalDataUpdate, 0, 0 );

	VkCommandBufferBeginInfo cmdBufBegInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	cmdBufBegInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( currentVFrame.cmdBuf, &cmdBufBegInfo );

	// TODO: async, multi-threaded, etc
	static b32 rescUploaded = 0;
	if( !rescUploaded )
	{
		VkUploadResources( currentVFrame.cmdBuf );
		rescUploaded = 1;

		global_bdas bdas = {};
		bdas.vtxAddr = vertexBuff.devicePointer;
		bdas.idxAddr = indexBuff.devicePointer;
		bdas.meshDescAddr = meshBuff.devicePointer;
		bdas.lightsDescAddr = lightsBuff.devicePointer;
		bdas.mtrlsAddr = materialsBuff.devicePointer;
		bdas.instDescAddr = instDescBuff.devicePointer;

		assert( bdasUboBuff.hostVisible );
		std::memcpy( bdasUboBuff.hostVisible, &bdas, sizeof( bdas ) );


		// NOTE - UpdateDescriptors
		std::vector<VkWriteDescriptorSet> descUpdates;

		VkDescriptorBufferInfo bdaDesc = bdasUboBuff.descriptor();
		VkWriteDescriptorSet bdaUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		bdaUpdate.dstSet = globBindlessDesc.set;
		bdaUpdate.dstBinding = VK_GLOBAL_SLOT_UNIFORM_BUFFER;
		bdaUpdate.dstArrayElement = 1;
		bdaUpdate.descriptorCount = 1;
		bdaUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_UNIFORM_BUFFER ];
		bdaUpdate.pBufferInfo = &bdaDesc;

		descUpdates.push_back( bdaUpdate );


		rndCtx.linearTextureSampler =
			VkMakeSampler( dc.device, 1, VK_SAMPLER_REDUCTION_MODE_MAX_ENUM, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );
		VkDescriptorImageInfo samplerDesc = { rndCtx.linearTextureSampler };
		VkWriteDescriptorSet samplerUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		samplerUpdate.dstSet = globBindlessDesc.set;
		samplerUpdate.dstBinding = VK_GLOBAL_SLOT_SAMPLER;
		samplerUpdate.dstArrayElement = 0;
		samplerUpdate.descriptorCount = 1;
		samplerUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLER ];
		samplerUpdate.pImageInfo = &samplerDesc;

		descUpdates.push_back( samplerUpdate );

		std::vector<VkDescriptorImageInfo> texDescs;
		VkWriteDescriptorSet texUpdates = {};
		RemakeWholeTextureDescriptors( textures, globBindlessDesc, texDescs, texUpdates );
		descUpdates.push_back( texUpdates );

		vkUpdateDescriptorSets( dc.device, std::size( descUpdates ), std::data( descUpdates ), 0, 0 );
	}

	static b32 clearedBuffers = 0;
	if( !clearedBuffers )
	{
		vkCmdFillBuffer( currentVFrame.cmdBuf, drawVisibilityBuff.hndl, 0, drawVisibilityBuff.size, 1U );
		vkCmdFillBuffer( currentVFrame.cmdBuf, depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size, 0 );

		VkBufferMemoryBarrier clearedVisibilityBarrier[] = {
			VkMakeBufferBarrier( drawVisibilityBuff.hndl,
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

	VkImageMemoryBarrier renderBeginBarriers[] = {
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
						  std::size( renderBeginBarriers ),
						  renderBeginBarriers );


	CullPass( currentVFrame.cmdBuf, rndCtx.compPipeline, drawcullCompProgram, camFrust, rndCtx.depthPyramid.mipCount );

	VkClearValue clearVals[ 2 ] = {};
	//clearVals[ 0 ].color = {};
	//clearVals[ 1 ].depthStencil = {};
	DrawIndirectPass( currentVFrame.cmdBuf,
					  rndCtx.gfxPipeline,
					  rndCtx.renderPass,
					  rndCtx.offscreenFbo,
					  drawCmdBuff,
					  drawCountBuff.hndl,
					  clearVals,
					  gfxOpaqueProgram,
					  freeCam );

	
	if constexpr( !multiShaderDepthPyramid )
	{
		DepthPyramidPass( currentVFrame.cmdBuf,
						  rndCtx.compHiZPipeline,
						  rndCtx.depthPyramid.mipCount,
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
			{ rndCtx.depthPyramid.width, rndCtx.depthPyramid.height },
			rndCtx.depthPyramid.mipCount,
			rndCtx.linearMinSampler,
			rndCtx.depthPyramidChain,
			rndCtx.depthTarget,
			depthPyramidMultiProgram );
	}

	if( boundingVolDbgDraw && bvDraw )
	{
		DebugDrawIndirectPass( currentVFrame.cmdBuf,
							   rndCtx.gfxBVDbgDrawPipeline,
							   rndCtx.render2ndPass,
							   rndCtx.offscreenFbo,
							   drawCmdDbgBuff,
							   drawCountDbgBuff.hndl,
							   debugGfxProgram,
							   freeCam );
	}


	ToneMappingWithSrgb( currentVFrame.cmdBuf, 
						 rndCtx.compAvgLumPipe, 
						 rndCtx.compTonemapPipe,
						 avgLumCompProgram, 
						 rndCtx.colorTarget,
						 tonemapCompProgram,
						 sc.imgs[ imgIdx ],
						 sc.imgViews[ imgIdx ],
						 dt );

	VkImageMemoryBarrier presentBarrier = VkMakeImgBarrier( sc.imgs[ imgIdx ],
															VK_ACCESS_SHADER_WRITE_BIT, 0,
															VK_IMAGE_LAYOUT_GENERAL,
															VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
															VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 );

	vkCmdPipelineBarrier( currentVFrame.cmdBuf, 
						  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
						  VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
						  VK_DEPENDENCY_BY_REGION_BIT, 0, 0, 0, 0, 1, 
						  &presentBarrier );

	VK_CHECK( vkEndCommandBuffer( currentVFrame.cmdBuf ) );

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &currentVFrame.canGetImgSema;
	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_TRANSFER_BIT; // VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.pWaitDstStageMask = &waitDstStageMsk;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &currentVFrame.cmdBuf;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &currentVFrame.canPresentSema;
	VK_CHECK( vkQueueSubmit( dc.gfxQueue, 1, &submitInfo, currentVFrame.hostSyncFence ) );

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &currentVFrame.canPresentSema;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &sc.swapchain;
	presentInfo.pImageIndices = &imgIdx;
	VK_CHECK( vkQueuePresentKHR( dc.gfxQueue, &presentInfo ) );
}

static void VkBackendKill()
{
	// NOTE: SHOULDN'T need to check if(VkObj) can't create -> app fail
	assert( dc.device );
	vkDeviceWaitIdle( dc.device );
	//for( auto& queued : deviceGlobalDeletionQueue ) queued();
	//deviceGlobalDeletionQueue.clear();
	

	vkDestroyDevice( dc.device, 0 );
#ifdef _VK_DEBUG_
	vkDestroyDebugUtilsMessengerEXT( vkInst, vkDbgMsg, 0 );
#endif
	vkDestroySurfaceKHR( vkInst, vkSurf, 0 );
	vkDestroyInstance( vkInst, 0 );

	//SysDllUnload( VK_DLL );
}

#undef VK_APPEND_DESTROYER
#undef VK_CHECK

