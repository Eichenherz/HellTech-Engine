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

// NOTE: clang-cl on VS issue
#ifdef __clang__
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>

#endif

#include <DirectXPackedVector.h>

namespace DXPacked = DirectX::PackedVector;

#include "sys_os_api.h"
#include "core_lib_api.h"

// TODO: gen from VkResult ?
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

//====================CONSTS====================//
constexpr u32 VK_SWAPCHAIN_MAX_IMG_ALLOWED = 3;
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
constexpr u64 VK_MIN_DEVICE_BLOCK_SIZE = 256 * MB;
constexpr u64 MAX_MIP_LEVELS = 12;
constexpr u32 NOT_USED_IDX = -1;
constexpr u32 OBJ_CULL_WORKSIZE = 64;
constexpr u32 MLET_CULL_WORKSIZE = 256;
//==============================================//
// TODO: cvars
//====================CVARS====================//
static bool colorBlending = 0;
//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//
constexpr bool multiPassDepthPyramid = 1;
static_assert( multiPassDepthPyramid );
// TODO: enable gfx debug outside of VS Debug
constexpr bool vkValidationLayerFeatures = 1;
constexpr bool worldLeftHanded = 1;
constexpr bool objectNaming = 1;
// TODO: cvar or constexpr ?
constexpr bool dbgDraw = true;
//==============================================//

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
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;
	if_same_type( VkSurfaceKHR ) return VK_OBJECT_TYPE_SURFACE_KHR;

	assert( 0 );
	return VK_OBJECT_TYPE_UNKNOWN;

#undef if_same_type
}

template<typename VKH>
inline void VkDbgNameObj( VKH vkHandle, VkDevice vkDevice, const char* name )
{
	if constexpr( !objectNaming ) return;

	static_assert( sizeof( vkHandle ) == sizeof( u64 ) );
	VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
	nameInfo.objectType = VkGetObjTypeFromHandle<VKH>();
	nameInfo.objectHandle = (u64) vkHandle;
	nameInfo.pObjectName = name;

	VK_CHECK( vkSetDebugUtilsObjectNameEXT( vkDevice, &nameInfo ) );
}

// TODO: remove ?
static const DXPacked::XMCOLOR white = { 255u, 255u, 255u, 1 };
static const DXPacked::XMCOLOR black = { 0u, 0u, 0u, 1 };
static const DXPacked::XMCOLOR gray = { 0x80u, 0x80u, 0x80u, 1 };
static const DXPacked::XMCOLOR lightGray = { 0xD3u, 0xD3u, 0xD3u, 1 };
static const DXPacked::XMCOLOR red = { 255u, 0u, 0u, 1 };
static const DXPacked::XMCOLOR green = { 0u, 255u, 0u, 1 };
static const DXPacked::XMCOLOR blue = { 0u, 0u, 255u, 1 };
static const DXPacked::XMCOLOR yellow = { 255u, 255u, 0u, 1 };
static const DXPacked::XMCOLOR cyan = { 0u, 255u, 255u, 1 };
static const DXPacked::XMCOLOR magenta = { 255u, 0u, 255u, 1 };


// TODO: degub-on-off
struct vk_label
{
	const VkCommandBuffer& cmdBuff;

	inline vk_label( const VkCommandBuffer& cmdBuff, const char* labelName, DXPacked::XMCOLOR col )
		: cmdBuff{ cmdBuff }
	{
		assert( cmdBuff );

		VkDebugUtilsLabelEXT dbgLabel = { VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
		dbgLabel.pLabelName = labelName;
		dbgLabel.color[ 0 ] = col.r;
		dbgLabel.color[ 1 ] = col.g;
		dbgLabel.color[ 2 ] = col.b;
		dbgLabel.color[ 3 ] = col.a;
		vkCmdBeginDebugUtilsLabelEXT( cmdBuff, &dbgLabel );
	}
	inline ~vk_label()
	{
		assert( cmdBuff );
		vkCmdEndDebugUtilsLabelEXT( cmdBuff );
	}
};

// TODO: separate GPU and logical device ?
// TODO: physical_device_info ?
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

inline static device VkMakeDeviceContext( VkInstance vkInst, VkSurfaceKHR vkSurf )
{
	constexpr VkPhysicalDeviceType PREFFERED_PHYSICAL_DEVICE_TYPE = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

	constexpr const char* ENABLED_DEVICE_EXTS[] = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME,

		//VK_KHR_PRESENT_ID_EXTENSION_NAME,
		//VK_KHR_PRESENT_WAIT_EXTENSION_NAME,

		VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,

		VK_KHR_ZERO_INITIALIZE_WORKGROUP_MEMORY_EXTENSION_NAME,

		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,

		VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME,
		VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME,

		VK_EXT_CONSERVATIVE_RASTERIZATION_EXTENSION_NAME,
		VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
		VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME
	};

	u32 numDevices = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, 0 ) );
	std::vector<VkPhysicalDevice> availableDevices( numDevices );
	VK_CHECK( vkEnumeratePhysicalDevices( vkInst, &numDevices, std::data( availableDevices ) ) );

	VkPhysicalDeviceSubgroupProperties waveProps = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
	VkPhysicalDeviceVulkan12Properties gpuProps12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES, &waveProps };
	VkPhysicalDeviceProperties2 gpuProps2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &gpuProps12 };


	VkPhysicalDeviceIndexTypeUint8FeaturesEXT uint8IdxFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
	VkPhysicalDeviceZeroInitializeWorkgroupMemoryFeaturesKHR zeroInitWorkgrMemFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ZERO_INITIALIZE_WORKGROUP_MEMORY_FEATURES_KHR, &uint8IdxFeatures };
	VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = 
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR, &zeroInitWorkgrMemFeatures };
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extDynamicStateFeatures =
	{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT, &sync2Features };
	VkPhysicalDeviceVulkan12Features gpuFeatures12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &extDynamicStateFeatures };
	// NOTE: Vulkan SDK 1.2.189 complains about this stuff , wtf ?
	VkPhysicalDeviceVulkan11Features gpuFeatures11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, &gpuFeatures12 };
	VkPhysicalDeviceFeatures2 gpuFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &gpuFeatures11 };

	VkPhysicalDevice gpu = 0;
	for( u64 i = 0; i < numDevices; ++i )
	{
		u32 extsNum = 0;
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, 0 ) || !extsNum ) continue;
		std::vector<VkExtensionProperties> availableExts( extsNum );
		if( vkEnumerateDeviceExtensionProperties( availableDevices[ i ], 0, &extsNum, std::data( availableExts ) ) ) continue;

		for( u64 i = 0; i < std::size( ENABLED_DEVICE_EXTS ); ++i )
		{
			bool foundExt = false;
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
	// NOTE: might help debugging ?
	gpuFeatures.features.robustBufferAccess = 0;


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
	
	VK_CHECK( VK_INTERNAL_ERROR( ( qGfxIdx == u32( -1 ) ) || ( qCompIdx == u32( -1 ) ) || ( qTransfIdx == u32( -1 ) ) ) );
	
	float queuePriorities = 1.0f;
	VkDeviceQueueCreateInfo queueInfos[ 3 ] = {};
	for( u32 qi = 0; qi < std::size( queueInfos ); ++qi )
	{
		queueInfos[ qi ].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfos[ qi ].queueFamilyIndex = qi;
		queueInfos[ qi ].queueCount = 1;
		queueInfos[ qi ].pQueuePriorities = &queuePriorities;
	}

	device dc = {};

	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.pNext = &gpuFeatures;
	deviceInfo.queueCreateInfoCount = std::size( queueInfos );
	deviceInfo.pQueueCreateInfos = &queueInfos[ 0 ];
	deviceInfo.enabledExtensionCount = std::size( ENABLED_DEVICE_EXTS );
	deviceInfo.ppEnabledExtensionNames = ENABLED_DEVICE_EXTS;
	VK_CHECK( vkCreateDevice( gpu, &deviceInfo, 0, &dc.device ) );

	
	VkLoadDeviceProcs( dc.device );

	vkGetDeviceQueue( dc.device, queueInfos[ qGfxIdx ].queueFamilyIndex, 0, &dc.gfxQueue );
	vkGetDeviceQueue( dc.device, queueInfos[ qCompIdx ].queueFamilyIndex, 0, &dc.compQueue );
	vkGetDeviceQueue( dc.device, queueInfos[ qTransfIdx ].queueFamilyIndex, 0, &dc.transfQueue );
	VK_CHECK( VK_INTERNAL_ERROR( !dc.gfxQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !dc.compQueue ) );
	VK_CHECK( VK_INTERNAL_ERROR( !dc.transfQueue ) );

	dc.gfxQueueIdx = queueInfos[ qGfxIdx ].queueFamilyIndex;
	dc.compQueueIdx = queueInfos[ qCompIdx ].queueFamilyIndex;
	dc.transfQueueIdx = queueInfos[ qTransfIdx ].queueFamilyIndex;
	dc.gpu = gpu;
	dc.gpuProps = gpuProps2.properties;
	dc.waveSize = waveProps.subgroupSize;

	return dc;
}


static device dc;


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
){
	for( u64 memIdx = 0; memIdx < pVkMemProps->memoryTypeCount; ++memIdx )
	{
		u32 memTypeBits = ( 1 << memIdx );
		bool isRequiredMemType = memTypeBitsRequirement & memTypeBits;

		VkMemoryPropertyFlags props = pVkMemProps->memoryTypes[ memIdx ].propertyFlags;
		bool hasRequiredProps = ( props & requiredProps ) == requiredProps;
		if( isRequiredMemType && hasRequiredProps ) return (i32) memIdx;
	}

	VK_CHECK( VK_INTERNAL_ERROR( "Memory type unmatch !" ) );

	return -1;
}

// TODO: move alloc flags ?
// NOTE: NV driver bug not allow HOST_VISIBLE + VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT for uniforms
inline static VkDeviceMemory
VkTryAllocDeviceMem(
	VkDevice								vkDevice,
	u64										size,
	u32										memTypeIdx,
	VkMemoryAllocateFlags					allocFlags,
	const VkMemoryDedicatedAllocateInfo*	dedicated
){
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

inline static vk_mem_arena
VkMakeMemoryArena(
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

// TODO: revisit
inline void
VkArenaTerimate( vk_mem_arena* vkArena )
{
	for( u64 i = 0; i < std::size( vkArena->mem ); ++i )
		vkFreeMemory( vkArena->device, vkArena->mem[ i ].device, 0 );
	for( u64 i = 0; i < std::size( vkArena->dedicatedAllocs ); ++i )
		vkFreeMemory( vkArena->device, vkArena->dedicatedAllocs[ i ].device, 0 );
}

// TODO: move debug stuff out of here ?
inline static void
VkStartGfxMemory( VkPhysicalDevice vkPhysicalDevice, VkDevice vkDevice )
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties( vkPhysicalDevice, &memProps );

	vkRscArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkDevice );
	vkAlbumArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkDevice );
	vkStagingArena = 
		VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vkDevice );
	vkHostComArena = VkMakeMemoryArena( memProps,
										VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
										VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
										VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
										vkDevice );

	vkDbgArena = VkMakeMemoryArena( memProps, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vkDevice );
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
// TODO: keep usage flags ?
// TODO: add buffer stride ?
struct buffer_data
{
	VkBuffer		hndl = 0;
	VkDeviceMemory	mem = 0;
	u64				size = 0;
	//u64				offset = 0;
	u8*				hostVisible = 0;
	u64				devicePointer = 0;
	u32				magicId;
};

inline VkDescriptorBufferInfo Descriptor( const buffer_data& b )
{
	//return VkDescriptorBufferInfo{ hndl,offset,size };
	return VkDescriptorBufferInfo{ b.hndl,0,b.size };
}

// TODO: use a texture_desc struct
// TODO: add more data ?
// TODO: VkDescriptorImageInfo 
// TODO: rsc don't directly store the memory, rsc manager refrences it ?
struct image
{
	VkImage			hndl;
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

// TODO: pass device for rsc creation, and stuff
// TODO: re-think resource creation and management

// TODO: Pass BuffCreateInfo
static buffer_data
VkCreateAllocBindBuffer(
	u64					sizeInBytes,
	VkBufferUsageFlags	usage,
	vk_mem_arena& vkArena
){
	buffer_data buffData = {};

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
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
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

	return buffData;
}

static image
VkCreateAllocBindImage(
	const VkImageCreateInfo& imgInfo,
	vk_mem_arena& vkArena,
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
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
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
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena.device, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

static image
VkCreateAllocBindImage(
	VkFormat			format,
	VkImageUsageFlags	usageFlags,
	VkExtent3D			extent,
	u32					mipCount,
	vk_mem_arena&		vkArena,
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
	vkGetPhysicalDeviceMemoryProperties( dc.gpu, &memProps );
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

	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
	switch( imgInfo.imageType )
	{
	case VK_IMAGE_TYPE_1D: viewType = VK_IMAGE_VIEW_TYPE_1D; break;
	case VK_IMAGE_TYPE_2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
	case VK_IMAGE_TYPE_3D: viewType = VK_IMAGE_VIEW_TYPE_3D; break;
	default: VK_CHECK( VK_INTERNAL_ERROR( "Uknown image type !" ) ); break;
	};
	img.view = VkMakeImgView( vkArena.device, img.hndl, imgInfo.format, 0, imgInfo.mipLevels, viewType, 0, imgInfo.arrayLayers );

	return img;
}

// TODO: differentiate barriers by usage ? like layout transition, exec dependency
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

inline static VkBufferMemoryBarrier2KHR
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
){
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

inline static VkBufferMemoryBarrier2KHR VkReverseBufferBarrier2( const VkBufferMemoryBarrier2KHR& b )
{
	VkBufferMemoryBarrier2KHR barrier = b;
	std::swap( barrier.srcAccessMask, barrier.dstAccessMask );
	std::swap( barrier.srcStageMask, barrier.dstStageMask );

	return barrier;
}

inline static VkImageMemoryBarrier2KHR
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
){
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



// TODO: add VkFramebuffer here ?
struct virtual_frame
{
	buffer_data		frameData;
	VkCommandPool	cmdPool;
	VkCommandBuffer cmdBuff;
	VkSemaphore		canGetImgSema;
	VkSemaphore		canPresentSema;
	VkFence			hostSyncFence;
};

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
	VK_CHECK( vkAllocateCommandBuffers( vkDevice, &cmdBuffAllocInfo, &vrtFrame.cmdBuff ) );

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrame.canGetImgSema ) );
	VK_CHECK( vkCreateSemaphore( vkDevice, &semaInfo, 0, &vrtFrame.canPresentSema ) );

	VkFenceCreateInfo fenceInfo = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	VK_CHECK( vkCreateFence( vkDevice, &fenceInfo, 0, &vrtFrame.hostSyncFence ) );

	vrtFrame.frameData = VkCreateAllocBindBuffer( 
		bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, arena );

	return vrtFrame;
}



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

inline static VkSurfaceKHR VkMakeWinSurface( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
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
		sc.imgViews[ i ] = VkMakeImgView( vkDevice, sc.imgs[ i ], scInfo.imageFormat, 0, 1, VK_IMAGE_VIEW_TYPE_2D, 0, 1 );
	}

	sc.width = scInfo.imageExtent.width;
	sc.height = scInfo.imageExtent.height;
	sc.imgCount = scInfo.minImageCount;
	sc.imgFormat = scInfo.imageFormat;

	return sc;
}

static swapchain sc;


// TODO:
struct renderer_config
{
	VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
};
// TODO: remake
struct render_context
{
	VkPipeline		gfxPipeline;
	VkPipeline		gfxMeshletPipeline;
	VkPipeline		gfxMergedPipeline;
	VkPipeline		compPipeline;
	VkPipeline		compHiZPipeline;
	
	VkPipeline		compAvgLumPipe;
	VkPipeline		compTonemapPipe;
	VkPipeline      compExpanderPipe;
	VkPipeline      compClusterCullPipe;
	VkPipeline      compExpMergePipe;

	VkRenderPass	renderPass;
	VkRenderPass	render2ndPass;

	VkSampler		quadMinSampler;
	VkSampler		pbrTexSampler;

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

// TODO: make general ?
inline static VkRenderPass
VkMakeRenderPass(
	VkDevice	vkDevice,
	bool        clearColor,
	bool        clearDepth,
	u32         colorTargetSlot,
	u32         depthTargetSlot,
	VkFormat	colorFormat,
	VkFormat	depthFormat
){
	VkAttachmentReference colorAttachement = { colorTargetSlot, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR };
	VkAttachmentReference depthAttachement = { depthTargetSlot, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR };

	VkSubpassDescription subpassDescr = {};
	subpassDescr.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescr.colorAttachmentCount = 1;
	subpassDescr.pColorAttachments = &colorAttachement;
	subpassDescr.pDepthStencilAttachment = &depthAttachement;

	VkAttachmentDescription attachmentDescriptions[ 2 ] = {};
	attachmentDescriptions[ 0 ].format = colorFormat;
	attachmentDescriptions[ 0 ].samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescriptions[ 0 ].loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	attachmentDescriptions[ 0 ].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachmentDescriptions[ 0 ].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescriptions[ 0 ].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescriptions[ 0 ].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
	attachmentDescriptions[ 0 ].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;

	attachmentDescriptions[ 1 ].format = depthFormat;
	attachmentDescriptions[ 1 ].samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescriptions[ 1 ].loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
	attachmentDescriptions[ 1 ].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachmentDescriptions[ 1 ].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescriptions[ 1 ].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescriptions[ 1 ].initialLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;
	attachmentDescriptions[ 1 ].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR;

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

static render_context rndCtx;


static u64 VK_DLL = 0;

#ifdef _VK_DEBUG_

#include <iostream>

VKAPI_ATTR VkBool32 VKAPI_CALL
VkDbgUtilsMsgCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT		msgSeverity,
	VkDebugUtilsMessageTypeFlagsEXT				msgType,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData
){
	// NOTE: validation layer bug
	if( callbackData->messageIdNumber == 0xe8616bf2 ) return VK_FALSE;


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

constexpr VkValidationFeatureEnableEXT enabledValidationFeats[] = {
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT,
		VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
		//VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT
};


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
		"VK_LAYER_KHRONOS_validation",
		//"VK_LAYER_LUNARG_api_dump"
	#endif // _VK_DEBUG_
	};

	VK_CHECK( VK_INTERNAL_ERROR( !( VK_DLL = SysDllLoad( "vulkan-1.dll" ) ) ) );

	// TODO: full vk file generation ?

	vkGetInstanceProcAddr = ( PFN_vkGetInstanceProcAddr ) SysGetProcAddr( VK_DLL, "vkGetInstanceProcAddr" );
	vkCreateInstance = ( PFN_vkCreateInstance ) vkGetInstanceProcAddr( 0, "vkCreateInstance" );
	vkEnumerateInstanceExtensionProperties =
		( PFN_vkEnumerateInstanceExtensionProperties ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceExtensionProperties" );
	vkEnumerateInstanceLayerProperties =
		( PFN_vkEnumerateInstanceLayerProperties ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceLayerProperties" );
	vkEnumerateInstanceVersion = ( PFN_vkEnumerateInstanceVersion ) vkGetInstanceProcAddr( 0, "vkEnumerateInstanceVersion" );

	u32 vkExtsNum = 0;
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, 0 ) );
	std::vector<VkExtensionProperties> givenExts( vkExtsNum );
	VK_CHECK( vkEnumerateInstanceExtensionProperties( 0, &vkExtsNum, std::data( givenExts ) ) );
	for( u32 i = 0; i < std::size( ENABLED_INST_EXTS ); ++i )
	{
		bool foundExt = false;
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
		bool foundLayer = false;
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

	VkValidationFeaturesEXT vkValidationFeatures = { VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
	vkValidationFeatures.enabledValidationFeatureCount = std::size( enabledValidationFeats );
	vkValidationFeatures.pEnabledValidationFeatures = enabledValidationFeats;

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

// TODO: keep global ?
static VkInstance				vkInst = 0;
static VkDebugUtilsMessengerEXT	vkDbgMsg = 0;

static VkSurfaceKHR				vkSurf = 0;

// TODO: where to place these ?
extern HINSTANCE hInst;
extern HWND hWnd;



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
	u32 x;
	u32 y;
	u32 z;
};

// TODO: don't need to keep desc set layout after initing stuff ?
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

inline static VkShaderModule VkMakeShaderModule( VkDevice vkDevice, const u32* spv, u64 size )
{
	VkShaderModuleCreateInfo shaderModuleInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
	shaderModuleInfo.codeSize = size;
	shaderModuleInfo.pCode = spv;

	VkShaderModule sm = {};
	VK_CHECK( vkCreateShaderModule( vkDevice, &shaderModuleInfo, 0, &sm ) );
	return sm;
}

// TODO: no C++ and vector ?
inline static vk_shader VkLoadShader( const char* shaderPath, VkDevice vkDevice )
{
	// TODO: 
	using namespace std;
	constexpr std::string_view shadersFolder = "Shaders/"sv;
	constexpr std::string_view shaderExtension = ".spv"sv;

	std::vector<u8> binSpvShader = SysReadFile( shaderPath );

	vk_shader shader = {};
	shader.spvByteCode = std::move( binSpvShader );
	shader.module = VkMakeShaderModule( vkDevice, 
										  (const u32*) std::data( shader.spvByteCode ), 
										  std::size( shader.spvByteCode ) );
	
	std::string_view shaderName = { shaderPath };
	shaderName.remove_prefix( std::size( shadersFolder ) );
	shaderName.remove_suffix( std::size( shaderExtension ) - 1 );
	VkDbgNameObj( shader.module, vkDevice, &shaderName[ 0 ] );

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
		pushConstRange.stageFlags = shaderReflection.shader_stage;
		pushConstRange.offset = shaderReflection.push_constant_blocks[ pci ].offset;
		pushConstRange.size = shaderReflection.push_constant_blocks[ pci ].size;
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
using vk_specializations = std::initializer_list<u32>;
using vk_dynamic_states = std::initializer_list<VkDynamicState>;

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
	descSetLayoutInfo.pBindings = std::data( bindings );
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetLayoutInfo, 0, &program.descSetLayout ) );

	VkDescriptorSetLayout setLayouts[] = { program.descSetLayout, bindlessLayout };

	VkPipelineLayoutCreateInfo pipeLayoutInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipeLayoutInfo.setLayoutCount = std::size( setLayouts );
	pipeLayoutInfo.pSetLayouts = setLayouts;
	pipeLayoutInfo.pushConstantRangeCount = std::size( pushConstRanges );
	pipeLayoutInfo.pPushConstantRanges = std::data( pushConstRanges );
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipeLayoutInfo, 0, &program.pipeLayout ) );

	if( std::size( bindings ) )
	{
		std::vector<VkDescriptorUpdateTemplateEntry> entries;
		entries.reserve( std::size( bindings ) );
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
		templateInfo.pDescriptorUpdateEntries = std::data( entries );
		templateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
		templateInfo.descriptorSetLayout = program.descSetLayout;
		templateInfo.pipelineBindPoint = bindPoint;
		templateInfo.pipelineLayout = program.pipeLayout;
		templateInfo.set = 0;
		VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &program.descUpdateTemplate ) );
	}
	
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
	const vk_specializations& consts
){
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

// TODO: config struct
struct vk_gfx_pipeline_state
{
	VkPolygonMode		polyMode = VK_POLYGON_MODE_FILL;
	VkCullModeFlags		cullFlags = VK_CULL_MODE_BACK_BIT;
	VkFrontFace			frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkPrimitiveTopology primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	bool				blendCol = colorBlending;
	bool				depthWrite = VK_TRUE;
	bool				depthTestEnable = VK_TRUE;
};
// TODO: shader stages more general
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
	const vk_gfx_pipeline_state& pipelineState
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

	u32 shaderStagesCount = bool( vs ) + bool( fs );

	VkPipelineInputAssemblyStateCreateInfo inAsmStateInfo = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	inAsmStateInfo.topology = pipelineState.primTopology;

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
	rasterInfo.polygonMode = pipelineState.polyMode;
	rasterInfo.cullMode = pipelineState.cullFlags;
	rasterInfo.frontFace = pipelineState.frontFace;
	rasterInfo.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisamplingInfo = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo depthStencilState = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	depthStencilState.depthTestEnable = pipelineState.depthTestEnable;
	depthStencilState.depthWriteEnable = pipelineState.depthWrite;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER;
	depthStencilState.depthBoundsTestEnable = VK_TRUE;
	depthStencilState.minDepthBounds = 0;
	depthStencilState.maxDepthBounds = 1.0f;

	VkPipelineColorBlendAttachmentState blendConfig = {};
	blendConfig.blendEnable = pipelineState.blendCol;
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
	pipelineInfo.stageCount = shaderStagesCount;// std::size( shaderStagesInfo );
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

// TODO: pipeline caputre representations blah blah ?
VkPipeline VkMakeComputePipeline(
	VkDevice			vkDevice,
	VkPipelineCache		vkPipelineCache,
	VkPipelineLayout	vkPipelineLayout,
	VkShaderModule		cs,
	vk_specializations	consts,
	const char*			pEntryPointName = SHADER_ENTRY_POINT 
){
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




#include "r_data_structs.h"

#include "asset_compiler.h"


inline VkFormat VkGetFormat( texture_format t )
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
inline VkImageType VkGetImageType( texture_type t )
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
VkMakeImageInfo(
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
inline VkImageCreateInfo VkGetImageInfoFromMetadata( const image_metadata& meta, VkImageUsageFlags usageFlags )
{
	VkImageCreateInfo imgInfo = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
	imgInfo.imageType = VkGetImageType( meta.type );
	imgInfo.format = VkGetFormat( meta.format );
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


#define HTVK_NO_SAMPLER_REDUCTION VK_SAMPLER_REDUCTION_MODE_MAX_ENUM

// TODO: AddrMode ?
inline VkSampler VkMakeSampler(
	VkDevice				vkDevice,
	VkSamplerReductionMode	reductionMode = HTVK_NO_SAMPLER_REDUCTION,
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
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	VkSampler sampler;
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


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
struct resource_vector
{
	std::vector<T> rsc;
	u32 magicCounter;
};

template<typename T>
inline const T& GetResourceFromHndl( hndl32<T> h, const resource_vector<T>& buf )
{
	assert( std::size( buf ) );
	assert( h );

	const T& entry = buf[ IdxFromHndl32( h ) ];
	assert( entry.magicId == MagicFromHndl32( h ) );

	return entry;
}
template<typename T>
inline hndl32<T> PushResourceToContainer( T& rsc, resource_vector<T>& buf )
{
	u32 magicCounter = buf.magicCounter++;
	rsc.magicId = magicCounter;
	buf.rsc.push_back( rsc );

	return Hndl32FromMagicAndIdx<T>( magicCounter, std::size( buf.rsc ) );
}


static resource_vector<image> textures;

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

// TODO: remove/improve 
static inline void
VkStageCopyUploadBuffer(
	VkCommandBuffer		cmdBuff,
	const char*			name,
	const u8*			pData,
	u64					copyDataSize,
	buffer_data&		dstBuff,
	staging_manager&	stagingMngr
){
	dstBuff = VkCreateAllocBindBuffer( copyDataSize,
									   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
									   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
									   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
									   vkRscArena );
	VkDbgNameObj( dstBuff.hndl, dc.device, name );

	buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
	std::memcpy( stagingBuf.hostVisible, pData, stagingBuf.size );
	StagingManagerPushForRecycle( stagingBuf.hndl, stagingMngr );

	VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
	vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, dstBuff.hndl, 1, &copyRegion );
}

struct box_bounds
{
	DirectX::XMFLOAT3 min;
	DirectX::XMFLOAT3 max;
};

struct entities_data
{
	std::vector<DirectX::XMFLOAT4X4A> transforms;
	std::vector<box_bounds> instAabbs;
};

// TODO: use instancing 4 drawing ?
struct debug_context
{
	buffer_data dbgLinesBuff;
	buffer_data dbgTrisBuff;
	vk_program	pipeProg;
	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;
};

static debug_context vkDbgCtx;

// TODO: query for gpu props ?
// TODO: dbgGeom buffer size based on what ?
// TODO: shader rename
static inline debug_context VkInitDebugCtx( 
	VkDevice							vkDevice, 
	VkRenderPass						vkRndPass,
	const VkPhysicalDeviceProperties&	gpuProps 
){
	debug_context dbgCtx = {};

	vk_shader vert = VkLoadShader( "Shaders/v_cpu_dbg_draw.vert.spv", vkDevice );
	vk_shader frag = VkLoadShader( "Shaders/f_pass_col.frag.spv", vkDevice );

	dbgCtx.pipeProg = VkMakePipelineProgram( vkDevice, gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vert, &frag } );

	static_assert( worldLeftHanded );
	vk_gfx_pipeline_state lineDrawPipelineState = {};
	lineDrawPipelineState.blendCol = VK_FALSE;
	lineDrawPipelineState.depthWrite = VK_FALSE;
	lineDrawPipelineState.depthTestEnable = VK_FALSE;
	lineDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;
	lineDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	lineDrawPipelineState.polyMode = VK_POLYGON_MODE_LINE;
	lineDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	dbgCtx.drawAsLines = VkMakeGfxPipeline( 
		vkDevice, 0, vkRndPass, dbgCtx.pipeProg.pipeLayout, vert.module, frag.module, lineDrawPipelineState );

	vk_gfx_pipeline_state triDrawPipelineState = {};
	triDrawPipelineState.blendCol = VK_TRUE;
	triDrawPipelineState.depthWrite = VK_TRUE;
	triDrawPipelineState.depthTestEnable = VK_TRUE;
	triDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;// VK_CULL_MODE_FRONT_BIT;
	triDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	triDrawPipelineState.polyMode = VK_POLYGON_MODE_FILL;
	triDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	dbgCtx.drawAsTriangles = VkMakeGfxPipeline( 
		vkDevice, 0, vkRndPass, dbgCtx.pipeProg.pipeLayout, vert.module, frag.module, triDrawPipelineState );


	vkDestroyShaderModule( vkDevice, vert.module, 0 );
	vkDestroyShaderModule( vkDevice, frag.module, 0 );

	dbgCtx.dbgLinesBuff = VkCreateAllocBindBuffer( 512 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena );
	dbgCtx.dbgTrisBuff = VkCreateAllocBindBuffer( 128 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena );

	return dbgCtx;
}


static constexpr void ReverseTriangleWinding( u32* indices, u64 count )
{
	assert( count % 3 == 0 );
	for( u64 t = 0; t < count; t += 3 ) std::swap( indices[ t ], indices[ t + 2 ] );
}
// TODO: improve ?
static void GenerateIcosphere( std::vector<DirectX::XMFLOAT3>& vtxData, std::vector<u32>& idxData, u64 numIters )
{
	using namespace DirectX;

	constexpr u64 ICOSAHEDRON_FACE_NUM = 20;
	constexpr u64 ICOSAHEDRON_VTX_NUM = 12;

	constexpr float X = 0.525731112119133606f;
	constexpr float Z = 0.850650808352039932f;
	constexpr float N = 0;

	constexpr XMFLOAT3 vertices[ ICOSAHEDRON_VTX_NUM ] =
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

	std::vector<XMFLOAT3> vtxCache;
	std::vector<u32> idxCache;

	vtxCache = { std::begin( vertices ), std::end( vertices ) };
	idxData = { std::begin( triangles ),std::end( triangles ) };

	//vtxCache.reserve( ICOSAHEDRON_VTX_NUM * std::exp2( numIters ) );
	idxCache.reserve( 3 * ICOSAHEDRON_FACE_NUM * exp2( 2 * numIters ) );
	idxData.reserve( 3 * ICOSAHEDRON_FACE_NUM * exp2( 2 * numIters ) );


	for( u64 i = 0; i < numIters; ++i )
	{
		for( u64 t = 0; t < std::size( idxData ); t += 3 )
		{
			u32 i0 = idxData[ t ];
			u32 i1 = idxData[ t + 1 ];
			u32 i2 = idxData[ t + 2 ];

			XMVECTOR v0 = XMLoadFloat3( &vtxCache[ i0 ] );
			XMVECTOR v1 = XMLoadFloat3( &vtxCache[ i1 ] );
			XMVECTOR v2 = XMLoadFloat3( &vtxCache[ i2 ] );
			XMFLOAT3 m01, m12, m20;
			XMStoreFloat3( &m01, XMVector3Normalize( XMVectorAdd( v0, v1 ) ) );
			XMStoreFloat3( &m12, XMVector3Normalize( XMVectorAdd( v1, v2 ) ) );
			XMStoreFloat3( &m20, XMVector3Normalize( XMVectorAdd( v2, v0 ) ) );

			u32 idxOffset = std::size( vtxCache ) - 1;

			vtxCache.push_back( m01 );
			vtxCache.push_back( m12 );
			vtxCache.push_back( m20 );

			if constexpr( !worldLeftHanded )
			{
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
			}
			else
			{
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

	vtxData = std::move( vtxCache );
}
// TODO: remove ?
static void GenerateBoxCube( std::vector<dbg_vertex>& vtx, std::vector<u32>& idx )
{
	using namespace DirectX;

	constexpr float w = 0.5f;
	constexpr float h = 0.5f;
	constexpr float t = 0.5f;

	vec3 c0 = { w, h,-t };
	vec3 c1 = { -w, h,-t };
	vec3 c2 = { -w,-h,-t };
	vec3 c3 = { w,-h,-t };

	vec3 c4 = { w, h, t };
	vec3 c5 = { -w, h, t };
	vec3 c6 = { -w,-h, t };
	vec3 c7 = { w,-h, t };

	constexpr vec2 u = OctaNormalEncode( { 0,1,0 } );
	constexpr vec2 d = OctaNormalEncode( { 0,-1,0 } );
	constexpr vec2 f = OctaNormalEncode( { 0,0,1 } );
	constexpr vec2 b = OctaNormalEncode( { 0,0,-1 } );
	constexpr vec2 l = OctaNormalEncode( { -1,0,0 } );
	constexpr vec2 r = OctaNormalEncode( { 1,0,0 } );

	constexpr XMFLOAT4 col = {};

	dbg_vertex vertices[] = {
		// Bottom
		{ {c0.x,c0.y,c0.z,1.0f}, col },
		{ {c1.x,c1.y,c1.z,1.0f}, col },
		{ {c2.x,c2.y,c2.z,1.0f}, col },
		{ {c3.x,c3.y,c3.z,1.0f}, col },
		// Left					
		{ {c7.x,c7.y,c7.z,1.0f}, col },
		{ {c4.x,c4.y,c4.z,1.0f}, col },
		{ {c0.x,c0.y,c0.z,1.0f}, col },
		{ {c3.x,c3.y,c3.z,1.0f}, col },
		// Front				
		{ {c4.x,c4.y,c4.z,1.0f}, col },
		{ {c5.x,c5.y,c5.z,1.0f}, col },
		{ {c1.x,c1.y,c1.z,1.0f}, col },
		{ {c0.x,c0.y,c0.z,1.0f}, col },
		// Back					
		{ {c6.x,c6.y,c6.z,1.0f}, col },
		{ {c7.x,c7.y,c7.z,1.0f}, col },
		{ {c3.x,c3.y,c3.z,1.0f}, col },
		{ {c2.x,c2.y,c2.z,1.0f}, col },
		// Right				
		{ {c5.x,c5.y,c5.z,1.0f}, col },
		{ {c6.x,c6.y,c6.z,1.0f}, col },
		{ {c2.x,c2.y,c2.z,1.0f}, col },
		{ {c1.x,c1.y,c1.z,1.0f}, col },
		// Top					
		{ {c7.x,c7.y,c7.z,1.0f}, col },
		{ {c6.x,c6.y,c6.z,1.0f}, col },
		{ {c5.x,c5.y,c5.z,1.0f}, col },
		{ {c4.x,c4.y,c4.z,1.0f}, col }
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


constexpr u64 boxLineVertexCount = 24u;
constexpr u64 boxTrisVertexCount = 36u;
/*
// NOTE: corners are stored in this way:
// NOTE: courtesy of Nabla
//	   /3--------/7
//	  / |       / |
//	 /  |      /  |
//	1---------5   |
//	|  /2- - -|- -6
//	| /       |  /
//	|/        | /
//	0---------4/
*/
constexpr u8 boxLineIndices[] = {
	0,1,	0,2,	1,3,	2,3,
	0,4,	1,5,	2,6,	3,7,
	4,5,	4,6,	5,7,	6,7
};
// NOTE: counter-clockwise
constexpr u8 boxTrisIndices[] = {
	0, 1, 2,    // side 1
	2, 1, 3,
	4, 0, 6,    // side 2
	6, 0, 2,
	7, 5, 6,    // side 3
	6, 5, 4,
	3, 1, 7,    // side 4
	7, 1, 5,
	4, 5, 0,    // side 5
	0, 5, 1,
	3, 7, 2,    // side 6
	2, 7, 6,
};

inline void XM_CALLCONV 
TrnasformBoxVertices(
	DirectX::XMMATRIX		transf,
	DirectX::XMFLOAT3		boxMin,
	DirectX::XMFLOAT3		boxMax,
	DirectX::XMFLOAT4*		boxCorners
){
	using namespace DirectX;

	boxCorners[ 0 ] = { boxMax.x, boxMax.y, boxMax.z, 1.0f };
	boxCorners[ 1 ] = { boxMax.x, boxMin.y, boxMax.z, 1.0f };
	boxCorners[ 2 ] = { boxMax.x, boxMax.y, boxMin.z, 1.0f };
	boxCorners[ 3 ] = { boxMax.x, boxMin.y, boxMin.z, 1.0f };
	boxCorners[ 4 ] = { boxMin.x, boxMax.y, boxMax.z, 1.0f };
	boxCorners[ 5 ] = { boxMin.x, boxMin.y, boxMax.z, 1.0f };
	boxCorners[ 6 ] = { boxMin.x, boxMax.y, boxMin.z, 1.0f };
	boxCorners[ 7 ] = { boxMin.x, boxMin.y, boxMin.z, 1.0f };

	for( u64 ci = 0; ci < 8; ++ci )
	{
		XMFLOAT4& outCorner = boxCorners[ ci ];
		XMVECTOR trnasformedCorner = XMVector4Transform( XMLoadFloat4( &outCorner ), transf );
		XMStoreFloat4( &outCorner, trnasformedCorner );
	}
}

// TODO: draw them filled ? 
// TODO: rethink ?

// TODO: remove ?
inline void WriteBoundingBoxLines(
	const DirectX::XMFLOAT4X4A& mat, 
	const DirectX::XMFLOAT3& min, 
	const DirectX::XMFLOAT3& max,
	const DirectX::XMFLOAT4& col,
	dbg_vertex* thisLineRange
){
	using namespace DirectX;

	XMFLOAT4 boxVertices[ 8 ] = {};
	TrnasformBoxVertices( XMLoadFloat4x4A( &mat ), min, max, boxVertices );

	for( u64 ii = 0; ii < boxLineVertexCount; ++ii )
	{
		thisLineRange[ ii ] = { boxVertices[ boxLineIndices[ ii ] ],col };
	}
}

inline std::vector<dbg_vertex>
ComputeSceneDebugBoundingBoxes(
	DirectX::XMMATRIX frustDrawMat,
	const entities_data& entities
){
	using namespace DirectX;

	assert( std::size( entities.instAabbs ) == std::size( entities.transforms ) );
	assert( boxLineVertexCount == std::size( boxLineIndices ) );

	u64 entitiesCount = std::size( entities.transforms );
	std::vector<dbg_vertex> dbgGeom;
	// NOTE: + boxLineVertexCount for frustum
	dbgGeom.resize( entitiesCount * boxLineVertexCount + boxLineVertexCount );

	for( u64 i = 0; i < entitiesCount; ++i )
	{
		const box_bounds& aabb = entities.instAabbs[ i ];
		const XMFLOAT4X4A& mat = entities.transforms[ i ];

		XMFLOAT4 boxVertices[ 8 ] = {};
		TrnasformBoxVertices( XMLoadFloat4x4A( &mat ), aabb.min, aabb.max, boxVertices );

		dbg_vertex* boxLineRange = std::data( dbgGeom ) + i * boxLineVertexCount;
		constexpr XMFLOAT4 col = { 255.0f,255.0f,0.0f,1.0f };
		for( u64 ii = 0; ii < boxLineVertexCount; ++ii )
		{
			boxLineRange[ ii ] = { boxVertices[ boxLineIndices[ ii ] ],col };
		}
	}

	u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;

	XMFLOAT4 boxVertices[ 8 ] = {};
	TrnasformBoxVertices( frustDrawMat, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f }, boxVertices );

	dbg_vertex* boxLineRange = std::data( dbgGeom ) + frustBoxOffset;
	constexpr XMFLOAT4 frustCol = { 0.0f, 255.0f, 255.0f, 1.0f };
	for( u64 i = 0; i < boxLineVertexCount; ++i )
	{
		boxLineRange[ i ] = { boxVertices[ boxLineIndices[ i ] ],frustCol };
	}

	return dbgGeom;
}


static entities_data entities;
static std::vector<dbg_vertex> dbgLineGeomCache;


static buffer_data proxyGeomBuff;
static buffer_data proxyIdxBuff;

static buffer_data screenspaceBoxBuff;

static buffer_data globVertexBuff;
// TODO: use indirect merged index buffer
static buffer_data indexBuff;
static buffer_data meshBuff;

static buffer_data meshletBuff;
// TODO: store u8x4 vtx in here ?
static buffer_data meshletVtxBuff;
static buffer_data meshletTrisBuff;

// TODO:
static buffer_data transformsBuff;

static buffer_data materialsBuff;
static buffer_data instDescBuff;
static buffer_data lightsBuff;

static buffer_data indirectMergedIndexBuff;

static buffer_data visibleInstsBuff;
static buffer_data meshletIdBuff;
static buffer_data visibleMeshletsBuff;

static buffer_data drawCmdBuff;
static buffer_data drawCmdAabbsBuff;
static buffer_data drawCmdDbgBuff;
static buffer_data drawVisibilityBuff;

static buffer_data drawMergedCmd;


constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "Assets/cyberbaron.drak";
//constexpr char glbPath[] = "D:\\3d models\\WaterBottle.glb";
//constexpr char drakPath[] = "Assets/WaterBottle.drak";
constexpr double RAND_MAX_SCALE = 1.0 / double( RAND_MAX );
// TODO: remove ?
inline static std::vector<instance_desc>
SpawnRandomInstances( const std::span<mesh_desc> meshes, u64 drawCount, u64 mtrlCount, float sceneRadius )
{
	using namespace DirectX;

	std::vector<instance_desc> insts( drawCount );
	float scale = 1.0f;
	for( instance_desc& i : insts )
	{
		i.meshIdx = rand() % std::size( meshes );
		i.mtrlIdx = 0;
		i.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		i.scale = scale * float( rand() * RAND_MAX_SCALE ) + 2.0f;

		XMVECTOR axis = XMVector3Normalize(
			XMVectorSet( float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 float( rand() * RAND_MAX_SCALE ) * 2.0f - 1.0f,
						 0 ) );
		float angle = XMConvertToRadians( float( rand() * RAND_MAX_SCALE ) * 90.0f );

		//XMVECTOR quat = XMQuaternionRotationNormal( axis, angle );
		XMVECTOR quat = XMQuaternionIdentity();
		XMStoreFloat4( &i.rot, quat );

		XMMATRIX scaleM = XMMatrixScaling( i.scale, i.scale, i.scale );
		XMMATRIX rotM = XMMatrixRotationQuaternion( quat );
		XMMATRIX moveM = XMMatrixTranslation( i.pos.x, i.pos.y, i.pos.z );

		XMMATRIX localToWorld = XMMatrixMultiply( scaleM, XMMatrixMultiply( rotM, moveM ) );

		XMStoreFloat4x4A( &i.localToWorld, localToWorld );
	}

	return insts;
}
inline static std::vector<light_data> SpawnRandomLights( u64 lightCount, float sceneRadius )
{
	std::vector<light_data> lights( lightCount );
	for( light_data& l : lights )
	{
		l.pos.x = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.y = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.pos.z = float( rand() * RAND_MAX_SCALE ) * sceneRadius * 2.0f - sceneRadius;
		l.radius = 100.0f * float( rand() * RAND_MAX_SCALE ) + 2.0f;
		l.col = { 600.0f,200.0f,100.0f };
	}

	return lights;
}
// TODO: staging clean up
// TODO: re make offsets, ranges, etc
static inline void VkUploadResources( VkCommandBuffer cmdBuff, entities_data& entities )
{
	std::vector<u8> binaryData;
	// TODO: add renderable_instances
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

	const std::span<mesh_desc> meshes = { (mesh_desc*) ( std::data( binaryData ) + offset ),fileDesc.meshesCount };
	offset += BYTE_COUNT( meshes );
	const std::span<material_data> mtrlDesc = { (material_data*) ( std::data( binaryData ) + offset ),fileDesc.mtrlsCount };
	offset += BYTE_COUNT( mtrlDesc );
	const std::span<image_metadata> imgDesc = { (image_metadata*) ( std::data( binaryData ) + offset ),fileDesc.texCount };

	const std::span<vertex> vtxView = { 
		(vertex*) ( std::data( binaryData ) + fileDesc.dataOffset + fileDesc.vtxRange.offset ),
		fileDesc.vtxRange.size / sizeof( vertex ) };

	const std::span<meshlet> mletView = { 
		(meshlet*) ( std::data( binaryData ) + fileDesc.dataOffset + fileDesc.mletsRange.offset ),
		fileDesc.mletsRange.size / sizeof( meshlet ) };

	assert( std::size( mletView ) < u16( -1 ) );

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

	constexpr u64 randSeed = 42;
	constexpr u64 drawCount = 4;
	constexpr u64 lightCount = 6;
	constexpr float sceneRad = 40.0f;
	std::srand( randSeed );

	assert( std::size( mtrls ) == 1 );
	std::vector<instance_desc> instDesc = SpawnRandomInstances( { std::data( meshes ),std::size( meshes ) }, drawCount, 1, sceneRad );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRad );

	assert( std::size( instDesc ) < u16( -1 ) );


	for( const instance_desc& ii : instDesc )
	{
		const mesh_desc& m = meshes[ ii.meshIdx ];
		entities.transforms.push_back( ii.localToWorld );
		entities.instAabbs.push_back( { m.aabbMin, m.aabbMax } );
	}


	std::vector<DirectX::XMFLOAT3> proxyVtx;
	std::vector<u32> proxyIdx;
	GenerateIcosphere( proxyVtx, proxyIdx, 8 );
	// TODO: stupid templates
	u64 uniqueVtxCount = MeshoptReindexMesh( 
		std::span<DirectX::XMFLOAT3>{ proxyVtx }, { std::begin( proxyIdx ),std::end( proxyIdx ) } );
	proxyVtx.resize( uniqueVtxCount );
	MeshoptOptimizeMesh( std::span<DirectX::XMFLOAT3>{ proxyVtx }, { std::begin( proxyIdx ),std::end( proxyIdx ) } );


	// TODO: make easier to use 
	std::vector<VkBufferMemoryBarrier2KHR> buffBarriers;
	{
		const u8* pVtxData = std::data( binaryData ) + fileDesc.dataOffset + fileDesc.vtxRange.offset;
		VkStageCopyUploadBuffer( cmdBuff, "Buff_Vtx", pVtxData, fileDesc.vtxRange.size, globVertexBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2( 
			globVertexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
	}
	{
		const u8* pIdxData = std::data( binaryData ) + fileDesc.dataOffset + fileDesc.idxRange.offset;
		u64 copyDataSize = fileDesc.idxRange.size;
		indexBuff = 
			VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
		VkDbgNameObj( indexBuff.hndl, dc.device, "Buff_Idx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( copyDataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, pIdxData, stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, indexBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier2( 
			indexBuff.hndl, 
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR, 
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ) );
	}
	{
		VkStageCopyUploadBuffer(
			cmdBuff, "Buff_Mesh_Desc", (const u8*) std::data( meshes ), BYTE_COUNT( meshes ), meshBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
	}
	{
		VkStageCopyUploadBuffer(
			cmdBuff, "Buff_Lights", (const u8*) std::data( lights ), BYTE_COUNT( lights ), lightsBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			lightsBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			//VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR ) );
	}
	{
		VkStageCopyUploadBuffer(
			cmdBuff, "Buff_Inst_Descs", (const u8*) std::data( instDesc ), BYTE_COUNT( instDesc ), instDescBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			instDescBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );

	}
	{
		VkStageCopyUploadBuffer(
			cmdBuff, "Buff_Mtrls", (const u8*) std::data( mtrls ), BYTE_COUNT( mtrls ), materialsBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			materialsBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR ) );
	}
	{
		VkStageCopyUploadBuffer(
			cmdBuff, "Buff_Meshlets", (const u8*) std::data( mletView ), BYTE_COUNT( mletView ), meshletBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ) );
	}
	{
		const std::span<u32> mletVtxView = {
			( u32* )( std::data( binaryData ) + fileDesc.dataOffset + fileDesc.mletsVtxRange.offset ),
			fileDesc.mletsVtxRange.size / sizeof( u32 ) };
		VkStageCopyUploadBuffer(
			cmdBuff, "Buff_Meshlet_Vtx", (const u8*) std::data( mletVtxView ), BYTE_COUNT( mletVtxView ), meshletVtxBuff, stagingManager );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletVtxBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ) );
	}
	{
		const std::span<u8> mletTriView = {
			( u8* )( std::data( binaryData ) + fileDesc.dataOffset + fileDesc.mletsTrisRange.offset ),
			fileDesc.mletsTrisRange.size / sizeof( u8 ) };

		meshletTrisBuff = VkCreateAllocBindBuffer( BYTE_COUNT( mletTriView ),
												   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												   VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
												   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												   vkRscArena );
		VkDbgNameObj( meshletTrisBuff.hndl, dc.device, "Buff_Meshlet_Tris" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( mletTriView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( mletTriView ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshletTrisBuff.hndl, 1, &copyRegion );

		buffBarriers.push_back( VkMakeBufferBarrier2(
			meshletTrisBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ) );
	}


	{
		proxyGeomBuff = VkCreateAllocBindBuffer(
			BYTE_COUNT( proxyVtx ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
		VkDbgNameObj( proxyGeomBuff.hndl, dc.device, "Buff_Proxy_Vtx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( proxyVtx ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( proxyVtx ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, proxyGeomBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			proxyGeomBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ) );
	}
	{
		proxyIdxBuff = VkCreateAllocBindBuffer(
			BYTE_COUNT( proxyIdx ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
		VkDbgNameObj( proxyIdxBuff.hndl, dc.device, "Buff_Proxy_Idx" );

		buffer_data stagingBuf = VkCreateAllocBindBuffer( BYTE_COUNT( proxyIdx ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		std::memcpy( stagingBuf.hostVisible, std::data( proxyIdx ), stagingBuf.size );
		StagingManagerPushForRecycle( stagingBuf.hndl, stagingManager );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, proxyIdxBuff.hndl, 1, &copyRegion );
		buffBarriers.push_back( VkMakeBufferBarrier2(
			proxyIdxBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ) );
	}


	drawCmdBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( draw_command ),
										   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
										   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
										   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
										   vkRscArena );
	VkDbgNameObj( drawCmdBuff.hndl, dc.device, "Buff_Indirect_Draw_Cmds" );

	drawCmdDbgBuff = VkCreateAllocBindBuffer( std::size( mletView ) * sizeof( draw_command ),
											  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											  VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
											  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
											  vkRscArena );
	VkDbgNameObj( drawCmdDbgBuff.hndl, dc.device, "Buff_Indirect_Dbg_Draw_Cmds" );

	// TODO: use per wave stuff ?
	drawVisibilityBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( u32 ),
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												  vkRscArena );
	VkDbgNameObj( drawVisibilityBuff.hndl, dc.device, "Buff_Draw_Vis" );


	screenspaceBoxBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * sizeof( dbg_vertex ) * 8,
												  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
												  vkRscArena );
	VkDbgNameObj( screenspaceBoxBuff.hndl, dc.device, "Buff_Screenspace_Boxes" );

	visibleInstsBuff = VkCreateAllocBindBuffer( std::size( instDesc ) * 3 * sizeof( u32 ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
												vkRscArena );
	visibleMeshletsBuff = VkCreateAllocBindBuffer( 10 * MB,//std::size( mletView ) * 3 * sizeof( u32 ),
												   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
												   vkRscArena );

	meshletIdBuff = VkCreateAllocBindBuffer( 10'000 * sizeof( u64 ), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkRscArena );
	VkDbgNameObj( meshletIdBuff.hndl, dc.device, "Buff_Meshlet_Dispatch_IDs" );


	indirectMergedIndexBuff = VkCreateAllocBindBuffer( 
		10 * MB,
		//std::size( instDesc ) * fileDesc.idxRange.size, 
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 
		vkRscArena );

	drawCmdAabbsBuff = VkCreateAllocBindBuffer( 10'000 * sizeof( draw_indirect ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
												vkRscArena );


	drawMergedCmd = VkCreateAllocBindBuffer(
		sizeof( draw_command ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena );

	//// NOTE: 1 to 1 corresp to instances
	//transformsBuff = VkCreateAllocBindBuffer(
	//	sizeof( DirectX::XMFLOAT4X4A ) * std::size( instDesc ),
	//	VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
	//	VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
	//	vkRscArena );
	//VkDbgNameObj( transformsBuff.hndl, dc.device, "Buff_Transforms" );

	// NOTE: create and texture uploads
	std::vector<VkImageMemoryBarrier2KHR> imageBarriers;
	{
		imageBarriers.reserve( std::size( imgDesc ) );
		
		u64 newTexturesOffset = std::size( textures.rsc );

		for( const image_metadata& meta : imgDesc )
		{
			VkImageCreateInfo info = VkGetImageInfoFromMetadata( meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
			image img = VkCreateAllocBindImage( info, vkAlbumArena );

			imageBarriers.push_back( VkMakeImageBarrier2(
				img.hndl,
				0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );


			hndl32<image> hImg = PushResourceToContainer( img, textures );
		}

		VkDependencyInfoKHR imitImagesDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		imitImagesDependency.imageMemoryBarrierCount = std::size( imageBarriers );
		imitImagesDependency.pImageMemoryBarriers = std::data( imageBarriers );
		vkCmdPipelineBarrier2KHR( cmdBuff, &imitImagesDependency );

		imageBarriers.resize( 0 );

		buffer_data stagingBuff = VkCreateAllocBindBuffer( fileDesc.texRange.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena );
		const u8* pTexBinData = std::data( binaryData ) + fileDesc.dataOffset + fileDesc.texRange.offset;
		std::memcpy( stagingBuff.hostVisible, pTexBinData, stagingBuff.size );
		StagingManagerPushForRecycle( stagingBuff.hndl, stagingManager );

		for( u64 i = 0; i < std::size( imgDesc ); ++i )
		{
			const image& dst = textures.rsc[ i + newTexturesOffset ];

			VkBufferImageCopy imgCopyRegion = {};
			imgCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imgCopyRegion.bufferOffset = imgDesc[ i ].texBinRange.offset;
			imgCopyRegion.imageSubresource.mipLevel = 0;
			imgCopyRegion.imageSubresource.baseArrayLayer = 0;
			imgCopyRegion.imageSubresource.layerCount = 1;
			imgCopyRegion.imageOffset = VkOffset3D{};
			imgCopyRegion.imageExtent = { u32( dst.width ),u32( dst.height ),1 };
			vkCmdCopyBufferToImage( cmdBuff, stagingBuff.hndl, dst.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );

			imageBarriers.push_back( VkMakeImageBarrier2(
				dst.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
				VK_ACCESS_2_SHADER_READ_BIT_KHR,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}
	}

	VkDependencyInfoKHR uploadDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	uploadDependency.bufferMemoryBarrierCount = std::size( buffBarriers );
	uploadDependency.pBufferMemoryBarriers = std::data( buffBarriers );
	uploadDependency.imageMemoryBarrierCount = std::size( imageBarriers );
	uploadDependency.pImageMemoryBarriers = std::data( imageBarriers );
	vkCmdPipelineBarrier2KHR( cmdBuff, &uploadDependency );
}

static buffer_data drawCountBuff;
static buffer_data drawCountDbgBuff;

static buffer_data avgLumBuff;

static buffer_data shaderGlobalsBuff;
static buffer_data shaderGlobalSyncCounterBuff;

static buffer_data depthAtomicCounterBuff;

static buffer_data atomicCounterBuff;

static buffer_data bdasUboBuff;

static buffer_data dispatchCmdBuff;
static buffer_data dispatchCmdBuff2;
static buffer_data dispatchCmdBuff3;

static buffer_data meshletCountBuff;

static buffer_data mergedIndexCountBuff;

static buffer_data drawMergedCountBuff;
// TODO:
inline static void VkInitInternalBuffers()
{
	avgLumBuff = VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, vkRscArena );
	VkDbgNameObj( avgLumBuff.hndl, dc.device, "Buff_AvgLum" );

	shaderGlobalsBuff = VkCreateAllocBindBuffer( 64,
												 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												 vkRscArena );

	shaderGlobalSyncCounterBuff = VkCreateAllocBindBuffer( 4,
														   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
														   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
														   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
														   vkRscArena );

	drawCountBuff = VkCreateAllocBindBuffer( 4,
											 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
											 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											 vkRscArena );
	VkDbgNameObj( drawCountBuff.hndl, dc.device, "Buff_Draw_Count" );

	drawCountDbgBuff = VkCreateAllocBindBuffer( 4, 
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT, 
												vkRscArena );
	VkDbgNameObj( drawCountDbgBuff.hndl, dc.device, "Buff_Dbg_Draw_Count" );
	// TODO: no transfer bit ?
	depthAtomicCounterBuff =
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );

	dispatchCmdBuff = VkCreateAllocBindBuffer( sizeof( dispatch_command ), 
											   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
											   VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
											   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											   vkRscArena );
	dispatchCmdBuff2 = VkCreateAllocBindBuffer( sizeof( dispatch_command ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												vkRscArena );
	dispatchCmdBuff3 = VkCreateAllocBindBuffer( sizeof( dispatch_command ),
												VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
												VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												vkRscArena );
	
	bdasUboBuff = VkCreateAllocBindBuffer( sizeof( global_bdas ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, vkHostComArena );

	meshletCountBuff = 
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
	VkDbgNameObj( meshletCountBuff.hndl, dc.device, "Buff_Mlet_Dispatch_Count" );

	atomicCounterBuff = VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
	VkDbgNameObj( atomicCounterBuff.hndl, dc.device, "Buff_Atomic_Counter" );

	mergedIndexCountBuff = 
		VkCreateAllocBindBuffer( 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );

	drawMergedCountBuff = VkCreateAllocBindBuffer( 
		4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena );
	VkDbgNameObj( drawMergedCountBuff.hndl, dc.device, "Buff_Draw_Merged_Count" );
}

// TODO: move out of global/static
static vk_program	gfxMergedProgram = {};
static vk_program	gfxOpaqueProgram = {};
static vk_program	gfxMeshletProgram = {};
static vk_program	cullCompProgram = {};
static vk_program	depthPyramidCompProgram = {};
static vk_program	avgLumCompProgram = {};
static vk_program	tonemapCompProgram = {};
static vk_program	depthPyramidMultiProgram = {};
static vk_program   expanderCompProgram = {};
static vk_program   clusterCullCompProgram = {};
static vk_program   expMergeCompProgram = {};

static vk_program   dbgDrawProgram = {};
static VkPipeline   gfxDrawIndirDbg = {};

static vk_program   zPrepassProgram = {};
static VkPipeline   gfxZPrepass = {};
static VkRenderPass zRndPass = {};
// TODO: no structured binding
void VkBackendInit()
{
	auto [vkInst, vkDbgMsg] = VkMakeInstance();

	vkSurf = VkMakeWinSurface( vkInst, hInst, hWnd );
	dc = VkMakeDeviceContext( vkInst, vkSurf );

	VkStartGfxMemory( dc.gpu, dc.device  );

	sc = VkMakeSwapchain( dc.device,dc.gpu, vkSurf, dc.gfxQueueIdx, VK_FORMAT_B8G8R8A8_UNORM );


	rndCtx.renderPass = VkMakeRenderPass( dc.device, 1, 1, 0, 1, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat );
	rndCtx.render2ndPass = VkMakeRenderPass( dc.device, 0, 0, 0, 1, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat );
	//zRndPass = VkMakeRenderPass( dc.device, 0, 1, VK_ATTACHMENT_UNUSED, 1, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat );
	zRndPass = VkMakeRenderPass( dc.device, 0, 1, 0, 1, rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat );

	
	globBindlessDesc = VkMakeBindlessGlobalDescriptor( dc.device, dc.gpuProps );

	{
		vk_shader vertZPre = VkLoadShader( "Shaders/v_z_prepass.vert.spv", dc.device );

		vk_gfx_pipeline_state zPrepass = {};

		zPrepassProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertZPre } );
		gfxZPrepass = VkMakeGfxPipeline( dc.device, 0, zRndPass, zPrepassProgram.pipeLayout, vertZPre.module, 0, zPrepass );

		vkDestroyShaderModule( dc.device, vertZPre.module, 0 );
	}
	{
		vk_shader vertBox = VkLoadShader( "Shaders/box_meshlet_draw.vert.spv", dc.device );
		vk_shader normalCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );

		vk_gfx_pipeline_state lineDrawPipelineState = {};
		lineDrawPipelineState.blendCol = VK_FALSE;
		lineDrawPipelineState.depthWrite = VK_FALSE;
		lineDrawPipelineState.depthTestEnable = VK_FALSE;
		lineDrawPipelineState.cullFlags = VK_CULL_MODE_NONE;
		lineDrawPipelineState.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		lineDrawPipelineState.polyMode = VK_POLYGON_MODE_LINE;
		lineDrawPipelineState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

		dbgDrawProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertBox, &normalCol } );
		gfxDrawIndirDbg = VkMakeGfxPipeline( 
			dc.device, 0, rndCtx.renderPass, dbgDrawProgram.pipeLayout, vertBox.module, normalCol.module, lineDrawPipelineState );

		vkDestroyShaderModule( dc.device, vertBox.module, 0 );
		vkDestroyShaderModule( dc.device, normalCol.module, 0 );
	}
	{
		vk_shader drawCull = VkLoadShader( "Shaders/draw_cull.comp.spv", dc.device );
		cullCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &drawCull } );
		rndCtx.compPipeline = VkMakeComputePipeline( dc.device, 0, cullCompProgram.pipeLayout, drawCull.module, { OBJ_CULL_WORKSIZE } );
		VkDbgNameObj( rndCtx.compPipeline, dc.device, "Pipeline_Comp_DrawCull" );

		vk_shader expansionComp = VkLoadShader( "Shaders/id_expander.comp.spv", dc.device );
		expanderCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &expansionComp } );
		rndCtx.compExpanderPipe = VkMakeComputePipeline( dc.device, 0, expanderCompProgram.pipeLayout, expansionComp.module, {} );
		VkDbgNameObj( rndCtx.compExpanderPipe, dc.device, "Pipeline_Comp_iD_Expander" );

		vk_shader clusterCull = VkLoadShader( "Shaders/cluster_cull.comp.spv", dc.device );
		clusterCullCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &clusterCull } );
		rndCtx.compClusterCullPipe = VkMakeComputePipeline( dc.device, 0, clusterCullCompProgram.pipeLayout, clusterCull.module, {} );
		VkDbgNameObj( rndCtx.compClusterCullPipe, dc.device, "Pipeline_Comp_ClusterCull" );

		vk_shader expMerge = VkLoadShader( "Shaders/comp_expand_merge.comp.spv", dc.device );
		expMergeCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &expMerge } );
		rndCtx.compExpMergePipe = VkMakeComputePipeline( dc.device, 0, expMergeCompProgram.pipeLayout, expMerge.module, {} );
		VkDbgNameObj( rndCtx.compExpMergePipe, dc.device, "Pipeline_Comp_ExpMerge" );

		vkDestroyShaderModule( dc.device, drawCull.module, 0 );
		vkDestroyShaderModule( dc.device, expansionComp.module, 0 );
		vkDestroyShaderModule( dc.device, clusterCull.module, 0 );
		vkDestroyShaderModule( dc.device, expMerge.module, 0 );
	}
	{
		vk_shader vertPBR = VkLoadShader( "Shaders/shdr.vert.spv", dc.device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", dc.device );
		vk_gfx_pipeline_state opaqueState = {};
		gfxOpaqueProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertPBR, &fragPBR } );
		rndCtx.gfxPipeline =
			VkMakeGfxPipeline( dc.device, 0, rndCtx.renderPass, gfxOpaqueProgram.pipeLayout, vertPBR.module, fragPBR.module, opaqueState );
		VkDbgNameObj( rndCtx.gfxPipeline, dc.device, "Pipeline_Gfx_Opaque" );

		vkDestroyShaderModule( dc.device, vertPBR.module, 0 );
		vkDestroyShaderModule( dc.device, fragPBR.module, 0 );
	}
	{
		vk_shader vtxMerged = VkLoadShader( "Shaders/vtx_merged.vert.spv", dc.device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", dc.device );
		vk_gfx_pipeline_state opaqueState = {};
		gfxMergedProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vtxMerged, &fragPBR } );
		rndCtx.gfxMergedPipeline = VkMakeGfxPipeline( 
			dc.device, 0, rndCtx.renderPass, gfxMergedProgram.pipeLayout, vtxMerged.module, fragPBR.module, opaqueState );
		VkDbgNameObj( rndCtx.gfxMergedPipeline, dc.device, "Pipeline_Gfx_Merged" );

		vkDestroyShaderModule( dc.device, vtxMerged.module, 0 );
		vkDestroyShaderModule( dc.device, fragPBR.module, 0 );
	}
	{
		vk_shader vertMeshlet = VkLoadShader( "Shaders/meshlet.vert.spv", dc.device );
		vk_shader fragCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", dc.device );
		vk_gfx_pipeline_state meshletState = {};
		gfxMeshletProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertMeshlet, &fragCol } );
		rndCtx.gfxMeshletPipeline = VkMakeGfxPipeline( 
			dc.device, 0, rndCtx.renderPass, gfxMeshletProgram.pipeLayout, vertMeshlet.module, fragCol.module, meshletState );
		VkDbgNameObj( rndCtx.gfxMeshletPipeline, dc.device, "Pipeline_Gfx_MeshletDraw" );

		vkDestroyShaderModule( dc.device, vertMeshlet.module, 0 );
		vkDestroyShaderModule( dc.device, fragCol.module, 0 );
	}
	{
		vk_shader avgLum = VkLoadShader( "Shaders/avg_luminance.comp.spv", dc.device );
		avgLumCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &avgLum } );
		rndCtx.compAvgLumPipe = 
			VkMakeComputePipeline( dc.device, 0, avgLumCompProgram.pipeLayout, avgLum.module, { dc.waveSize } );
		VkDbgNameObj( rndCtx.compAvgLumPipe, dc.device, "Pipeline_Comp_AvgLum" );

		vk_shader toneMapper = VkLoadShader( "Shaders/tonemap_gamma.comp.spv", dc.device );
		tonemapCompProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &toneMapper } );
		rndCtx.compTonemapPipe = VkMakeComputePipeline( dc.device, 0, tonemapCompProgram.pipeLayout, toneMapper.module, {} );
		VkDbgNameObj( rndCtx.compTonemapPipe, dc.device, "Pipeline_Comp_Tonemap" );

		vkDestroyShaderModule( dc.device, avgLum.module, 0 );
		vkDestroyShaderModule( dc.device, toneMapper.module, 0 );
	}
	{
		static_assert( multiPassDepthPyramid );
		vk_shader downsampler = VkLoadShader( "Shaders/pow2_downsampler.comp.spv", dc.device );
		depthPyramidMultiProgram = VkMakePipelineProgram( dc.device, dc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &downsampler } );
		rndCtx.compHiZPipeline = VkMakeComputePipeline( dc.device, 0, depthPyramidMultiProgram.pipeLayout, downsampler.module, {} );
		VkDbgNameObj( rndCtx.compHiZPipeline, dc.device, "Pipeline_Comp_DepthPyr" );

		vkDestroyShaderModule( dc.device, downsampler.module, 0 );
	}

	for( u64 vfi = 0; vfi < rndCtx.framesInFlight; ++vfi )
	{
		rndCtx.vrtFrames[ vfi ] = VkCreateVirtualFrame( dc.device, dc.gfxQueueIdx, 1 * MB, vkHostComArena );
	}

	VkInitInternalBuffers();

	vkDbgCtx = VkInitDebugCtx( dc.device, rndCtx.renderPass, dc.gpuProps );


	rndCtx.quadMinSampler =
		VkMakeSampler( dc.device, VK_SAMPLER_REDUCTION_MODE_MIN, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE );
}

inline u64 VkGetGroupCount( u64 invocationCount, u64 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}

// NOTE: dx math ? includes these ?
//#include <intrin.h>
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

inline void VkDebugSyncBarrierEverything( VkCommandBuffer cmdBuff )
{
	VkMemoryBarrier2KHR everythingBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
	everythingBarrier.srcStageMask = everythingBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR;
	everythingBarrier.srcAccessMask = everythingBarrier.dstAccessMask = 
		VK_ACCESS_2_MEMORY_READ_BIT_KHR | VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
	
	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &everythingBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );
}

// TODO: meshlet cone culling 
// TODO: must pass vertexOffset around somehow
// TODO: revisit triangle culling ?
inline static void 
CullPass( 
	VkCommandBuffer			cmdBuff, 
	VkPipeline				vkPipeline, 
	const vk_program&		program,
	const image&			depthPyramid,
	const VkSampler&		minQuadSampler
){
	// NOTE: wtf Vulkan ?
	constexpr u64 VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR;


	vk_label label = { cmdBuff,"Cull Pass",{} };

	cull_info cullInfo = {};
	cullInfo.drawCallsCount = instDescBuff.size / sizeof( instance_desc );
	
	vkCmdFillBuffer( cmdBuff, drawCountBuff.hndl, 0, drawCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, drawCountDbgBuff.hndl, 0, drawCountDbgBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, meshletCountBuff.hndl, 0, meshletCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, mergedIndexCountBuff.hndl, 0, mergedIndexCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, drawMergedCountBuff.hndl, 0, drawMergedCountBuff.size, 0u );

	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff.hndl, 0, dispatchCmdBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff2.hndl, 0, dispatchCmdBuff2.size, 0u );
	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff3.hndl, 0, dispatchCmdBuff3.size, 0u );
	
	VkBufferMemoryBarrier2KHR beginCullBarriers[] = {
		VkMakeBufferBarrier2( 
			drawCmdBuff.hndl,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

	    VkMakeBufferBarrier2( 
			meshletCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountDbgBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			mergedIndexCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

		VkMakeBufferBarrier2( 
			dispatchCmdBuff.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			dispatchCmdBuff2.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			dispatchCmdBuff3.hndl,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

		VkMakeBufferBarrier2(
			indirectMergedIndexBuff.hndl,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR )
	};

	VkImageMemoryBarrier2KHR hiZReadBarrier = VkMakeImageBarrier2(
		rndCtx.depthPyramid.hndl,
		VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_ACCESS_2_SHADER_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT );

	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.bufferMemoryBarrierCount = std::size( beginCullBarriers );
	dependency.pBufferMemoryBarriers = beginCullBarriers;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &hiZReadBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );

	VkDescriptorImageInfo depthPyramidInfo = { minQuadSampler, depthPyramid.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	
	vk_descriptor_info pushDescs[] = {   
		Descriptor( drawVisibilityBuff ),
		Descriptor( visibleInstsBuff ),
		depthPyramidInfo,
		Descriptor( atomicCounterBuff ),
		Descriptor( dispatchCmdBuff ),
		Descriptor( drawCountBuff ),
		Descriptor( drawCmdBuff )
	};

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, pushDescs );
	vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );
	vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( cullInfo ), &cullInfo );
	vkCmdDispatch( cmdBuff, VkGetGroupCount( cullInfo.drawCallsCount, program.groupSize.x ), 1, 1 );

	
#if 1
	{
		VkBufferMemoryBarrier2KHR dispatchBarrier =
			VkMakeBufferBarrier2( dispatchCmdBuff.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH );

		// TODO: write to read and write to write separately ?
		VkMemoryBarrier2KHR writeToReadWriteBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
		writeToReadWriteBarrier.srcStageMask = writeToReadWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
		writeToReadWriteBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
		writeToReadWriteBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

		VkDependencyInfoKHR execDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		execDependency.bufferMemoryBarrierCount = 1;
		execDependency.pBufferMemoryBarriers = &dispatchBarrier;
		execDependency.memoryBarrierCount = 1;
		execDependency.pMemoryBarriers = &writeToReadWriteBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &execDependency );



		vk_descriptor_info pushDesc[] = {
			Descriptor( visibleInstsBuff ),
			Descriptor( drawCountBuff ),
			Descriptor( meshletIdBuff ),
			Descriptor( meshletCountBuff ),
			Descriptor( atomicCounterBuff ),
			Descriptor( dispatchCmdBuff2 )
		};

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpanderPipe );
		vkCmdPushDescriptorSetWithTemplateKHR( 
			cmdBuff, expanderCompProgram.descUpdateTemplate, expanderCompProgram.pipeLayout, 0, pushDesc );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff.hndl, 0 );
	}
	
	{
		VkBufferMemoryBarrier2KHR dispatchBarrier =
			VkMakeBufferBarrier2( dispatchCmdBuff2.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH );

		// TODO: write to read and write to write separately ?
		VkMemoryBarrier2KHR writeToReadWriteBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
		writeToReadWriteBarrier.srcStageMask = writeToReadWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
		writeToReadWriteBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
		writeToReadWriteBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

		VkDependencyInfoKHR execCullDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		execCullDependency.bufferMemoryBarrierCount = 1;
		execCullDependency.pBufferMemoryBarriers = &dispatchBarrier;
		execCullDependency.memoryBarrierCount = 1;
		execCullDependency.pMemoryBarriers = &writeToReadWriteBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &execCullDependency );



		vk_descriptor_info ccd[] = {
			Descriptor( meshletIdBuff ),
			Descriptor( meshletCountBuff ),
			Descriptor( drawCmdDbgBuff ),
			Descriptor( drawCountDbgBuff ),
			Descriptor( visibleMeshletsBuff ),
			depthPyramidInfo,
			Descriptor( atomicCounterBuff ),
			Descriptor( dispatchCmdBuff3 ),
			Descriptor( drawCmdAabbsBuff )
		};

		// TODO: wtf binds ?
		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compClusterCullPipe );
		vkCmdPushDescriptorSetWithTemplateKHR(
			cmdBuff, clusterCullCompProgram.descUpdateTemplate, clusterCullCompProgram.pipeLayout, 0, ccd );
		vkCmdBindDescriptorSets(
			cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, clusterCullCompProgram.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff2.hndl, 0 );
	}
	
	{
		VkBufferMemoryBarrier2KHR dispatchBarrier =
			VkMakeBufferBarrier2( dispatchCmdBuff3.hndl,
								  VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH );

		// TODO: write to read and write to write separately ?
		VkMemoryBarrier2KHR writeToReadWriteBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
		writeToReadWriteBarrier.srcStageMask = writeToReadWriteBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
		writeToReadWriteBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR;
		writeToReadWriteBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

		VkDependencyInfoKHR execTriExpDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		execTriExpDependency.bufferMemoryBarrierCount = 1;
		execTriExpDependency.pBufferMemoryBarriers = &dispatchBarrier;
		execTriExpDependency.memoryBarrierCount = 1;
		execTriExpDependency.pMemoryBarriers = &writeToReadWriteBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &execTriExpDependency );



		vk_descriptor_info pushDesc[] = {
			Descriptor( visibleMeshletsBuff ),
			Descriptor( drawCountDbgBuff ),
			Descriptor( indirectMergedIndexBuff ),
			Descriptor( mergedIndexCountBuff ),
			Descriptor( drawMergedCmd ),
			Descriptor( drawMergedCountBuff ),
			Descriptor( atomicCounterBuff )
		};

		assert( meshletTrisBuff.devicePointer && meshletVtxBuff.devicePointer );
		struct { u64 meshletTriAddr; u64 meshletVtxAddr; } pushConst = { meshletTrisBuff.devicePointer, meshletVtxBuff.devicePointer };

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpMergePipe );
		vkCmdPushDescriptorSetWithTemplateKHR( 
			cmdBuff, expMergeCompProgram.descUpdateTemplate, expMergeCompProgram.pipeLayout, 0, pushDesc );
		vkCmdPushConstants( cmdBuff, expMergeCompProgram.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff3.hndl, 0 );
	}

#endif // 0

	VkBufferMemoryBarrier2KHR endCullBarriers[] = {
		VkMakeBufferBarrier2( 
			drawCmdBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountBuff.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCmdDbgBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCountDbgBuff.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawCmdAabbsBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawMergedCmd.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( 
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeBufferBarrier2( 
			indirectMergedIndexBuff.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ),
	};

	VkDependencyInfoKHR dependencyEnd = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyEnd.bufferMemoryBarrierCount = std::size( endCullBarriers );
	dependencyEnd.pBufferMemoryBarriers = endCullBarriers;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependencyEnd );
}

// TODO: redesign
inline static void
DrawIndexedIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&		drawCmds,
	const buffer_data&		camData,
	VkBuffer				drawCmdCount,
	VkBuffer                indexBuff,
	VkIndexType             indexType,
	u32                     maxDrawCallCount,
	const VkClearValue*		clearVals,
	const vk_program&		program,
	bool                    fullPass
){
	vk_label label = { cmdBuff,"Draw Indexed Indirect Pass",{} };

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
	
	if( fullPass )
	{
		vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );
	}

	struct { u64 vtxAddr, transfAddr, drawCmdAddr, camAddr; } push = {
		globVertexBuff.devicePointer, instDescBuff.devicePointer, drawCmds.devicePointer, camData.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push ), &push );


	vkCmdBindIndexBuffer( cmdBuff, indexBuff, 0, indexType );

	vkCmdDrawIndexedIndirectCount( 
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCmdCount, 0, maxDrawCallCount, sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}

// TODO: sexier
inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&      drawCmds,
	VkBuffer				drawCmdCount,
	const vk_program&       program,
	const mat4&             viewProjMat
){
	vk_label label = { cmdBuff,"Draw Indirect Pass",{} };

	VkViewport viewport = { 0, ( float )sc.height, ( float )sc.width, -( float )sc.height, 0, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetViewport( cmdBuff, 0, 1, &viewport );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vk_descriptor_info descriptors[] = { Descriptor( drawCmds ), Descriptor( instDescBuff ), Descriptor( meshletBuff ) };
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

	struct { mat4 viewProj; vec4 color; } push = { viewProjMat, { 255,0,0,0 } };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( push ), &push );

	u32 maxDrawCnt = drawCmds.size / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCmdCount, 0, maxDrawCnt, sizeof( draw_indirect ) );

	vkCmdEndRenderPass( cmdBuff );
}

// TODO: adjust for more draws ?
inline static void
DrawIndirectIndexedMerged(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	const buffer_data&      indexBuff,
	const buffer_data&      drawCmds,
	const buffer_data&      drawCount,
	const buffer_data&      camData,
	const VkClearValue*     clearVals,
	const vk_program&       program,
	const vk_global_descriptor& globDesc = globBindlessDesc
){
	vk_label label = { cmdBuff,"Draw Indexed Indirect Merged Pass",{} };

	constexpr u32 maxDrawCount = 1;

	VkViewport viewport = { 0, ( float )sc.height, ( float )sc.width, -( float )sc.height, 0, 1.0f };
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
	//vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeLayout, 1, 1, &globDesc.set, 0, 0 );

	struct { u64 vtxAddr, transfAddr, camAddr; } push = {
		globVertexBuff.devicePointer, instDescBuff.devicePointer, camData.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push ), &push );

	vkCmdBindIndexBuffer( cmdBuff, indexBuff.hndl, 0, VK_INDEX_TYPE_UINT32 );

	vkCmdDrawIndexedIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}


// TODO: must remake single pass
inline static void
DepthPyramidPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	u64						mipLevelsCount,
	VkSampler				quadMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const vk_program&		program 
){
	assert( 0 );
	u32 dispatchGroupX = ( ( depthTarget.width + 63 ) >> 6 );
	u32 dispatchGroupY = ( ( depthTarget.height + 63 ) >> 6 );

	downsample_info dsInfo = {};
	dsInfo.mips = mipLevelsCount;
	dsInfo.invRes.x = 1.0f / float( depthTarget.width );
	dsInfo.invRes.y = 1.0f / float( depthTarget.height );
	dsInfo.workGroupCount = dispatchGroupX * dispatchGroupY;


	VkImageMemoryBarrier depthReadBarrier = VkMakeImgBarrier( depthTarget.hndl,
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
	depthPyramidDescs[ 1 ] = { quadMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 2 ] = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	for( u64 i = 0; i < rndCtx.depthPyramid.mipCount; ++i )
	{
		depthPyramidDescs[ i + 3 ] = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
	}

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, std::data( depthPyramidDescs ) );

	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( dsInfo ), &dsInfo );

	vkCmdDispatch( cmdBuff, dispatchGroupX, dispatchGroupY, 1 );

	VkImageMemoryBarrier depthWriteBarrier = VkMakeImgBarrier( depthTarget.hndl,
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
	VkSampler				pointMinSampler,
	VkImageView				( &depthMips )[ MAX_MIP_LEVELS ],
	const image&			depthTarget,
	const image&			depthPyramid,
	const vk_program&		program 
){
	vk_label label = { cmdBuff,"HiZ Multi Pass",{} };

	VkImageMemoryBarrier2KHR hizBeginBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR,
			// TODO: all gfx commands ?
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2( 
			depthPyramid.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.imageMemoryBarrierCount = std::size( hizBeginBarriers );
	dependency.pImageMemoryBarriers = hizBeginBarriers;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );


	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	VkDescriptorImageInfo sourceDepth = { pointMinSampler, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };



	VkMemoryBarrier2KHR executionBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR };
	executionBarrier.srcStageMask = executionBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR;
	executionBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR;
	executionBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR;

	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
	{
		if( i != 0 ) sourceDepth = { pointMinSampler, depthMips[ i - 1 ], VK_IMAGE_LAYOUT_GENERAL };

		VkDescriptorImageInfo destDepth = { 0, depthMips[ i ], VK_IMAGE_LAYOUT_GENERAL };
		vk_descriptor_info descriptors[] = { destDepth, sourceDepth };

		vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

		u32 levelWidth = std::max( 1u, u32( depthPyramid.width ) >> i );
		u32 levelHeight = std::max( 1u, u32( depthPyramid.height ) >> i );

		vec2 reduceData = {};
		reduceData.x = levelWidth;
		reduceData.y = levelHeight;

		vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( reduceData ), &reduceData );

		u32 dispatchX = VkGetGroupCount( levelWidth, program.groupSize.x );
		u32 dispatchY = VkGetGroupCount( levelHeight, program.groupSize.y );
		vkCmdDispatch( cmdBuff, dispatchX, dispatchY, 1 );

		// TODO: use memory barriers ?
		VkImageMemoryBarrier2KHR reduceBarrier =
			VkMakeImageBarrier2( depthPyramid.hndl,
								 VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 VK_ACCESS_2_SHADER_READ_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_ASPECT_COLOR_BIT );

		VkDependencyInfoKHR passDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		//passDependency.imageMemoryBarrierCount = 1;
		//passDependency.pImageMemoryBarriers = &reduceBarrier;
		passDependency.memoryBarrierCount = 1;
		passDependency.pMemoryBarriers = &executionBarrier;
		vkCmdPipelineBarrier2KHR( cmdBuff, &passDependency );

	}

	// TODO: do we need ?
	VkImageMemoryBarrier2KHR hizEndBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2(
			depthPyramid.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkDependencyInfoKHR dependencyEnd = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyEnd.imageMemoryBarrierCount = std::size( hizEndBarriers );
	dependencyEnd.pImageMemoryBarriers = hizEndBarriers;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependencyEnd );
}

// TODO: optimize
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

	vkCmdFillBuffer( cmdBuff, shaderGlobalsBuff.hndl, 0, shaderGlobalsBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, shaderGlobalSyncCounterBuff.hndl, 0, shaderGlobalSyncCounterBuff.size, 0u );

	VkBufferMemoryBarrier2KHR zeroInitGlobals[] = {
		VkMakeBufferBarrier2( shaderGlobalsBuff.hndl,
								VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
								VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
		VkMakeBufferBarrier2( shaderGlobalSyncCounterBuff.hndl,
								VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
								VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR )
	};
	VkImageMemoryBarrier2KHR hrdColTargetAcquire = VkMakeImageBarrier2( fboHdrColTrg.hndl,
																		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
																		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
																		VK_ACCESS_2_SHADER_READ_BIT_KHR,
																		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
																		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
																		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
																		VK_IMAGE_ASPECT_COLOR_BIT );

	VkDependencyInfoKHR dependencyAcquire = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyAcquire.imageMemoryBarrierCount = 1;
	dependencyAcquire.pImageMemoryBarriers = &hrdColTargetAcquire;
	dependencyAcquire.bufferMemoryBarrierCount = std::size( zeroInitGlobals );
	dependencyAcquire.pBufferMemoryBarriers = zeroInitGlobals;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependencyAcquire );

	// AVERAGE LUM
	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, avgPipe );

	VkDescriptorImageInfo hdrColTrgInfo = { 0, fboHdrColTrg.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	vk_descriptor_info avgLumDescs[] = { 
		hdrColTrgInfo, 
		Descriptor( avgLumBuff ), 
		Descriptor( shaderGlobalsBuff ),
		Descriptor( shaderGlobalSyncCounterBuff ) 
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, avgProg.descUpdateTemplate, avgProg.pipeLayout, 0, &avgLumDescs[ 0 ] );

	vkCmdPushConstants( cmdBuff, avgProg.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( avgLumInfo ), &avgLumInfo );

	vkCmdDispatch( cmdBuff, 
				   VkGetGroupCount( fboHdrColTrg.width, avgProg.groupSize.x ), 
				   VkGetGroupCount( fboHdrColTrg.height, avgProg.groupSize.y ), 1 );

	// TONEMAPPING w/ GAMMA sRGB
	VkImageMemoryBarrier2KHR scWriteBarrier =
		VkMakeImageBarrier2( scImg,
							 0, 0,
							 VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
							 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
							 VK_IMAGE_LAYOUT_UNDEFINED,
							 VK_IMAGE_LAYOUT_GENERAL,
							 VK_IMAGE_ASPECT_COLOR_BIT );

	VkBufferMemoryBarrier2KHR avgLumReadBarrier =
		VkMakeBufferBarrier2( avgLumBuff.hndl,
							  VK_ACCESS_2_SHADER_WRITE_BIT_KHR, 
							  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
							  VK_ACCESS_2_SHADER_READ_BIT_KHR, 
							  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR );

	VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependency.bufferMemoryBarrierCount = 1;
	dependency.pBufferMemoryBarriers = &avgLumReadBarrier;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &scWriteBarrier;
	vkCmdPipelineBarrier2KHR( cmdBuff, &dependency );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, tonePipe );

	VkDescriptorImageInfo sdrColScInfo = { 0, scView, VK_IMAGE_LAYOUT_GENERAL };
	vk_descriptor_info tonemapDescs[] = {
		hdrColTrgInfo,
		sdrColScInfo,
		Descriptor( avgLumBuff )
	};

	vkCmdPushDescriptorSetWithTemplateKHR( 
		cmdBuff, tonemapProg.descUpdateTemplate, tonemapProg.pipeLayout, 0, &tonemapDescs[ 0 ] );

	assert( ( fboHdrColTrg.width == sc.width ) && ( fboHdrColTrg.height == sc.height ) );
	vkCmdDispatch( cmdBuff,
				   VkGetGroupCount( fboHdrColTrg.width, avgProg.groupSize.x ),
				   VkGetGroupCount( fboHdrColTrg.height, avgProg.groupSize.y ), 1 );

}

// TODO: color depth toggle stuff
inline static void
DebugDrawPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			vkPipeline,
	VkRenderPass		vkRndPass,
	VkFramebuffer	    offscreenFbo,
	const buffer_data&  drawBuff,
	const vk_program&   program,
	const mat4&         projView,
	range		        drawRange
){
	vk_label label = { cmdBuff,"Dbg Draw Pass",{} };

	VkViewport viewport = { 0, ( float )sc.height, ( float )sc.width, -( float )sc.height, 0, 1.0f };
	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetViewport( cmdBuff, 0, 1, &viewport );
	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vk_descriptor_info pushDescs[] = { Descriptor( drawBuff ) };
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, pushDescs );
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( mat4 ), &projView );

	vkCmdDraw( cmdBuff, drawRange.size, 1, drawRange.offset, 0 );

	vkCmdEndRenderPass( cmdBuff );
}


// TODO: pass cam data via push const
void HostFrames( const global_data* globs, bool bvDraw, bool freeCam, float dt )
{
	using namespace DirectX;
	//u64 timestamp = SysGetFileTimestamp( "D:\\EichenRepos\\QiY\\QiY\\Shaders\\pbr.frag.glsl" );

	u64 currentFrameIdx = rndCtx.vFrameIdx;
	const virtual_frame& thisVFrame = rndCtx.vrtFrames[ rndCtx.vFrameIdx ];
	// TODO: don't modulo frameIndex ?
	rndCtx.vFrameIdx = ( rndCtx.vFrameIdx + 1 ) % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;

	VK_CHECK( VK_INTERNAL_ERROR( vkWaitForFences( dc.device, 1, &thisVFrame.hostSyncFence, true, UINT64_MAX ) > VK_TIMEOUT ) );
	VK_CHECK( vkResetFences( dc.device, 1, &thisVFrame.hostSyncFence ) );

	VK_CHECK( vkResetCommandPool( dc.device, thisVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );


	VkCommandBufferBeginInfo cmdBufBegInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	cmdBufBegInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer( thisVFrame.cmdBuff, &cmdBufBegInfo );


	// TODO: swapchain resize ?
	if( !rndCtx.depthTarget.hndl )
	{
		VkImageUsageFlags depthTargetUsg = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		rndCtx.depthTarget =
			VkCreateAllocBindImage( rndCtx.desiredDepthFormat, depthTargetUsg, { sc.width,sc.height,1 }, 1, vkAlbumArena );
		VkDbgNameObj( rndCtx.depthTarget.hndl, dc.device, "Img_Render_Target_Depth" );

		u32 hiZWidth = 0;
		u32 hiZHeight = 0;
		u32 hiZMipCount = 0;
		// TODO: place depth pyr elsewhere 
		if constexpr( !multiPassDepthPyramid )
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
			u32 squareDim = std::min( hiZWidth, hiZHeight );
			hiZWidth = hiZHeight = squareDim;
			hiZMipCount = GetImgMipCountForPow2( hiZWidth, hiZHeight );
		}
		VK_CHECK( VK_INTERNAL_ERROR( !( hiZMipCount < MAX_MIP_LEVELS ) ) );
		VkImageUsageFlags hiZUsg = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
			| VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		VkFormat depthFormat = VK_FORMAT_R32_SFLOAT;
		rndCtx.depthPyramid = VkCreateAllocBindImage( depthFormat, hiZUsg, { hiZWidth, hiZHeight, 1 }, hiZMipCount, vkAlbumArena );
		VkDbgNameObj( rndCtx.depthPyramid.hndl, dc.device, "Img_Depth_Pyramid" );

		for( u64 i = 0; i < rndCtx.depthPyramid.mipCount; ++i )
		{
			rndCtx.depthPyramidChain[ i ] =
				VkMakeImgView( dc.device, rndCtx.depthPyramid.hndl, rndCtx.depthPyramid.nativeFormat, i, 1 );
		}


		VkImageMemoryBarrier2KHR initBarriers[] = {
		VkMakeImageBarrier2(
			rndCtx.depthTarget.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),
		VkMakeImageBarrier2(
			rndCtx.depthPyramid.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
		};
		VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependency.imageMemoryBarrierCount = std::size( initBarriers );
		dependency.pImageMemoryBarriers = initBarriers;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );
	}

	if( !rndCtx.colorTarget.hndl )
	{
		VkImageUsageFlags colorTargetUsg =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		rndCtx.colorTarget =
			VkCreateAllocBindImage( rndCtx.desiredColorFormat, colorTargetUsg, { sc.width,sc.height,1 }, 1, vkAlbumArena );
		VkDbgNameObj( rndCtx.colorTarget.hndl, dc.device, "Img_Render_Target_Color" );

		VkImageMemoryBarrier2KHR initBarrier = VkMakeImageBarrier2(
			rndCtx.colorTarget.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT );
		VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &initBarrier;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );
	}

	if( !rndCtx.offscreenFbo )
	{
		VkImageView fboAttach[] = { rndCtx.colorTarget.view, rndCtx.depthTarget.view };
		rndCtx.offscreenFbo = VkMakeFramebuffer( dc.device, rndCtx.renderPass, fboAttach, std::size( fboAttach ), sc.width, sc.height );
	}

	// TODO: async, multi-threaded, etc
	static bool rescUploaded = 0;
	if( !rescUploaded )
	{
		VkUploadResources( thisVFrame.cmdBuff, entities );
		rescUploaded = 1;
	
		global_bdas bdas = {};
		bdas.vtxAddr = globVertexBuff.devicePointer;
		bdas.idxAddr = indexBuff.devicePointer;
		bdas.meshDescAddr = meshBuff.devicePointer;
		bdas.lightsDescAddr = lightsBuff.devicePointer;
		bdas.meshletsAddr = meshletBuff.devicePointer;
		bdas.meshletsVtxAddr = meshletVtxBuff.devicePointer;
		bdas.meshletsTriAddr = meshletTrisBuff.devicePointer;
		bdas.mtrlsAddr = materialsBuff.devicePointer;
		bdas.instDescAddr = instDescBuff.devicePointer;
	
		assert( bdasUboBuff.hostVisible );
		std::memcpy( bdasUboBuff.hostVisible, &bdas, sizeof( bdas ) );
	
	
		// NOTE: UpdateDescriptors
		std::vector<VkWriteDescriptorSet> descUpdates;
	
		VkDescriptorBufferInfo bdaDesc = Descriptor( bdasUboBuff );
		VkWriteDescriptorSet bdaUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		bdaUpdate.dstSet = globBindlessDesc.set;
		bdaUpdate.dstBinding = VK_GLOBAL_SLOT_UNIFORM_BUFFER;
		bdaUpdate.dstArrayElement = 1;
		bdaUpdate.descriptorCount = 1;
		bdaUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_UNIFORM_BUFFER ];
		bdaUpdate.pBufferInfo = &bdaDesc;
	
		descUpdates.push_back( bdaUpdate );
	
	
		rndCtx.pbrTexSampler = VkMakeSampler( dc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );
		VkDescriptorImageInfo samplerDesc = { rndCtx.pbrTexSampler };
		VkWriteDescriptorSet samplerUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		samplerUpdate.dstSet = globBindlessDesc.set;
		samplerUpdate.dstBinding = VK_GLOBAL_SLOT_SAMPLER;
		samplerUpdate.dstArrayElement = 0;
		samplerUpdate.descriptorCount = 1;
		samplerUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLER ];
		samplerUpdate.pImageInfo = &samplerDesc;
	
		descUpdates.push_back( samplerUpdate );
	
		std::vector<VkDescriptorImageInfo> texDescs;
		texDescs.reserve( std::size( textures.rsc ) );
		for( const image& i : textures.rsc )
		{
			texDescs.push_back( { 0, i.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL } );
		}

		VkWriteDescriptorSet texUpdates = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		texUpdates.dstSet = globBindlessDesc.set;
		texUpdates.dstBinding = VK_GLOBAL_SLOT_SAMPLED_IMAGE;
		texUpdates.dstArrayElement = 0;
		texUpdates.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];
		texUpdates.descriptorCount = std::size( texDescs );
		texUpdates.pImageInfo = std::data( texDescs );

		descUpdates.push_back( texUpdates );
	
		

		//descUpdates.push_back( globalDataUpdate );


		vkUpdateDescriptorSets( dc.device, std::size( descUpdates ), std::data( descUpdates ), 0, 0 );


		// TODO: remove from here
		XMMATRIX t = XMMatrixMultiply( XMMatrixScaling( 100.0f, 60.0f, 20.0f ), XMMatrixTranslation( 20.0f, -10.0f, -60.0f ) );
		XMFLOAT4 boxVertices[ 8 ] = {};
		TrnasformBoxVertices( t, { -1.0f,-1.0f,-1.0f }, { 1.0f,1.0f,1.0f }, boxVertices );
		std::span<dbg_vertex> occlusionBoxSpan = { (dbg_vertex*) vkDbgCtx.dbgTrisBuff.hostVisible,boxTrisVertexCount };
		assert( std::size( occlusionBoxSpan ) == std::size( boxTrisIndices ) );
		for( u64 i = 0; i < std::size( occlusionBoxSpan ); ++i )
		{
			occlusionBoxSpan[ i ] = { boxVertices[ boxTrisIndices[ i ] ],{0.000004f,0.000250f,0.000123f,1.0f} };
		}
	}
	
	static bool initBuffers = 0;
	if( !initBuffers )
	{
		vkCmdFillBuffer( thisVFrame.cmdBuff, drawVisibilityBuff.hndl, 0, drawVisibilityBuff.size, 1U );
		vkCmdFillBuffer( thisVFrame.cmdBuff, depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size, 0u );
		// TODO: rename 
		vkCmdFillBuffer( thisVFrame.cmdBuff, atomicCounterBuff.hndl, 0, atomicCounterBuff.size, 0u );

		VkBufferMemoryBarrier2KHR initBuffersBarriers[] = {
			VkMakeBufferBarrier2( drawVisibilityBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR, 
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
			VkMakeBufferBarrier2( depthAtomicCounterBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),
			VkMakeBufferBarrier2( atomicCounterBuff.hndl,
									VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
									VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
									VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR )
		};
	
		VkDependencyInfoKHR initBuffsDependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		initBuffsDependency.bufferMemoryBarrierCount = std::size( initBuffersBarriers );
		initBuffsDependency.pBufferMemoryBarriers = initBuffersBarriers;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &initBuffsDependency );

		initBuffers = 1;
	}
	
	// TODO: run 1 for every frame in flight
	VkDescriptorBufferInfo uboInfo = { thisVFrame.frameData.hndl, 0, sizeof( *globs ) };

	VkWriteDescriptorSet globalDataUpdate = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	globalDataUpdate.dstSet = globBindlessDesc.set;
	globalDataUpdate.dstBinding = VK_GLOBAL_SLOT_UNIFORM_BUFFER;
	globalDataUpdate.dstArrayElement = 0;
	globalDataUpdate.descriptorCount = 1;
	globalDataUpdate.descriptorType = globalDescTable[ VK_GLOBAL_SLOT_UNIFORM_BUFFER ];
	globalDataUpdate.pBufferInfo = &uboInfo;
	// TODO: remove this ?
	vkUpdateDescriptorSets( dc.device, 1, &globalDataUpdate, 0, 0 );

	std::memcpy( thisVFrame.frameData.hostVisible, ( u8* )globs, sizeof( *globs ) );

	VkClearValue clearVals[ 2 ] = {};

	XMMATRIX proj = XMLoadFloat4x4A( &globs->proj );
	XMMATRIX xmProjView = XMMatrixMultiply( XMLoadFloat4x4A( &globs->activeView ), proj );
	mat4 projView;
	XMStoreFloat4x4A( &projView, xmProjView );

	if( !freeCam )
	{
		//DrawIndexedIndirectPass( thisVFrame.cmdBuff,
		//						 gfxZPrepass,
		//						 rndCtx.renderPass,
		//						 rndCtx.offscreenFbo,
		//						 drawCmdBuff,
		//						 thisVFrame.frameData,
		//						 drawCountBuff.hndl,
		//						 indexBuff.hndl,
		//						 VK_INDEX_TYPE_UINT32,
		//						 instDescBuff.size / sizeof( instance_desc ),
		//						 clearVals,
		//						 zPrepassProgram,
		//						 false );
		
		DrawIndirectIndexedMerged(
			thisVFrame.cmdBuff,
			gfxZPrepass,
			rndCtx.renderPass,
			rndCtx.offscreenFbo,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			thisVFrame.frameData,
			clearVals,
			zPrepassProgram );

		DebugDrawPass( thisVFrame.cmdBuff,
					   vkDbgCtx.drawAsTriangles,
					   rndCtx.render2ndPass,
					   rndCtx.offscreenFbo,
					   vkDbgCtx.dbgTrisBuff,
					   vkDbgCtx.pipeProg,
					   projView,
					   { 0,boxTrisVertexCount } );
		
		DepthPyramidMultiPass(
			thisVFrame.cmdBuff,
			rndCtx.compHiZPipeline,
			rndCtx.quadMinSampler,
			rndCtx.depthPyramidChain,
			rndCtx.depthTarget,
			rndCtx.depthPyramid,
			depthPyramidMultiProgram );
		
		
		VkBufferMemoryBarrier2KHR clearDrawCountBarrier = VkMakeBufferBarrier2(
			//drawCountBuff.hndl,
			drawMergedCountBuff.hndl,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR );
		
		VkDependencyInfoKHR dependency = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
		dependency.bufferMemoryBarrierCount = 1;
		dependency.pBufferMemoryBarriers = &clearDrawCountBarrier;
		vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependency );

		// TODO: Aaltonen double draw ? ( not double culling )
		// TODO: merge up to 256 meshes 
		CullPass( thisVFrame.cmdBuff, rndCtx.compPipeline, cullCompProgram, rndCtx.depthPyramid, rndCtx.quadMinSampler );
	}

	// Emit depth + HzB
	
	//DrawIndexedIndirectPass( thisVFrame.cmdBuff,
	//						 rndCtx.gfxPipeline,
	//						 rndCtx.renderPass,
	//						 rndCtx.offscreenFbo,
	//						 drawCmdBuff,
	//						 thisVFrame.frameData,
	//						 drawCountBuff.hndl,
	//						 indexBuff.hndl,
	//						 VK_INDEX_TYPE_UINT32,
	//						 instDescBuff.size / sizeof( instance_desc ),
	//						 clearVals,
	//						 gfxOpaqueProgram,
	//						 true );

	//DrawIndexedIndirectPass(
	//	thisVFrame.cmdBuff,
	//	rndCtx.gfxMeshletPipeline,
	//	rndCtx.renderPass,
	//	rndCtx.offscreenFbo,
	//	drawCmdDbgBuff,
	//	drawCountDbgBuff.hndl,
	//	meshletTrisBuff.hndl,
	//	VK_INDEX_TYPE_UINT8_EXT,
	//	meshletBuff.size / sizeof( meshlet ),
	//	clearVals,
	//	gfxMeshletProgram );

	vkCmdBindDescriptorSets(
		thisVFrame.cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, gfxMergedProgram.pipeLayout, 1, 1, &globBindlessDesc.set, 0, 0 );

	DrawIndirectIndexedMerged(
		thisVFrame.cmdBuff,
		rndCtx.gfxMergedPipeline,
		rndCtx.renderPass,
		rndCtx.offscreenFbo,
		indirectMergedIndexBuff,
		drawMergedCmd,
		drawMergedCountBuff,
		thisVFrame.frameData,
		clearVals,
		gfxMergedProgram );

	DebugDrawPass( thisVFrame.cmdBuff,
				   vkDbgCtx.drawAsTriangles,
				   rndCtx.render2ndPass,
				   rndCtx.offscreenFbo,
				   vkDbgCtx.dbgTrisBuff,
				   vkDbgCtx.pipeProg,
				   projView,
				   { 0,boxTrisVertexCount } );

	// NOTE: inv( A * B ) = inv B * inv A
	XMMATRIX invFrustMat = XMMatrixMultiply( XMLoadFloat4x4A( &globs->mainView ), proj );
	XMVECTOR det = XMMatrixDeterminant( invFrustMat );
	assert( XMVectorGetX( det ) );
	XMMATRIX frustMat = XMMatrixInverse( &det, invFrustMat );
	// TODO: might need to sync or double buffer
	dbgLineGeomCache = ComputeSceneDebugBoundingBoxes( frustMat, entities );
	std::memcpy( vkDbgCtx.dbgLinesBuff.hostVisible, std::data( dbgLineGeomCache ), BYTE_COUNT( dbgLineGeomCache ) );

	// TODO: remove the depth target from these ?
	// TODO: rethink
	if( dbgDraw && ( freeCam || bvDraw ) )
	{
		////u64 frustBoxOffset = ( std::size( entities.instAabbs ) + std::size( entities.meshletAabbs ) ) * boxLineVertexCount;
		u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;
		
		range drawRange = {};
		drawRange.offset = bvDraw ? 0 : frustBoxOffset;
		drawRange.size = ( freeCam && bvDraw ) ? std::size( dbgLineGeomCache ) : ( freeCam ? boxLineVertexCount : frustBoxOffset );


		DebugDrawPass( thisVFrame.cmdBuff,
					   vkDbgCtx.drawAsLines,
					   rndCtx.render2ndPass,
					   rndCtx.offscreenFbo,
					   vkDbgCtx.dbgLinesBuff,
					   vkDbgCtx.pipeProg,
					   projView,
					   drawRange );

		if( bvDraw )
		{
			DrawIndirectPass( thisVFrame.cmdBuff,
							  gfxDrawIndirDbg,
							  rndCtx.render2ndPass,
							  rndCtx.offscreenFbo,
							  drawCmdAabbsBuff,
							  drawCountDbgBuff.hndl,
							  dbgDrawProgram,
							  projView );
		}
		
	}

	DepthPyramidMultiPass(
		thisVFrame.cmdBuff,
		rndCtx.compHiZPipeline,
		rndCtx.quadMinSampler,
		rndCtx.depthPyramidChain,
		rndCtx.depthTarget,
		rndCtx.depthPyramid,
		depthPyramidMultiProgram );



	u32 imgIdx;
	VK_CHECK( vkAcquireNextImageKHR( dc.device, sc.swapchain, UINT64_MAX, thisVFrame.canGetImgSema, 0, &imgIdx ) );

	ToneMappingWithSrgb( thisVFrame.cmdBuff,
						 rndCtx.compAvgLumPipe, 
						 rndCtx.compTonemapPipe,
						 avgLumCompProgram, 
						 rndCtx.colorTarget,
						 tonemapCompProgram,
						 sc.imgs[ imgIdx ],
						 sc.imgViews[ imgIdx ],
						 dt );
	
	VkImageMemoryBarrier2KHR hrdColTargetAcquire = VkMakeImageBarrier2(
		rndCtx.colorTarget.hndl,
		VK_ACCESS_2_SHADER_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		0, 0,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
		VK_IMAGE_ASPECT_COLOR_BIT );

	VkDependencyInfoKHR dependencyAcquire = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyAcquire.imageMemoryBarrierCount = 1;
	dependencyAcquire.pImageMemoryBarriers = &hrdColTargetAcquire;
	vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependencyAcquire );

	VkImageMemoryBarrier2KHR presentWaitBarrier = VkMakeImageBarrier2(
		sc.imgs[ imgIdx ],
		VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		0, 0,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_IMAGE_ASPECT_COLOR_BIT );

	VkDependencyInfoKHR dependencyPresent = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR };
	dependencyPresent.imageMemoryBarrierCount = 1;
	dependencyPresent.pImageMemoryBarriers = &presentWaitBarrier;
	vkCmdPipelineBarrier2KHR( thisVFrame.cmdBuff, &dependencyPresent );

	VK_CHECK( vkEndCommandBuffer( thisVFrame.cmdBuff ) );

	VkSubmitInfo submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &thisVFrame.canGetImgSema;
	VkPipelineStageFlags waitDstStageMsk = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	submitInfo.pWaitDstStageMask = &waitDstStageMsk;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &thisVFrame.cmdBuff;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &thisVFrame.canPresentSema;
	// NOTE: queue submit has implicit host sync for trivial stuff
	VK_CHECK( vkQueueSubmit( dc.gfxQueue, 1, &submitInfo, thisVFrame.hostSyncFence ) );

	VkPresentInfoKHR presentInfo = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &thisVFrame.canPresentSema;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &sc.swapchain;
	presentInfo.pImageIndices = &imgIdx;
	VK_CHECK( vkQueuePresentKHR( dc.gfxQueue, &presentInfo ) );
}

void VkBackendKill()
{
	// NOTE: SHOULDN'T need to check if( VkObj ). Can't create -> app fail
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

#undef HTVK_NO_SAMPLER_REDUCTION
#undef VK_APPEND_DESTROYER
#undef VK_CHECK

