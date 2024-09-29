#include "vk_common.hpp"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <string_view>
#include <charconv>
#include <span>
#include <functional>

#include "handles.hpp"

#include "r_data_structs.h"
#include "geometry.hpp"
// TODO: use own allocator

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
#include "math_util.hpp"

#include "vk_resources.hpp"
#include "vk_sync.hpp"
#include "vk_utils.hpp"
#include "vk_instance.hpp"
#include "vk_device.hpp"
#include "vk_descriptors.hpp"
#include "vk_swapchain.hpp"


#include "vk_pipelines.hpp"
#include "vk_shaders.hpp"
// TODO: golbal file
//====================CONSTS====================//
constexpr u64 VK_MAX_FRAMES_IN_FLIGHT_ALLOWED = 2;
constexpr u32 NOT_USED_IDX = -1; // TODO: DUPLIACTE 
constexpr u32 OBJ_CULL_WORKSIZE = 64;
constexpr u32 MLET_CULL_WORKSIZE = 256;


//==============================================//
// TODO: cvars
//====================CVARS====================//

//==============================================//
// TODO: compile time switches
//==============CONSTEXPR_SWITCH==============//
constexpr bool multiPassDepthPyramid = 1;
static_assert( multiPassDepthPyramid );

//constexpr bool worldLeftHanded = 1;

constexpr bool dbgDraw = true;
//==============================================//

#include "metaprogramming.hpp"

#ifdef VK_USE_PLATFORM_WIN32_KHR
// TODO: where to place these ?
extern HINSTANCE hInst;
extern HWND hWnd;

inline VkSurfaceKHR VkMakeSurface( VkInstance vkInst, HINSTANCE hInst, HWND hWnd )
{
	VkWin32SurfaceCreateInfoKHR surfInfo = { 
		.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, .hinstance = hInst, .hwnd = hWnd 
	};
	VkSurfaceKHR vkSurf;
	VK_CHECK( vkCreateWin32SurfaceKHR( vkInst, &surfInfo, 0, &vkSurf ) );
	return vkSurf;
}
#else
#error Must provide OS specific Surface
#endif  // VK_USE_PLATFORM_WIN32_KHR


// TODO:
struct vk_renderer_config
{
	static constexpr VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	static constexpr VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	static constexpr VkFormat		desiredSwapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
	static constexpr u8             MAX_FRAMES_ALLOWED = 2;

	u16             renderWidth;
	u16             rednerHeight;
	u8              maxAllowedFramesInFlight = 2;
};

import r_dxc_compiler;

using dxc_options = std::initializer_list<LPCWSTR>;

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
	vkSamplerInfo.mipmapMode = VkGetMipmapTypeFromGltf( config.min );
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
) {
	return { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = imgType,
		.format = imgFormat,
		.extent = imgExtent,
		.mipLevels = mipLevels,
		.arrayLayers = layerCount,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usageFlags,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
}

inline VkImageCreateInfo VkGetImageInfoFromMetadata( const image_metadata& meta, VkImageUsageFlags usageFlags )
{
	return { 
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VkGetImageType( meta.type ),
		.format = VkGetFormat( meta.format ),
		.extent = { u32( meta.width ), u32( meta.height ), 1 },
		.mipLevels = meta.mipCount,
		.arrayLayers = meta.layerCount,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = usageFlags,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
}


#define HTVK_NO_SAMPLER_REDUCTION VK_SAMPLER_REDUCTION_MODE_MAX_ENUM

// TODO: AddrMode ?
inline VkSampler VkMakeSampler(
	VkDevice				vkDevice,
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
	VK_CHECK( vkCreateSampler( vkDevice, &samplerInfo, 0, &sampler ) );
	return sampler;
}


#include "imgui/imgui.h"

// TODO: move spv shaders into exe folder
struct imgui_vk_context
{
	// TODO: make part of frame resources 
	vk_buffer                 vtxBuffs[ 2 ];
	vk_buffer                 idxBuffs[ 2 ];
	vk_image                       fontsImg;

	VkDescriptorSetLayout       descSetLayout;
	VkPipelineLayout            pipelineLayout;
	VkDescriptorUpdateTemplate  descTemplate;
	VkPipeline	                pipeline;
	VkSampler                   fontSampler;
	VkFormat colDstFormat;
};

static imgui_vk_context imguiVkCtx;


inline auto ImguiPreparePipeline( VkDevice vkDevice, VkSampler fontSampler )
{
	struct {
		VkDescriptorSetLayout descSetLayout = {};
		VkPipelineLayout pipelineLayout = {};
		VkDescriptorUpdateTemplate descTemplate = {};
	} retval;

	VkDescriptorSetLayoutBinding descSetBindings[] = {
		{
			.binding = 0,
			.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		},
		{
			.binding = 1,
			.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			.descriptorCount = 1,
			.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
			.pImmutableSamplers = &fontSampler,

		}
	};

	VkDescriptorSetLayoutCreateInfo descSetInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = ( u32 ) std::size( descSetBindings ),
		.pBindings = descSetBindings
	};

	
	VK_CHECK( vkCreateDescriptorSetLayout( vkDevice, &descSetInfo, 0, &retval.descSetLayout ) );

	VkPushConstantRange pushConst = { VK_SHADER_STAGE_VERTEX_BIT, 0,sizeof( float ) * 4 };
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &retval.descSetLayout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConst,
	};
	VK_CHECK( vkCreatePipelineLayout( vkDevice, &pipelineLayoutInfo, 0, &retval.pipelineLayout ) );

	VkDescriptorUpdateTemplateEntry entries[] = {
		{
			.dstBinding = descSetBindings[ 0 ].binding,
			.descriptorCount = descSetBindings[ 0 ].descriptorCount,
			.descriptorType = descSetBindings[ 0 ].descriptorType,
			.offset = 0,
			.stride = sizeof( vk_descriptor_info ),
		},
		{
			.dstBinding = descSetBindings[ 1 ].binding,
			.descriptorCount = descSetBindings[ 1 ].descriptorCount,
			.descriptorType = descSetBindings[ 1 ].descriptorType,
			.offset = sizeof( vk_descriptor_info ),
			.stride = sizeof( vk_descriptor_info ),
		}
	};

	VkDescriptorUpdateTemplateCreateInfo templateInfo = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
		.descriptorUpdateEntryCount = ( u32 ) std::size( entries ),
		.pDescriptorUpdateEntries = std::data( entries ),
		.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR,
		.descriptorSetLayout = retval.descSetLayout,
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.pipelineLayout = retval.pipelineLayout,
	};
	VK_CHECK( vkCreateDescriptorUpdateTemplate( vkDevice, &templateInfo, 0, &retval.descTemplate ) );

	auto&& [e0, e1, e2] = std::forward<decltype(retval)>(retval);
	return std::make_tuple( e0, e1, e2 );
}
// TODO: buffer resize ?
// TODO: vk formats 
static inline imgui_vk_context ImguiMakeVkContext(
	const vk_device& vkDc,
	VkFormat colDstFormat
) {
	imgui_vk_context ctx = {};
	ctx.fontSampler = VkMakeSampler( vkDc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );
	std::tie( ctx.descSetLayout, ctx.pipelineLayout, ctx.descTemplate ) = ImguiPreparePipeline( vkDc.device, ctx.fontSampler );

	vk_shader vert = VkLoadShader( "Shaders/shader_imgui.vert.spv", vkDc.device );
	vk_shader frag = VkLoadShader( "Shaders/shader_imgui.frag.spv", vkDc.device );

	vk_gfx_pipeline_state guiState = {
		.polyMode = VK_POLYGON_MODE_FILL,
		.cullFlags = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.depthWrite = false,
		.depthTestEnable = false,
		.blendCol = true,
	};
	ctx.pipeline = VkMakeGfxPipeline(
		vkDc.device, ctx.pipelineLayout, vert.module, frag.module, &colDstFormat, VK_FORMAT_D32_SFLOAT, guiState, "Pipe_Gfx_ImGui");

	ctx.vtxBuffs[ 0 ] = VkCreateAllocBindBuffer( 
		vkDc, { "Buff_Vtx_Imgui0", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT ,64 * KB, 1 }, vk_mem_usage::HOST_VISIBLE);
	ctx.idxBuffs[ 0 ] = VkCreateAllocBindBuffer( 
		vkDc, { "Buff_Idx_Imgui0", VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 64 * KB, 1 }, vk_mem_usage::HOST_VISIBLE );
	ctx.vtxBuffs[ 1 ] = VkCreateAllocBindBuffer(
		vkDc, { "Buff_Vtx_Imgui1", VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 64 * KB, 1 }, vk_mem_usage::HOST_VISIBLE );
	ctx.idxBuffs[ 1 ] = VkCreateAllocBindBuffer( 
		vkDc, { "Buff_Idx_Imgui1", VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 64 * KB, 1 }, vk_mem_usage::HOST_VISIBLE );

	ctx.fontsImg = VkCreateAllocBindImage(
		vkDc,
		{
			.name = "Img_Imgui_Fonts",
			.format = VK_FORMAT_R8G8B8A8_UNORM, // Don't hardcode ?
			.usg = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, // Don't hardcode ?
			.width = ( u16 ) ImGui::GetIO().Fonts->TexWidth,
			.height = ( u16 ) ImGui::GetIO().Fonts->TexHeight,
			.layerCount = 1,
			.mipCount = 1
		} 
	);

	return ctx;
}

inline auto ImguiGetFontImage()
{
	struct {
		u8* pixels;
		u32 width;
		u32 height;
	} retval;

	ImGui::GetIO().Fonts->GetTexDataAsRGBA32( &retval.pixels, ( int* ) &retval.width, ( int* ) &retval.height );
	return retval;
}


// TODO: use instancing 4 drawing ?
struct debug_context
{
	vk_buffer dbgLinesBuff;
	vk_buffer dbgTrisBuff;
	vk_program	pipeProg;
	VkPipeline	drawAsLines;
	VkPipeline	drawAsTriangles;
};

static debug_context vkDbgCtx;


// TODO: query for gpu props ?
// TODO: dbgGeom buffer size based on what ?
// TODO: shader rename
static inline debug_context VkMakeDebugContext( VkDevice vkDevice, const VkPhysicalDeviceProperties& gpuProps )
{
	debug_context dbgCtx = {};

	vk_shader vert = VkLoadShader( "Shaders/v_cpu_dbg_draw.vert.spv", vkDevice );
	vk_shader frag = VkLoadShader( "Shaders/f_pass_col.frag.spv", vkDevice );

	dbgCtx.pipeProg = VkMakePipelineProgram( 
		vkDevice, gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vert, &frag }, vk.descMngr.setLayout );

	static_assert( worldLeftHanded );
	vk_gfx_pipeline_state lineDrawPipelineState = {
		.polyMode = VK_POLYGON_MODE_LINE,
		.cullFlags = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
		.depthWrite = VK_FALSE,
		.depthTestEnable = VK_FALSE,
		.blendCol = VK_FALSE,
	};
	dbgCtx.drawAsLines = VkMakeGfxPipeline( vkDevice, dbgCtx.pipeProg.pipeLayout, vert.module, frag.module,
											&rndCtx.desiredColorFormat, rndCtx.desiredDepthFormat, lineDrawPipelineState, "Pipe_GFX_DbgLines");

	vk_gfx_pipeline_state triDrawPipelineState = {
		.polyMode = VK_POLYGON_MODE_FILL,
		.cullFlags = VK_CULL_MODE_NONE,// VK_CULL_MODE_FRONT_BIT;
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.primTopology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.depthWrite = VK_TRUE,
		.depthTestEnable = VK_TRUE,
		.blendCol = VK_TRUE,
	};
	dbgCtx.drawAsTriangles = VkMakeGfxPipeline( vkDevice, dbgCtx.pipeProg.pipeLayout, vert.module, frag.module,
												0, rndCtx.desiredDepthFormat, triDrawPipelineState, "Pipe_GFX_DbgLinesTriangles" );


	vkDestroyShaderModule( vkDevice, vert.module, 0 );
	vkDestroyShaderModule( vkDevice, frag.module, 0 );

	dbgCtx.dbgLinesBuff = VkCreateAllocBindBuffer( 512 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena, dc.gpu );
	dbgCtx.dbgTrisBuff = VkCreateAllocBindBuffer( 128 * KB, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vkHostComArena, dc.gpu );

	return dbgCtx;
}


static entities_data entities;
static std::vector<dbg_vertex> dbgLineGeomCache;


static vk_buffer proxyGeomBuff;
static vk_buffer proxyIdxBuff;

static vk_buffer screenspaceBoxBuff;

static vk_buffer globVertexBuff;
static vk_buffer indexBuff;
static vk_buffer meshBuff;

static vk_buffer meshletBuff;
static vk_buffer meshletDataBuff;

// TODO:
static vk_buffer transformsBuff;

static vk_buffer materialsBuff;
static vk_buffer instDescBuff;
static vk_buffer lightsBuff;


static vk_buffer intermediateIndexBuff;
static vk_buffer indirectMergedIndexBuff;

static vk_buffer drawCmdBuff;
static vk_buffer drawCmdAabbsBuff;
static vk_buffer drawCmdDbgBuff;
static vk_buffer drawVisibilityBuff;

static vk_buffer drawMergedCmd;

constexpr char glbPath[] = "D:\\3d models\\cyberbaron\\cyberbaron.glb";
constexpr char drakPath[] = "Assets/cyberbaron.drak";

constexpr u64 randSeed = 42;
constexpr u64 drawCount = 5;
constexpr u64 lightCount = 100;
constexpr float sceneRad = 40.0f;


enum MMF_OPENFLAGS : u8
{
	OPN_READ = 0,
	OPN_READWRITE
};

// TODO: file system should ref count
struct win32_mmaped_file_handle
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = INVALID_HANDLE_VALUE;
	std::span<u8> dataView;

	~win32_mmaped_file_handle()
	{
		UnmapViewOfFile( std::data( dataView ) );
		CloseHandle( hFileMapping );
		CloseHandle( hFile );
	}
};

win32_mmaped_file_handle OpenMmappedFile( std::string_view fileName, MMF_OPENFLAGS oflags )
{
	DWORD dwflags = ( OPN_READWRITE == oflags ) ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ;
	HANDLE hFile = CreateFileA(
		std::data( fileName ), dwflags, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, 0 );
	//WIN_CHECK( hFile == INVALID_HANDLE_VALUE );

	DWORD dwFileSizeHigh;
	size_t qwFileSize = GetFileSize( hFile, &dwFileSizeHigh );
	qwFileSize += ( size_t( dwFileSizeHigh ) << 32 );
	//WIN_CHECK( qwFileSize == 0 );

	DWORD dwFlagsFileMapping = ( OPN_READWRITE == oflags ) ? PAGE_READWRITE : PAGE_READONLY;
	HANDLE hFileMapping = CreateFileMappingA( hFile, 0, dwFlagsFileMapping, 0, 0, 0 );
	//WIN_CHECK( !hFileMapping );

	//DWORD dwFlagsView = (OPN_WRITE == oflags || OPN_READWRITE == oflags) ? FILE_MAP_WRITE: FILE_MAP_READ;
	DWORD dwFlagsView = ( OPN_READWRITE == oflags ) ? FILE_MAP_WRITE : FILE_MAP_READ;
	u8* pData = ( u8* ) MapViewOfFile( hFileMapping, dwFlagsView, 0, 0, qwFileSize );

	//WIN_CHECK( !pData );

	return win32_mmaped_file_handle{ hFile, hFileMapping, std::span<u8>{pData, qwFileSize} };
}


static inline void VkUploadResources( VkCommandBuffer cmdBuff, entities_data& entities, u64 currentFrameId )
{
	win32_mmaped_file_handle hMmf = OpenMmappedFile( drakPath, OPN_READ );
	std::span<u8> binaryData = hMmf.dataView;

	// TODO: add renderable_instances
	// TODO: extra checks and stuff ?
	// TODO: ensure resources of the same type are contiguous ?
	if( 0 )
	{
		std::vector<u8> binaryData;
		binaryData = SysReadFile( drakPath );
		if( std::size( binaryData ) == 0 )
		{
			std::vector<u8> fileData = SysReadFile( glbPath );
			CompileGlbAssetToBinary( fileData, binaryData );
			// TODO: does this override ?
			SysWriteToFile( drakPath, std::data( binaryData ), std::size( binaryData ) );
		}
	}

	const drak_file_footer& fileFooter =
		*( drak_file_footer* ) ( std::data( binaryData ) + std::size( binaryData ) - sizeof( drak_file_footer ) );
	{
		using namespace std;
		assert( "DRK"sv == fileFooter.magik );
	}
	const std::span<mesh_desc> meshes = {
		( mesh_desc* ) ( std::data( binaryData ) + fileFooter.meshesByteRange.offset ),
		fileFooter.meshesByteRange.size / sizeof( mesh_desc ) };

	const std::span<material_data> mtrlDesc = {
		( material_data* ) ( std::data( binaryData ) + fileFooter.mtrlsByteRange.offset ),
		fileFooter.mtrlsByteRange.size / sizeof( material_data ) };

	const std::span<image_metadata> imgDesc = {
		( image_metadata* ) ( std::data( binaryData ) + fileFooter.imgsByteRange.offset ),
		fileFooter.imgsByteRange.size / sizeof( image_metadata ) };


	std::vector<material_data> mtrls = {};
	for( const material_data& m : mtrlDesc )
	{
		mtrls.push_back( m );
		material_data& refM = mtrls[ std::size( mtrls ) - 1 ];
		//refM.baseColIdx += std::size( textures.rsc ) + srvManager.slotSizeTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];
		//refM.normalMapIdx += std::size( textures.rsc ) + srvManager.slotSizeTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];
		//refM.occRoughMetalIdx += std::size( textures.rsc ) + srvManager.slotSizeTable[ VK_GLOBAL_SLOT_SAMPLED_IMAGE ];

		//refM.baseColIdx += std::size( textures );
		//refM.normalMapIdx += std::size( textures );
		//refM.occRoughMetalIdx += std::size( textures );



		refM.baseColIdx += 1;
		refM.normalMapIdx += 1;
		refM.occRoughMetalIdx += 1;
	}

	std::srand( randSeed );

	assert( std::size( mtrls ) == 1 );
	std::vector<instance_desc> instDesc = SpawnRandomInstances( { std::data( meshes ),std::size( meshes ) }, drawCount, 1, sceneRad );
	std::vector<light_data> lights = SpawnRandomLights( lightCount, sceneRad * 0.75f );

	assert( std::size( instDesc ) < u16( -1 ) );


	for( const instance_desc& ii : instDesc )
	{
		const mesh_desc& m = meshes[ ii.meshIdx ];
		entities.transforms.push_back( ii.localToWorld );
		entities.instAabbs.push_back( { m.aabbMin, m.aabbMax } );
	}


	std::vector<DirectX::XMFLOAT3> proxyVtx;
	std::vector<u32> proxyIdx;
	{
		GenerateIcosphere( proxyVtx, proxyIdx, 1 );
		u64 uniqueVtxCount = MeshoptReindexMesh( std::span<DirectX::XMFLOAT3>{ proxyVtx }, proxyIdx );
		proxyVtx.resize( uniqueVtxCount );
		MeshoptOptimizeMesh( std::span<DirectX::XMFLOAT3>{ proxyVtx }, proxyIdx );

		assert( std::size( lights ) < u16( -1 ) );
		assert( std::size( proxyVtx ) < u16( -1 ) );
		// NOTE: becaue there's only one type of light
		u64 initialSize = std::size( proxyIdx );
		proxyIdx.resize( initialSize * std::size( lights ) );
		for( u64 li = 0; li < std::size( lights ); ++li )
		{
			u64 idxBuffOffset = initialSize * li;
			for( u64 ii = 0; ii < initialSize; ++ii )
			{
				proxyIdx[ idxBuffOffset + ii ] = u32( proxyIdx[ ii ] & u16( -1 ) ) | ( u32( li ) << 16 );
			}
		}
	}


	// TODO: make easier to use 
	VkMemoryBarrier2 buffBarriers[] = { VkMakeMemoryBarrier2(
		VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_NONE )
	};
	{
		const std::span<u8> vtxView = { std::data( binaryData ) + fileFooter.vtxByteRange.offset, fileFooter.vtxByteRange.size };

		globVertexBuff = VkCreateAllocBindBuffer( 
			std::size( vtxView ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vkRscArena, dc.gpu );
		VkDbgNameObj( globVertexBuff.hndl, dc.device, "Buff_Vtx" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( std::size( vtxView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, std::data( vtxView ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, globVertexBuff.hndl, 1, &copyRegion );
	}
	{
		const std::span<u8> idxSpan = { std::data( binaryData ) + fileFooter.idxByteRange.offset, fileFooter.idxByteRange.size };

		indexBuff = VkCreateAllocBindBuffer(
			std::size( idxSpan ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena, dc.gpu );
		VkDbgNameObj( indexBuff.hndl, dc.device, "Buff_Idx" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( std::size( idxSpan ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, std::data( idxSpan ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, indexBuff.hndl, 1, &copyRegion );
	}
	{
		meshBuff = VkCreateAllocBindBuffer( 
			BYTE_COUNT( meshes ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vkRscArena, dc.gpu );
		VkDbgNameObj( meshBuff.hndl, dc.device, "Buff_Mesh_Desc" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( 
			BYTE_COUNT( meshes ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( meshes ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshBuff.hndl, 1, &copyRegion );
	}
	{
		lightsBuff = VkCreateAllocBindBuffer( 
			BYTE_COUNT( lights ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vkRscArena, dc.gpu );
		VkDbgNameObj( lightsBuff.hndl, dc.device, "Buff_Lights" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( 
			BYTE_COUNT( lights ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( lights ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, lightsBuff.hndl, 1, &copyRegion );
	}
	{
		instDescBuff = VkCreateAllocBindBuffer( 
			BYTE_COUNT( instDesc ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vkRscArena, dc.gpu );
		VkDbgNameObj( instDescBuff.hndl, dc.device, "Buff_Inst_Descs" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( 
			BYTE_COUNT( instDesc ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( instDesc ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, instDescBuff.hndl, 1, &copyRegion );
	}
	{
		materialsBuff = VkCreateAllocBindBuffer( 
			BYTE_COUNT( mtrls ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vkRscArena, dc.gpu );
		VkDbgNameObj( materialsBuff.hndl, dc.device, "Buff_Mtrls" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( 
			BYTE_COUNT( mtrls ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, ( const u8* ) std::data( mtrls ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, materialsBuff.hndl, 1, &copyRegion );
	}
	{
		const std::span<u8> mletView = { std::data( binaryData ) + fileFooter.mletsByteRange.offset,fileFooter.mletsByteRange.size };

		assert( fileFooter.mletsByteRange.size < u16( -1 ) * sizeof( meshlet ) );

		meshletBuff = VkCreateAllocBindBuffer( std::size( mletView ),
											   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
											   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
											   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
											   vkRscArena, dc.gpu );
		VkDbgNameObj( meshletBuff.hndl, dc.device, "Buff_Meshlets" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( std::size( mletView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, std::data( mletView ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshletBuff.hndl, 1, &copyRegion );
	}
	{
		const std::span<u8> mletDataView = {
			std::data( binaryData ) + fileFooter.mletsDataByteRange.offset,
			fileFooter.mletsDataByteRange.size };

		meshletDataBuff = VkCreateAllocBindBuffer( std::size( mletDataView ),
												   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
												   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
												   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
												   vkRscArena, dc.gpu );
		VkDbgNameObj( meshletDataBuff.hndl, dc.device, "Buff_Meshlet_Data" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer(
			std::size( mletDataView ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, std::data( mletDataView ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, meshletDataBuff.hndl, 1, &copyRegion );
	}


	{
		proxyGeomBuff = VkCreateAllocBindBuffer(
			BYTE_COUNT( proxyVtx ),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			vkRscArena, dc.gpu );
		VkDbgNameObj( proxyGeomBuff.hndl, dc.device, "Buff_Proxy_Vtx" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( 
			BYTE_COUNT( proxyVtx ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, std::data( proxyVtx ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, proxyGeomBuff.hndl, 1, &copyRegion );
	}
	{
		proxyIdxBuff = VkCreateAllocBindBuffer(
			BYTE_COUNT( proxyIdx ), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vkRscArena, dc.gpu );
		VkDbgNameObj( proxyIdxBuff.hndl, dc.device, "Buff_Proxy_Idx" );

		vk_buffer stagingBuf = VkCreateAllocBindBuffer( 
			BYTE_COUNT( proxyIdx ), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuf.hostVisible, std::data( proxyIdx ), stagingBuf.size );

		VkBufferCopy copyRegion = { 0,0,stagingBuf.size };
		vkCmdCopyBuffer( cmdBuff, stagingBuf.hndl, proxyIdxBuff.hndl, 1, &copyRegion );
	}

	drawCmdBuff = VkCreateAllocBindBuffer(
		std::size( instDesc ) * sizeof( draw_command ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena, dc.gpu );
	VkDbgNameObj( drawCmdBuff.hndl, dc.device, "Buff_Indirect_Draw_Cmds" );

	drawCmdDbgBuff = VkCreateAllocBindBuffer(
		( fileFooter.mletsByteRange.size / sizeof( meshlet ) ) * sizeof( draw_command ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena, dc.gpu );
	VkDbgNameObj( drawCmdDbgBuff.hndl, dc.device, "Buff_Indirect_Dbg_Draw_Cmds" );

	// TODO: expose from asset compiler 
	constexpr u64 MAX_TRIS = 256;
	//u64 maxByteCountMergedIndexBuff = std::size( instDesc ) * ( meshletBuff.size / sizeof( meshlet ) ) * MAX_TRIS * 3ull;
	u64 maxByteCountMergedIndexBuff = 10 * MB;

	intermediateIndexBuff = VkCreateAllocBindBuffer(
		maxByteCountMergedIndexBuff,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena, dc.gpu );
	VkDbgNameObj( intermediateIndexBuff.hndl, dc.device, "Buff_Intermediate_Idx" );

	indirectMergedIndexBuff = VkCreateAllocBindBuffer(
		maxByteCountMergedIndexBuff,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena, dc.gpu );
	VkDbgNameObj( indirectMergedIndexBuff.hndl, dc.device, "Buff_Merged_Idx" );

	drawCmdAabbsBuff = VkCreateAllocBindBuffer( 
		10'000 * sizeof( draw_indirect ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena, dc.gpu );


	drawMergedCmd = VkCreateAllocBindBuffer(
		sizeof( draw_command ),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		vkRscArena, dc.gpu );


	std::vector<VkImageMemoryBarrier2KHR> imageNewBarriers;
	std::vector<std::function<void()>> copyCmds;
	std::vector<VkImageMemoryBarrier2KHR> imageInitBarriers;
	{
		imageNewBarriers.reserve( std::size( imgDesc ) );
		copyCmds.reserve( std::size( imgDesc ) );
		imageInitBarriers.reserve( std::size( imgDesc ) );

		const u8* pTexBinData = std::data( binaryData ) + fileFooter.texBinByteRange.offset;

		vk_buffer stagingBuff = VkCreateAllocBindBuffer(
			fileFooter.texBinByteRange.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
		std::memcpy( stagingBuff.hostVisible, pTexBinData, stagingBuff.size );

		for( const image_metadata& meta : imgDesc )
		{
			VkImageCreateInfo info = VkGetImageInfoFromMetadata( 
				meta, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
			vk_image img = VkCreateAllocBindImage( info, vkAlbumArena, dc.gpu );

			imageNewBarriers.push_back( VkMakeImageBarrier2(
				img.hndl,
				0, 0,
				VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_ASPECT_COLOR_BIT ) );

			hndl64<vk_image> hImg = textures.emplace( img );
			
			//u16 imgDescriptor = VkAllocDescriptorIdx( 
			//	dc.device, VkDescriptorImageInfo{ 0, img.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR }, vk.descMngr );

			copyCmds.push_back([ & ] () {
					
				}
			);
			

			imageInitBarriers.push_back( VkMakeImageBarrier2(
				img.hndl,
				VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
				VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
				VK_ACCESS_2_SHADER_READ_BIT_KHR,
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
				VK_IMAGE_ASPECT_COLOR_BIT ) );
		}

		VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, imageNewBarriers );
		for( const auto& cpCmd : copyCmds )
		{
			cpCmd();
		}
		//VkFlushDescriptorUpdates( dc.device, vk.descMngr );
	}

	VkCmdPipelineFlushCacheBarriers( cmdBuff, buffBarriers );
	VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, imageInitBarriers );
}

// TODO: 
struct render_context
{
	VkPipeline   gfxZPrepass;
	VkPipeline		gfxPipeline;
	VkPipeline		gfxMeshletPipeline;
	VkPipeline		gfxMergedPipeline;
	VkPipeline      gfxDrawIndirDbg;
	VkPipeline		compPipeline;
	VkPipeline		compHiZPipeline;

	VkPipeline		compAvgLumPipe;
	VkPipeline		compTonemapPipe;
	VkPipeline      compExpanderPipe;
	VkPipeline      compClusterCullPipe;
	VkPipeline      compExpMergePipe;

	vk_program	gfxMeshletProgram;
	vk_program	avgLumCompProgram;
	vk_program	tonemapCompProgram;
	vk_program   dbgDrawProgram;
	
	VkSampler		quadMinSampler;
	VkSampler		pbrSampler;

	static constexpr VkFormat		desiredDepthFormat = VK_FORMAT_D32_SFLOAT;
	static constexpr VkFormat		desiredColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

	render_context() = default;
	explicit render_context( const vk_device& vkDc, const vk_descriptor_manager& descMngr );
};

render_context::render_context( const vk_device& vkDc, const vk_descriptor_manager& descMngr )
{
	quadMinSampler = VkMakeSampler( vkDc.device, VK_SAMPLER_REDUCTION_MODE_MIN, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE );
	pbrSampler = VkMakeSampler( vkDc.device, HTVK_NO_SAMPLER_REDUCTION, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT );

	{
		vk_shader vertZPre = VkLoadShader( "Shaders/v_z_prepass.vert.spv", vkDc.device );
		gfxZPrepass = VkMakeGfxPipeline(
			vkDc.device, descMngr.globalPipelineLayout, vertZPre.module, 0, 0, desiredDepthFormat, {}, "Pipeline_Gfx_ZPrepass" );
		vkDestroyShaderModule( vkDc.device, vertZPre.module, 0 );
	}
	{
		vk_shader vertBox = VkLoadShader( "Shaders/box_meshlet_draw.vert.spv", vkDc.device );
		vk_shader normalCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", vkDc.device );

		vk_gfx_pipeline_state lineDrawPipelineState = {
			.polyMode = VK_POLYGON_MODE_LINE,
			.cullFlags = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
			.primTopology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
			.depthWrite = VK_FALSE,
			.depthTestEnable = VK_FALSE,
			.blendCol = VK_FALSE,
		};
		dbgDrawProgram = VkMakePipelineProgram(
			vkDc.device, vkDc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertBox, &normalCol }, descMngr.setLayout );
		gfxDrawIndirDbg = VkMakeGfxPipeline( vkDc.device, dbgDrawProgram.pipeLayout, vertBox.module, normalCol.module,
											 &desiredColorFormat, desiredDepthFormat, lineDrawPipelineState, "Pipeline_Gfx_DbgDraw");

		vkDestroyShaderModule( vkDc.device, vertBox.module, 0 );
		vkDestroyShaderModule( vkDc.device, normalCol.module, 0 );
	}
	{
		vk_shader drawCull = VkLoadShader( "Shaders/c_draw_cull.comp.spv", vkDc.device );
		compPipeline = VkMakeComputePipeline(
			vkDc.device, 0, descMngr.globalPipelineLayout, drawCull.module, { 32u }, "Pipeline_Comp_DrawCull" );

		vk_shader clusterCull = VkLoadShader( "Shaders/c_meshlet_cull.comp.spv", vkDc.device );
		compClusterCullPipe = VkMakeComputePipeline(
			vkDc.device, 0, descMngr.globalPipelineLayout, clusterCull.module, {}, "Pipeline_Comp_ClusterCull" );

		vk_shader expansionComp = VkLoadShader( "Shaders/c_id_expander.comp.spv", vkDc.device );
		compExpanderPipe = VkMakeComputePipeline( 
			vkDc.device, 0, descMngr.globalPipelineLayout, expansionComp.module, {}, "Pipeline_Comp_iD_Expander" );

		vk_shader expMerge = VkLoadShader( "Shaders/comp_expand_merge.comp.spv", vkDc.device );
		compExpMergePipe = VkMakeComputePipeline( vkDc.device, 0, descMngr.globalPipelineLayout, expMerge.module, {}, "Pipeline_Comp_ExpMerge" );

		vkDestroyShaderModule( vkDc.device, drawCull.module, 0 );
		vkDestroyShaderModule( vkDc.device, expansionComp.module, 0 );
		vkDestroyShaderModule( vkDc.device, clusterCull.module, 0 );
		vkDestroyShaderModule( vkDc.device, expMerge.module, 0 );
	}
	{
		vk_shader vtxMerged = VkLoadShader( "Shaders/vtx_merged.vert.spv", vkDc.device );
		vk_shader fragPBR = VkLoadShader( "Shaders/pbr.frag.spv", vkDc.device );
		vk_gfx_pipeline_state opaqueState = {};

		gfxMergedPipeline = VkMakeGfxPipeline( vkDc.device, descMngr.globalPipelineLayout, vtxMerged.module, fragPBR.module,
													  &desiredColorFormat, desiredDepthFormat, opaqueState, "Pipeline_Gfx_Merged" );

		vkDestroyShaderModule( vkDc.device, vtxMerged.module, 0 );
		vkDestroyShaderModule( vkDc.device, fragPBR.module, 0 );
	}
	{
		vk_shader vertMeshlet = VkLoadShader( "Shaders/meshlet.vert.spv", vkDc.device );
		vk_shader fragCol = VkLoadShader( "Shaders/f_pass_col.frag.spv", vkDc.device );
		vk_gfx_pipeline_state meshletState = {};
		gfxMeshletProgram = VkMakePipelineProgram(
			vkDc.device, vkDc.gpuProps, VK_PIPELINE_BIND_POINT_GRAPHICS, { &vertMeshlet, &fragCol }, descMngr.setLayout );
		gfxMeshletPipeline = VkMakeGfxPipeline( vkDc.device, gfxMeshletProgram.pipeLayout, vertMeshlet.module,
													   fragCol.module, &desiredColorFormat, desiredDepthFormat,
													   meshletState, "Pipeline_Gfx_MeshletDraw" );

		vkDestroyShaderModule( vkDc.device, vertMeshlet.module, 0 );
		vkDestroyShaderModule( vkDc.device, fragCol.module, 0 );
	}
	{
		vk_shader avgLum = VkLoadShader( "Shaders/avg_luminance.comp.spv", vkDc.device );
		avgLumCompProgram = VkMakePipelineProgram(
			vkDc.device, vkDc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &avgLum }, descMngr.setLayout );
		compAvgLumPipe = VkMakeComputePipeline(
			vkDc.device, 0, avgLumCompProgram.pipeLayout, avgLum.module, { vkDc.waveSize }, "Pipeline_Comp_AvgLum" );

		vk_shader toneMapper = VkLoadShader( "Shaders/tonemap_gamma.comp.spv", vkDc.device );
		tonemapCompProgram = VkMakePipelineProgram(
			vkDc.device, vkDc.gpuProps, VK_PIPELINE_BIND_POINT_COMPUTE, { &toneMapper }, descMngr.setLayout );
		compTonemapPipe = VkMakeComputePipeline( vkDc.device, 0, tonemapCompProgram.pipeLayout, toneMapper.module, {}, "Pipeline_Comp_Tonemap" );

		vkDestroyShaderModule( vkDc.device, avgLum.module, 0 );
		vkDestroyShaderModule( vkDc.device, toneMapper.module, 0 );
	}
	{
		static_assert( multiPassDepthPyramid );
		vk_shader downsampler = VkLoadShader( "Shaders/pow2_downsampler.comp.spv", vkDc.device );
		compHiZPipeline = VkMakeComputePipeline( 
			vkDc.device, 0, descMngr.globalPipelineLayout, downsampler.module, {}, "Pipeline_Comp_DepthPyramid" );

		vkDestroyShaderModule( vkDc.device, downsampler.module, 0 );
	}

	vkDbgCtx = VkMakeDebugContext( vkDc.device, vkDc.gpuProps );

	imguiVkCtx = ImguiMakeVkContext( vkDc.device, vkDc.gpuProps, VK_FORMAT_B8G8R8A8_UNORM );

}

struct virtual_frame
{
	vk_buffer		frameData;
	vk_gpu_timer frameTimer;
	VkCommandBuffer cmdBuff; // TODO: use vk_command_buffer
	VkSemaphore		canGetImgSema; // TODO: place somewhere else ?
	VkSemaphore		canPresentSema;
	u16 frameDescIdx;
};

inline static virtual_frame VkCreateVirtualFrame( const vk_device& vkDevice, u32 bufferSize )
{
	virtual_frame vrtFrame = {};

	VkCommandBufferAllocateInfo cmdBuffAllocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = vrtFrame.cmdPool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VK_CHECK( vkAllocateCommandBuffers( vkDevice.device, &cmdBuffAllocInfo, &vrtFrame.cmdBuff ) );

	VkSemaphoreCreateInfo semaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK( vkCreateSemaphore( vkDevice.device, &semaInfo, 0, &vrtFrame.canGetImgSema ) );
	VK_CHECK( vkCreateSemaphore( vkDevice.device, &semaInfo, 0, &vrtFrame.canPresentSema ) );

	VkBufferUsageFlags usg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	vrtFrame.frameData = VkCreateAllocBindBuffer(
		vkDevice, { .name = "Buff_Frame_Data", .usage = usg, .elemCount = bufferSize, .stride = 1 }, vk_mem_usage::HOST_VISIBLE );

	constexpr u32 queryCount = 2;
	VkQueryPool queryPool = VkMakeQueryPool( vkDevice.device, queryCount, "VkQueryPool_GPU_timer" );
	VkBufferUsageFlags usgQuery = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	vk_buffer resultBuff = VkCreateAllocBindBuffer( 
		vkDevice, 
		{ .name = "Buff_Timestamp_Queries", .usage = usgQuery, .elemCount = queryCount, .stride = sizeof( u64 ) }, 
		vk_mem_usage::HOST_VISIBLE );
	vrtFrame.frameTimer = { resultBuff, queryPool, queryCount, vkDevice.timestampPeriod };

	return vrtFrame;
}


struct vk_backend
{
	u64 dllHandle;
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMsg;
	VkSurfaceKHR surface;

	vk_device pDevice;
	vk_swapchain swapchain;

	virtual_frame	vrtFrames[ VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	VkSemaphore     timelineSema;
	u64				vFrameIdx;
	u8				framesInFlight = VK_MAX_FRAMES_IN_FLIGHT_ALLOWED;

	const virtual_frame& GetNextFrame( u64 currentFrameIdx ) const
	{
		u64 vrtFrameIdx = currentFrameIdx % std::size( vrtFrames );
		return vrtFrames[ vrtFrameIdx ];
	}


	handle_map<vk_buffer> bufferMap;
	handle_map<vk_image> imageMap;
	vk_descriptor_manager descManager;

	hndl64<vk_buffer> hDrawCount;
	hndl64<vk_buffer> hDrawCountDebug;
	hndl64<vk_buffer> hAvgLum;
	hndl64<vk_buffer> hGlobals;
	hndl64<vk_buffer> hGlobalSyncCounter;
	hndl64<vk_buffer> hDepthAtomicCounter;
	hndl64<vk_buffer> hAtomicCounter;
	hndl64<vk_buffer> hDispatchCmd0;
	hndl64<vk_buffer> hDispatchCmd1;
	hndl64<vk_buffer> hMeshletCount;
	hndl64<vk_buffer> hMergedIdxCount;
	hndl64<vk_buffer> hMergedDrawCout;

	render_context rndCtx;

	explicit vk_backend();
	void Terminate();

	void HostFrames( const frame_data& frameData, gpu_data& gpuData );
};

vk_backend::vk_backend()
{
	std::tie( this->dllHandle, this->inst, this->dbgMsg ) = VkMakeInstance();
	surface = VkMakeSurface( inst, hInst, hWnd );

	this->pDevice = VkMakeDeviceContext( inst, surface );
	swapchain = VkMakeSwapchain( deviceCtx.device, deviceCtx.gpu, surface, deviceCtx.gfxQueueIdx, VK_FORMAT_B8G8R8A8_UNORM );

	descManager = VkMakeDescriptorManager( deviceCtx.device, deviceCtx.gpuProps );

	for( u64 vfi = 0; vfi < framesInFlight; ++vfi )
	{
		vrtFrames[ vfi ] = VkCreateVirtualFrame( deviceCtx, u32( -1 ) );
	}
	VkSemaphoreTypeCreateInfo timelineInfo = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = vFrameIdx = 0
	};
	VkSemaphoreCreateInfo timelineSemaInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &timelineInfo };
	VK_CHECK( vkCreateSemaphore( deviceCtx.device, &timelineSemaInfo, 0, &timelineSema ) );

	{
		avgLumBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_AvgLum", .usage = STORAGE_INDIRECT, .elemCount = 1, .stride = sizeof( u32 ) } );
		shaderGlobalsBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Shader_Global", .usage = STORAGE_INDIRECT_DST, .elemCount = 64, .stride = 1 } );
		shaderGlobalSyncCounterBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Shader_Global_Sync_Counter", .usage = STORAGE_INDIRECT_DST, .elemCount = 1, .stride = sizeof( u32 ) } );
		drawCountBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Draw_Count", .usage = STORAGE_INDIRECT_DST_BDA, .elemCount = 1, .stride = sizeof( u32 ) } );
		drawCountDbgBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Draw_Count_Dbg", .usage = STORAGE_INDIRECT_DST_BDA, .elemCount = 1, .stride = sizeof( u32 ) } );
		// TODO: no transfer bit ?
		constexpr VkBufferUsageFlags depthAtomicBuffUsg = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		depthAtomicCounterBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Depth_Atomic_Counter", .usage = depthAtomicBuffUsg, .elemCount = 1, .stride = sizeof( u32 ) } );

		dispatchCmdBuff0 = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Dispatch_Cmd0", .usage = STORAGE_INDIRECT_DST_BDA, .elemCount = 1, .stride = sizeof( dispatch_command ) } );
		dispatchCmdBuff1 = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Dispatch_Cmd1", .usage = STORAGE_INDIRECT_DST_BDA, .elemCount = 1, .stride = sizeof( dispatch_command ) } );

		meshletCountBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Mlet_Dispatch_Count", .usage = STORAGE_DST_BDA, .elemCount = 1, .stride = sizeof( u32 ) } );
		atomicCounterBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Atomic_Counter", .usage = STORAGE_DST_BDA, .elemCount = 1, .stride = sizeof( u32 ) } );
		mergedIndexCountBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Merged_Idx_Count", .usage = STORAGE_DST_BDA, .elemCount = 1, .stride = sizeof( u32 ) } );
		drawMergedCountBuff = VkCreateAllocBindBuffer(
			deviceCtx, { .name = "Buff_Draw_Merged_Count", .usage = STORAGE_INDIRECT_DST_BDA, .elemCount = 1, .stride = sizeof( u32 ) } );
	}

	this->rndCtx = render_context{ deviceCtx, descManager };
}
void vk_backend::Terminate()
{
	// NOTE: SHOULDN'T need to check if( VkObj ). Can't create -> app fail
	vkDeviceWaitIdle( this->deviceCtx.device );
	//for( auto& queued : deviceGlobalDeletionQueue ) queued();
	//deviceGlobalDeletionQueue.clear();


	vkDestroyDevice( this->deviceCtx.device, 0 );
#ifdef _VK_DEBUG_
	//vkDestroyDebugUtilsMessengerEXT( vkInst, vkDbgMsg, 0 );
#endif
	//vkDestroySurfaceKHR( vkInst, vkSurf, 0 );
	//vkDestroyInstance( vkInst, 0 );

	//SysDllUnload( VK_DLL );
}

inline u64 GroupCount( u64 invocationCount, u64 workGroupSize )
{
	return ( invocationCount + workGroupSize - 1 ) / workGroupSize;
}



inline static void
FrameClearDataPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline
) {

}

// TODO: reduce the ammount of counter buffers
// TODO: optimize expansion shader
inline static void
CullPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	const vk_image& depthPyramid,
	const VkSampler& minQuadSampler,
	u16 _camIdx,
	u16 _hizBuffIdx,
	u16 samplerIdx,
	group_size groupSize
) {
	// NOTE: wtf Vulkan ?
	constexpr u64 VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;


	vk_label label = { cmdBuff,"Cull Pass",{} };

	vkCmdFillBuffer( cmdBuff, drawCountBuff.hndl, 0, drawCountBuff.size, 0u );

	vkCmdFillBuffer( cmdBuff, drawCountDbgBuff.hndl, 0, drawCountDbgBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, meshletCountBuff.hndl, 0, meshletCountBuff.size, 0u );

	vkCmdFillBuffer( cmdBuff, mergedIndexCountBuff.hndl, 0, mergedIndexCountBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, drawMergedCountBuff.hndl, 0, drawMergedCountBuff.size, 0u );

	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff0.hndl, 0, dispatchCmdBuff0.size, 0u );
	vkCmdFillBuffer( cmdBuff, dispatchCmdBuff1.hndl, 0, dispatchCmdBuff1.size, 0u );

	VkMemoryBarrier2 beginCullBarriers[] = {
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

		VkMakeMemoryBarrier2(
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR | VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR ),

		VkMakeMemoryBarrier2(
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR )
	};

	VkImageMemoryBarrier2KHR hiZReadBarrier[] = { VkMakeImageBarrier2(
		depthPyramid.hndl,
		VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_ACCESS_2_SHADER_READ_BIT_KHR,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
		VK_IMAGE_ASPECT_COLOR_BIT ) };

	VkCmdPipelineFlushCacheBarriers( cmdBuff, beginCullBarriers );
	VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, hiZReadBarrier );

	u32 instCount = instDescBuff.size / sizeof( instance_desc );

	{
		struct
		{
			u64 instDescAddr = instDescBuff.devicePointer;
			u64 meshDescAddr = meshBuff.devicePointer;
			u64 visInstsAddr = intermediateIndexBuff.devicePointer;
			u64 atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
			u64 drawCounterAddr = drawCountBuff.devicePointer;
			u64 dispatchCmdAddr = dispatchCmdBuff0.devicePointer;
			u32	hizBuffIdx = _hizBuffIdx;
			u32	hizSamplerIdx = samplerIdx;
			u32 instanceCount = instCount;
			u32 camIdx = _camIdx;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );
		vkCmdPushConstants( cmdBuff, vk.descMngr.globalPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatch( cmdBuff, GroupCount( instCount, groupSize.x ), 1, 1 );
	}

	VkMemoryBarrier2 dispatchSyncBarriers[] = {
			VkMakeMemoryBarrier2(
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
								  VK_PIPELINE_STAGE_2_DISPATCH_INDIRECT_BIT_HELLTECH ),
			VkMakeMemoryBarrier2(
								  VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
								  VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
								  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT )
	};

	{
		VkCmdPipelineFlushCacheBarriers( cmdBuff, dispatchSyncBarriers );


		struct
		{
			u64 visInstAddr = intermediateIndexBuff.devicePointer;
			u64 visInstCountAddr = drawCountBuff.devicePointer;
			u64 expandeeAddr = indirectMergedIndexBuff.devicePointer;
			u64 expandeeCountAddr = meshletCountBuff.devicePointer;
			u64 atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
			u64 dispatchCmdAddr = dispatchCmdBuff1.devicePointer;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpanderPipe );
		vkCmdPushConstants( cmdBuff, vk.descMngr.globalPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff0.hndl, 0 );
	}

	{
		VkCmdPipelineFlushCacheBarriers( cmdBuff, dispatchSyncBarriers );

		struct
		{
			u64 instDescAddr = instDescBuff.devicePointer;
			u64 meshletDescAddr = meshletBuff.devicePointer;
			u64	inMeshletsIdAddr = indirectMergedIndexBuff.devicePointer;
			u64	inMeshletsCountAddr = meshletCountBuff.devicePointer;
			u64	outMeshletsIdAddr = intermediateIndexBuff.devicePointer;
			u64	outMeshletsCountAddr = drawCountDbgBuff.devicePointer;
			u64	atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
			u64	dispatchCmdAddr = dispatchCmdBuff0.devicePointer;
			u64	dbgDrawCmdsAddr = drawCmdAabbsBuff.devicePointer;
			u32 hizBuffIdx = _hizBuffIdx;
			u32	hizSamplerIdx = samplerIdx;
			u32 camIdx = _camIdx;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compClusterCullPipe );
		vkCmdPushConstants( cmdBuff, vk.descMngr.globalPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff1.hndl, 0 );
	}

	{
		VkCmdPipelineFlushCacheBarriers( cmdBuff, dispatchSyncBarriers );

		struct
		{
			u64 meshletDataAddr = meshletDataBuff.devicePointer;
			u64 visMeshletsAddr = intermediateIndexBuff.devicePointer;
			u64 visMeshletsCountAddr = drawCountDbgBuff.devicePointer;
			u64 mergedIdxBuffAddr = indirectMergedIndexBuff.devicePointer;
			u64 mergedIdxCountAddr = mergedIndexCountBuff.devicePointer;
			u64 drawCmdsAddr = drawMergedCmd.devicePointer;
			u64 drawCmdCountAddr = drawMergedCountBuff.devicePointer;
			u64 atomicWorkgrCounterAddr = atomicCounterBuff.devicePointer;
		} pushConst = {};

		vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, rndCtx.compExpMergePipe );
		vkCmdPushConstants( cmdBuff, vk.descMngr.globalPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof( pushConst ), &pushConst );
		vkCmdDispatchIndirect( cmdBuff, dispatchCmdBuff0.hndl, 0 );
	}


	VkMemoryBarrier2 endCullBarriers[] = {
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR | VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT_KHR ),
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_SHADER_READ_BIT_KHR,//VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR ),
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_INDEX_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT_KHR ),
	};

	VkCmdPipelineFlushCacheBarriers( cmdBuff, endCullBarriers );
}


// TODO: overdraw more efficiently 
// TODO: no imgui dependency
static inline void ImguiDrawUiPass(
	const imgui_vk_context& ctx,
	VkCommandBuffer cmdBuff,
	const VkRenderingAttachmentInfoKHR* pColInfo,
	const VkRenderingAttachmentInfoKHR* pDepthInfo,
	u64 frameIdx
) {
	static_assert( sizeof( ImDrawVert ) == sizeof( imgui_vertex ) );
	static_assert( sizeof( ImDrawIdx ) == 2 );

	using namespace DirectX;

	const ImDrawData* guiDrawData = ImGui::GetDrawData();

	const vk_buffer& vtxBuff = imguiVkCtx.vtxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];
	const vk_buffer& idxBuff = imguiVkCtx.idxBuffs[ frameIdx % VK_MAX_FRAMES_IN_FLIGHT_ALLOWED ];

	assert( guiDrawData->TotalVtxCount < u16( -1 ) );
	assert( guiDrawData->TotalVtxCount * sizeof( ImDrawVert ) < vtxBuff.size );

	ImDrawVert* vtxDst = ( ImDrawVert* ) vtxBuff.hostVisible;
	ImDrawIdx* idxDst = ( ImDrawIdx* ) idxBuff.hostVisible;
	for( u64 ci = 0; ci < guiDrawData->CmdListsCount; ++ci )
	{
		const ImDrawList* cmdList = guiDrawData->CmdLists[ ci ];
		std::memcpy( vtxDst, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof( ImDrawVert ) );
		std::memcpy( idxDst, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof( ImDrawIdx ) );
		vtxDst += cmdList->VtxBuffer.Size;
		idxDst += cmdList->IdxBuffer.Size;
	}


	vk_label label = { cmdBuff,"Draw Imgui Pass",{} };

	VkRect2D scissor = { 0,0,sc.width,sc.height };
	VkRenderingInfo renderInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
	renderInfo.renderArea = scissor;
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = pColInfo ? 1 : 0;
	renderInfo.pColorAttachments = pColInfo;
	renderInfo.pDepthAttachment = pDepthInfo;
	vkCmdBeginRendering( cmdBuff, &renderInfo );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline );

	vk_descriptor_info pushDescs[] = {
		vtxBuff.Descriptor(), {ctx.fontSampler, ctx.fontsImg.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR} };

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, ctx.descTemplate, ctx.pipelineLayout, 0, pushDescs );

	float scale[ 2 ] = { 2.0f / guiDrawData->DisplaySize.x, 2.0f / guiDrawData->DisplaySize.y };
	float move[ 2 ] = { -1.0f - guiDrawData->DisplayPos.x * scale[ 0 ], -1.0f - guiDrawData->DisplayPos.y * scale[ 1 ] };
	XMFLOAT4 pushConst = { scale[ 0 ],scale[ 1 ],move[ 0 ],move[ 1 ] };
	vkCmdPushConstants( cmdBuff, ctx.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( pushConst ), &pushConst );
	vkCmdBindIndexBuffer( cmdBuff, idxBuff.hndl, 0, VK_INDEX_TYPE_UINT16 );


	// (0,0) unless using multi-viewports
	XMFLOAT2 clipOff = { guiDrawData->DisplayPos.x, guiDrawData->DisplayPos.y };
	// (1,1) unless using retina display which are often (2,2)
	XMFLOAT2 clipScale = { guiDrawData->FramebufferScale.x, guiDrawData->FramebufferScale.y };

	u32 vtxOffset = 0;
	u32 idxOffset = 0;
	for( u64 li = 0; li < guiDrawData->CmdListsCount; ++li )
	{
		const ImDrawList* cmdList = guiDrawData->CmdLists[ li ];
		for( u64 ci = 0; ci < cmdList->CmdBuffer.Size; ++ci )
		{
			const ImDrawCmd* pCmd = &cmdList->CmdBuffer[ ci ];
			// Project scissor/clipping rectangles into framebuffer space
			XMFLOAT2 clipMin = { ( pCmd->ClipRect.x - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.y - clipOff.y ) * clipScale.y };
			XMFLOAT2 clipMax = { ( pCmd->ClipRect.z - clipOff.x ) * clipScale.x, ( pCmd->ClipRect.w - clipOff.y ) * clipScale.y };

			// Clamp to viewport as vkCmdSetScissor() won't accept values that are off bounds
			clipMin = { std::max( clipMin.x, 0.0f ), std::max( clipMin.y, 0.0f ) };
			clipMax = { std::min( clipMax.x, ( float ) sc.width ), std::min( clipMax.y, ( float ) sc.height ) };

			if( clipMax.x < clipMin.x || clipMax.y < clipMin.y ) continue;

			VkRect2D scissor = { i32( clipMin.x ), i32( clipMin.y ), u32( clipMax.x - clipMin.x ), u32( clipMax.y - clipMin.y ) };
			vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

			vkCmdDrawIndexed( cmdBuff, pCmd->ElemCount, 1, pCmd->IdxOffset + idxOffset, pCmd->VtxOffset + vtxOffset, 0 );
		}
		idxOffset += cmdList->IdxBuffer.Size;
		vtxOffset += cmdList->VtxBuffer.Size;
	}

	vkCmdEndRendering( cmdBuff );
}

// TODO: color depth toggle stuff
inline static void
DebugDrawPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			vkPipeline,
	const VkRenderingAttachmentInfoKHR* pColInfo,
	const VkRenderingAttachmentInfoKHR* pDepthInfo,
	const vk_buffer& drawBuff,
	const vk_program& program,
	const mat4& projView,
	range		        drawRange
) {
	vk_label label = { cmdBuff,"Dbg Draw Pass",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderingInfo renderInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
	renderInfo.renderArea = scissor;
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = pColInfo ? 1 : 0;
	renderInfo.pColorAttachments = pColInfo;
	renderInfo.pDepthAttachment = pDepthInfo;
	vkCmdBeginRendering( cmdBuff, &renderInfo );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	vk_descriptor_info pushDescs[] = { Descriptor( drawBuff ) };
	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, pushDescs );
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( mat4 ), &projView );

	vkCmdDraw( cmdBuff, drawRange.size, 1, drawRange.offset, 0 );

	vkCmdEndRendering( cmdBuff );
}

inline static void
DrawIndexedIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	VkRenderPass			vkRndPass,
	VkFramebuffer			offscreenFbo,
	VkDescriptorSet descSet,
	const vk_buffer& drawCmds,
	const vk_buffer& camData,
	VkBuffer				drawCmdCount,
	VkBuffer                indexBuff,
	VkIndexType             indexType,
	u32                     maxDrawCallCount,
	const VkClearValue* clearVals,
	const vk_program& program,
	bool                    fullPass
) {
	vk_label label = { cmdBuff,"Draw Indexed Indirect Pass",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderPassBeginInfo rndPassBegInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	rndPassBegInfo.renderPass = vkRndPass;
	rndPassBegInfo.framebuffer = offscreenFbo;
	rndPassBegInfo.renderArea = scissor;
	rndPassBegInfo.clearValueCount = clearVals ? 2 : 0;
	rndPassBegInfo.pClearValues = clearVals;

	vkCmdBeginRenderPass( cmdBuff, &rndPassBegInfo, VK_SUBPASS_CONTENTS_INLINE );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	if( fullPass )
	{
		vkCmdBindDescriptorSets( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, program.pipeLayout, 1, 1, &descSet, 0, 0 );
	}

	struct { u64 vtxAddr, transfAddr, drawCmdAddr, camAddr; } push = {
		globVertexBuff.devicePointer, instDescBuff.devicePointer, drawCmds.devicePointer, camData.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof( push ), &push );


	vkCmdBindIndexBuffer( cmdBuff, indexBuff, 0, indexType );

	vkCmdDrawIndexedIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCmdCount, 0, maxDrawCallCount, sizeof( draw_command ) );

	vkCmdEndRenderPass( cmdBuff );
}

inline static void
DrawIndirectPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	const VkRenderingAttachmentInfoKHR* pColInfo,
	const VkRenderingAttachmentInfoKHR* pDepthInfo,
	const vk_buffer& drawCmds,
	VkBuffer				drawCmdCount,
	const vk_program& program,
	const mat4& viewProjMat
) {
	vk_label label = { cmdBuff,"Draw Indirect Pass",{} };

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderingInfo renderInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
	renderInfo.renderArea = scissor;
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = pColInfo ? 1 : 0;
	renderInfo.pColorAttachments = pColInfo;
	renderInfo.pDepthAttachment = pDepthInfo;
	vkCmdBeginRendering( cmdBuff, &renderInfo );

	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );

	//vk_descriptor_info descriptors[] = { Descriptor( drawCmds ), Descriptor( instDescBuff ), Descriptor( meshletBuff ) };
	//vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, program.descUpdateTemplate, program.pipeLayout, 0, descriptors );

	//struct { mat4 viewProj; vec4 color; } push = { viewProjMat, { 255,0,0,0 } };
	struct { mat4 viewProj; vec4 color; u64 cmdAddr; u64 transfAddr; u64 meshletAddr; } push = {
		viewProjMat, { 255,0,0,0 }, drawCmds.devicePointer, instDescBuff.devicePointer, meshletBuff.devicePointer };
	vkCmdPushConstants( cmdBuff, program.pipeLayout, program.pushConstStages, 0, sizeof( push ), &push );

	u32 maxDrawCnt = drawCmds.size / sizeof( draw_indirect );
	vkCmdDrawIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_indirect, cmd ), drawCmdCount, 0, maxDrawCnt, sizeof( draw_indirect ) );

	vkCmdEndRendering( cmdBuff );
}



// TODO: adjust for more draws ?
inline static void
DrawIndexedIndirectMerged(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	const VkRenderingAttachmentInfo* pColInfo,
	const VkRenderingAttachmentInfo* pDepthInfo,
	VkPipelineLayout       pipelineLayout,
	const vk_buffer& indexBuff,
	const vk_buffer& drawCmds,
	const vk_buffer& drawCount,
	const void* pPushData,
	u64 pushDataSize
) {
	vk_label label = { cmdBuff,"Draw Indexed Indirect Merged Pass",{} };

	constexpr u32 maxDrawCount = 1;

	VkRect2D scissor = { { 0, 0 }, { sc.width, sc.height } };

	VkRenderingInfo renderInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
	renderInfo.renderArea = scissor;
	renderInfo.layerCount = 1;
	renderInfo.colorAttachmentCount = pColInfo ? 1 : 0;
	renderInfo.pColorAttachments = pColInfo;
	renderInfo.pDepthAttachment = pDepthInfo;
	vkCmdBeginRendering( cmdBuff, &renderInfo );


	vkCmdSetScissor( cmdBuff, 0, 1, &scissor );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline );


	vkCmdPushConstants( cmdBuff, pipelineLayout, VK_SHADER_STAGE_ALL, 0, pushDataSize, pPushData );


	vkCmdBindIndexBuffer( cmdBuff, indexBuff.hndl, 0, VK_INDEX_TYPE_UINT32 );

	vkCmdDrawIndexedIndirectCount(
		cmdBuff, drawCmds.hndl, offsetof( draw_command, cmd ), drawCount.hndl, 0, maxDrawCount, sizeof( draw_command ) );

	vkCmdEndRendering( cmdBuff );
}

#if 0
// TODO: must remake single pass
inline static void
DepthPyramidPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	u64						mipLevelsCount,
	VkSampler				quadMinSampler,
	VkImageView( &depthMips )[ MAX_MIP_LEVELS ],
	const vk_image& depthTarget,
	const vk_image& depthPyramid,
	const vk_program& program
) {
	static_assert( 0 );
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
	depthPyramidDescs[ 0 ] = { 0, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	depthPyramidDescs[ 1 ] = { quadMinSampler, 0, VK_IMAGE_LAYOUT_GENERAL };
	depthPyramidDescs[ 2 ] = { depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size };
	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
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
#endif


inline static void
DepthPyramidMultiPass(
	VkCommandBuffer			cmdBuff,
	VkPipeline				vkPipeline,
	vk_image depthTarget,
	vk_image depthPyramid,
	u16 depthTargetIdx,
	std::span<u16> hizMipIdx,
	u16 samplerIdx,
	group_size groupSize
) {
	vk_label label = { cmdBuff,"HiZ Multi Pass",{} };

	VkImageMemoryBarrier2KHR hizBeginBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2(
			depthPyramid.hndl,
			0,0,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};
	VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, hizBeginBarriers );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline );

	struct hiz_push
	{
		vec2 reduceData;
		u32 samplerIdx;

		u32 inSampledImgIdx;
		u32 outImgIdx;
	};

	VkMemoryBarrier2 executionBarrier[] = { VkMakeMemoryBarrier2(
		VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
	) };

	for( u64 i = 0; i < depthPyramid.mipCount; ++i )
	{
		u32 levelWidth = std::max( 1u, u32( depthPyramid.width ) >> i );
		u32 levelHeight = std::max( 1u, u32( depthPyramid.height ) >> i );

		hiz_push pushConst = { .reduceData = { ( float ) levelWidth, ( float ) levelHeight }, .samplerIdx = samplerIdx };

		[[unlikely]]
		if( i == 0 )
		{
			pushConst.inSampledImgIdx = depthTargetIdx;
			pushConst.outImgIdx = hizMipIdx[ 0 ];
		} 
		else
		{
			pushConst.inSampledImgIdx = hizMipIdx[ i - 1 ];
			pushConst.outImgIdx = hizMipIdx[ i ];
		}

		vkCmdPushConstants(
			cmdBuff, vk.descMngr.globalPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeof( pushConst ), &pushConst );

		u32 dispatchX = GroupCount( levelWidth, groupSize.x );
		u32 dispatchY = GroupCount( levelHeight, groupSize.y );
		vkCmdDispatch( cmdBuff, dispatchX, dispatchY, 1 );

		VkCmdPipelineFlushCacheBarriers( cmdBuff, executionBarrier );
	}

	// TODO: do we need ?
	VkImageMemoryBarrier2KHR hizEndBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT_KHR | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),

		VkMakeImageBarrier2(
			depthPyramid.hndl,
			VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_ACCESS_2_SHADER_READ_BIT_KHR,
			VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, hizEndBarriers );
}

// TODO: optimize
inline static void
AverageLuminancePass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			avgPipe,
	const vk_program& avgProg,
	const vk_image& fboHdrColTrg,
	float				dt
) {
	vk_label label = { cmdBuff,"Averge Lum Pass",{} };
	// NOTE: inspired by http://www.alextardif.com/HistogramLuminance.html
	avg_luminance_info avgLumInfo = {};
	avgLumInfo.minLogLum = -10.0f;
	avgLumInfo.invLogLumRange = 1.0f / 12.0f;
	avgLumInfo.dt = dt;

	vkCmdFillBuffer( cmdBuff, shaderGlobalsBuff.hndl, 0, shaderGlobalsBuff.size, 0u );
	vkCmdFillBuffer( cmdBuff, shaderGlobalSyncCounterBuff.hndl, 0, shaderGlobalSyncCounterBuff.size, 0u );

	VkMemoryBarrier2KHR zeroInitGlobals[] = {
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT )
	};
	VkImageMemoryBarrier2KHR hrdColTargetAcquire[] = {
		VkMakeImageBarrier2(
			fboHdrColTrg.hndl,
		VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
		VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
		VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkCmdPipelineFlushCacheBarriers( cmdBuff, zeroInitGlobals );
	VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, hrdColTargetAcquire );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, avgPipe );

	vk_descriptor_info avgLumDescs[] = {
		{ 0, fboHdrColTrg.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR },
		Descriptor( avgLumBuff ),
		Descriptor( shaderGlobalsBuff ),
		Descriptor( shaderGlobalSyncCounterBuff )
	};

	vkCmdPushDescriptorSetWithTemplateKHR( cmdBuff, avgProg.descUpdateTemplate, avgProg.pipeLayout, 0, &avgLumDescs[ 0 ] );

	vkCmdPushConstants( cmdBuff, avgProg.pipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( avgLumInfo ), &avgLumInfo );

	vkCmdDispatch(
		cmdBuff, GroupCount( fboHdrColTrg.width, avgProg.groupSize.x ), GroupCount( fboHdrColTrg.height, avgProg.groupSize.y ), 1 );
}

// TODO: optimize
inline static void
FinalCompositionPass(
	VkCommandBuffer		cmdBuff,
	VkPipeline			tonePipe,
	const vk_image& fboHdrColTrg,
	const vk_program& tonemapProg,
	VkImage				scImg,
	VkImageView			scView
) {
	vk_label label = { cmdBuff,"Final Composition Pass",{} };

	VkImageMemoryBarrier2 swapchainWriteBarrier[] =
	{
		VkMakeImageBarrier2(
			scImg,
			0, 0,
			VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_ASPECT_COLOR_BIT )
	};

	VkMemoryBarrier2 avgLumReadBarrier[] =
	{
		VkMakeMemoryBarrier2(
			VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT )
	};

	VkCmdPipelineFlushCacheBarriers( cmdBuff, avgLumReadBarrier );
	VkCmdPipelineImgLayoutTransitionBarriers( cmdBuff, swapchainWriteBarrier );

	vkCmdBindPipeline( cmdBuff, VK_PIPELINE_BIND_POINT_COMPUTE, tonePipe );

	vk_descriptor_info tonemapDescs[] = {
		{ 0, fboHdrColTrg.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR },
		{ 0, scView, VK_IMAGE_LAYOUT_GENERAL },
		avgLumBuff.Descriptor()
	};

	vkCmdPushDescriptorSetWithTemplateKHR(
		cmdBuff, tonemapProg.descUpdateTemplate, tonemapProg.pipeLayout, 0, &tonemapDescs[ 0 ] );

	assert( ( fboHdrColTrg.width == sc.width ) && ( fboHdrColTrg.height == sc.height ) );
	vkCmdDispatch( cmdBuff,
				   GroupCount( fboHdrColTrg.width, tonemapProg.groupSize.x ),
				   GroupCount( fboHdrColTrg.height, tonemapProg.groupSize.y ), 1 );

}


// TODO: enforce some clearOp ---> clearVals params correctness ?
inline VkRenderingAttachmentInfo 
VkMakeAttachemntInfo( 
	VkImageView view,
	VkAttachmentLoadOp loadOp, 
	VkAttachmentStoreOp storeOp, 
	VkClearValue clearValue 
) {
	return {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
		.imageView = view,
		.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
		.loadOp = loadOp,
		.storeOp = storeOp,
		.clearValue = ( loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR ) ? clearValue : VkClearValue{},
	};
}

inline void VkWaitSemaphores(
	VkDevice vkDevice, 
	std::initializer_list<VkSemaphore> semas, 
	std::initializer_list<u64> values, 
	u64 maxWait = UINT64_MAX 
) {
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
	VkResult waitResult = vkWaitSemaphores( vkDevice, &waitInfo, maxWait );
	VK_CHECK( VK_INTERNAL_ERROR( waitResult > VK_TIMEOUT ) );
}

// TODO: use VkSubmitInfo2 ?
inline VkSubmitInfo VkMakeSubmitInfo(
	std::initializer_list<VkCommandBuffer> cmdBuffers,
	std::initializer_list<VkSemaphore> waitSemas,
	std::initializer_list<VkSemaphore> signalSemas,
	std::initializer_list<u64> signalValues,
	VkPipelineStageFlags waitDstStageMsk
) {
	VkTimelineSemaphoreSubmitInfo timelineInfo = {
		.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
		.signalSemaphoreValueCount = std::size( signalValues ),
		.pSignalSemaphoreValues = std::data( signalValues )
	};

	VkSubmitInfo submitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		.pNext = &timelineInfo,
		.waitSemaphoreCount = ( u32 ) std::size( waitSemas ),
		.pWaitSemaphores = std::data( waitSemas ),
		.pWaitDstStageMask = &waitDstStageMsk,
		.commandBufferCount = ( u32 ) std::size( cmdBuffers ),
		.pCommandBuffers = std::data( cmdBuffers ),
		.signalSemaphoreCount = ( u32 ) std::size( signalSemas ),
		.pSignalSemaphores = std::data( signalSemas )
	};

	return submitInfo;
}

void vk_backend::HostFrames( const frame_data& frameData, gpu_data& gpuData )
{
	using namespace DirectX;

	u64 currentFrameIdx = this->vFrameIdx++;
	const virtual_frame& thisVFrame = GetNextFrame( currentFrameIdx );

	VkWaitSemaphores( this->pDevice.device, { this->timelineSema }, { currentFrameIdx }, UINT64_MAX );
	
	VK_CHECK( vkResetCommandPool( dc.device, thisVFrame.cmdPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT ) );

	u32 imgIdx;
	VK_CHECK( vkAcquireNextImageKHR( dc.device, sc.swapchain, UINT64_MAX, thisVFrame.canGetImgSema, 0, &imgIdx ) );

	// TODO: no copy
	global_data globs = {
		.proj = frameData.proj,
		.mainView = frameData.mainView,
		.activeView = frameData.activeView,
		.worldPos = frameData.worldPos,
		.camViewDir = frameData.camViewDir,
	};
	std::memcpy( thisVFrame.frameData.hostVisible, &globs, sizeof( globs ) );

	// TODO: 
	if( currentFrameIdx < VK_MAX_FRAMES_IN_FLIGHT_ALLOWED )
	{
		u16 globalsDescIdx = VkAllocDescriptorIdx( 
			dc.device, VkDescriptorBufferInfo{ thisVFrame.frameData.hndl, 0, sizeof( globs ) }, vk.descMngr );
		//VkFlushDescriptorUpdates( dc.device, vk.descDealer );

		const_cast< virtual_frame& >( thisVFrame ).frameDescIdx = globalsDescIdx;
	}

	std::vector<VkImageMemoryBarrier2> layoutTransitionBarriers;
	[[unlikely]]
	if( !vk.imageMap.isValid( renderPath.hDepthPyramid ) )
	{
		constexpr u16 squareDim = 512;
		u8 hiZMipCount = std::min( GetImgMipCountForPow2( squareDim, squareDim ), MAX_MIP_LEVELS );

		constexpr VkImageUsageFlags hiZUsg =
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		renderPath.hDepthPyramid = vk.imageMap.insert( VkCreateAllocBindImage( {
			.name = "Img_depthPyramid",
			.format = VK_FORMAT_R32_SFLOAT,
			.usg = hiZUsg,
			.width = squareDim,
			.height = squareDim,
			.layerCount = 1,
			.mipCount = hiZMipCount } );

		const vk_image& hiz = vk.imageMap[ renderPath.hDepthPyramid ];

		u16 hizDescIdx = VkAllocDescriptorIdx(
			dc.device, VkDescriptorImageInfo{ 0,hiz.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR }, vk.descMngr );
		renderPath.hizTargetIdx = hizDescIdx;
		renderPath.hizMipIdx[0] = hizDescIdx;

		for( u32 i = 0; i < hiz.mipCount; ++i )
		{
			u16 hizMipDescIdx = VkAllocDescriptorIdx(
				dc.device, VkDescriptorImageInfo{ 0,hiz.optionalViews[ i ], VK_IMAGE_LAYOUT_GENERAL }, vk.descMngr );
			renderPath.hizMipIdx[ i + 1 ] = hizMipDescIdx;
		}
		

		u16 quadMinSamplerIdx = VkAllocDescriptorIdx(
			dc.device, vk_sampler_descriptor_info{ renderPath.quadMinSampler }, vk.descMngr );
		renderPath.quadMinSamplerIdx = quadMinSamplerIdx;
	}
	[[unlikely]]
	if( !vk.imageMap.isValid( renderPath.hDepthTarget ) )
	{
		constexpr VkImageUsageFlags usgFlags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		renderPath.hDepthTarget = vk.imageMap.insert( VkCreateAllocBindImage( {
				.name = "Img_depthTarget",
				.format = VK_FORMAT_D32_SFLOAT,
				.usg = usgFlags,
				.width = sc.width,
				.height = sc.height,
				.layerCount = 1,
				.mipCount = 1 },
				vkAlbumArena, dc.device, dc.gpu ) );


		const vk_image& depthTarget = vk.imageMap[ renderPath.hDepthTarget ];

		VkImageMemoryBarrier2 initBarriers[] = {
		VkMakeImageBarrier2(
			depthTarget.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_DEPTH_BIT ),
			//VkMakeImageBarrier2(
			//	depthPyramid.hndl,
			//	0, 0,
			//	0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
			//	VK_IMAGE_LAYOUT_UNDEFINED,
			//	VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
			//	VK_IMAGE_ASPECT_COLOR_BIT )
		};

		layoutTransitionBarriers.append_range( initBarriers );

		//VkCmdPipelineImgLayoutTransitionBarriers( thisVFrame.cmdBuff, initBarriers );


		u16 depthDescIdx = VkAllocDescriptorIdx(
			dc.device, VkDescriptorImageInfo{ 0,depthTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR }, vk.descMngr );
		renderPath.depthTargetIdx = depthDescIdx;
	}
	[[unlikely]]
	if( !vk.imageMap.isValid( renderPath.hColorTarget ) )
	{
		constexpr VkImageUsageFlags usgFlags =
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

		renderPath.hColorTarget = vk.imageMap.insert( VkCreateAllocBindImage( {
				.name = "Img_colorTarget",
				.format = VK_FORMAT_R16G16B16A16_SFLOAT,
				.usg = usgFlags,
				.width = sc.width,
				.height = sc.height,
				.layerCount = 1,
				.mipCount = 1 },
				vkAlbumArena, dc.device, dc.gpu ) );

		const vk_image& colorTarget = vk.imageMap[ renderPath.hColorTarget ];

		VkImageMemoryBarrier2 initBarriers[] = { VkMakeImageBarrier2(
			colorTarget.hndl,
			0, 0,
			0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT ) };

		layoutTransitionBarriers.append_range( initBarriers );
		//VkCmdPipelineImgLayoutTransitionBarriers( thisVFrame.cmdBuff, initBarrier );

		u16 colDescIdx = VkAllocDescriptorIdx(
			dc.device, VkDescriptorImageInfo{ 0,colorTarget.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR }, vk.descMngr );
		renderPath.colTargetIdx = colDescIdx;

		u16 pbrSamplerIdx = VkAllocDescriptorIdx(
			dc.device, vk_sampler_descriptor_info{ renderPath.pbrSampler }, vk.descMngr );
		renderPath.pbrSamplerIdx = pbrSamplerIdx;
	}
	
	this->pDevice.FlushDescriptorUpdates( this->descManager.pendingUpdates );
	this->descManager.pendingUpdates.resize( 0 );

	const vk_image& depthTarget = vk.imageMap[ renderPath.hDepthTarget ];
	const vk_image& depthPyramid = vk.imageMap[ renderPath.hDepthPyramid ];
	const vk_image& colorTarget = vk.imageMap[ renderPath.hColorTarget ];

	auto depthWrite = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto depthRead = VkMakeAttachemntInfo( depthTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorWrite = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {} );
	auto colorRead = VkMakeAttachemntInfo( colorTarget.view, VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );

	static bool rescUploaded = 0;
	[[unlikely]]
	if( !rescUploaded )
	{
		if( !imguiVkCtx.fontsImg.hndl )
		{
			auto [pixels, width, height] = ImguiGetFontImage();
			u64 uploadSize = width * height * sizeof( u32 );
			//assert with fontIMg size
			{
				VkImageMemoryBarrier2 fontsBarrier[] = { VkMakeImageBarrier2( fonts.hndl, 0, 0,
														 VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
														 VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
														 VK_IMAGE_LAYOUT_UNDEFINED,
														 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
														 VK_IMAGE_ASPECT_COLOR_BIT ) };
				VkCmdPipelineImgLayoutTransitionBarriers( thisVFrame.cmdBuff, fontsBarrier );
			}

			vk_buffer upload = VkCreateAllocBindBuffer( uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vkStagingArena, dc.gpu );
			std::memcpy( upload.hostVisible, pixels, uploadSize );

			VkBufferImageCopy imgCopyRegion = {
				.imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
				.imageExtent = { width,height,1 }
			};
			vkCmdCopyBufferToImage(
				thisVFrame.cmdBuff, upload.hndl, fonts.hndl, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imgCopyRegion );

			{
				VkImageMemoryBarrier2 fontsBarrier[] = { VkMakeImageBarrier2( fonts.hndl,
														 VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
														 VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
														 VK_ACCESS_2_SHADER_READ_BIT_KHR,
														 VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR,
														 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
														 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
														 VK_IMAGE_ASPECT_COLOR_BIT ) };
				VkCmdPipelineImgLayoutTransitionBarriers( thisVFrame.cmdBuff, fontsBarrier );
			}

			VkDbgNameObj( fonts.hndl, dc.device, "Img_Fonts" );
			imguiVkCtx.fontsImg = fonts;
		}

		VkUploadResources( thisVFrame.cmdBuff, entities, currentFrameIdx );
		rescUploaded = 1;

		for( const auto& i : textures )
		{
			u16 itsJustTemp = VkAllocDescriptorIdx(
				dc.device, VkDescriptorImageInfo{ 0, i.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR }, vk.descMngr );
		}

		// TODO: remove from here
		WrtieOcclusionBoxes( ( dbg_vertex* ) vkDbgCtx.dbgTrisBuff.hostVisible );
	}

	static bool initBuffers = 0;
	if( !initBuffers )
	{
		vkCmdFillBuffer( thisVFrame.cmdBuff, depthAtomicCounterBuff.hndl, 0, depthAtomicCounterBuff.size, 0u );
		// TODO: rename 
		vkCmdFillBuffer( thisVFrame.cmdBuff, atomicCounterBuff.hndl, 0, atomicCounterBuff.size, 0u );

		VkMemoryBarrier2 initBuffersBarriers[] = {
			VkMakeMemoryBarrier2(
				VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
				VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT ),
		};

		VkCmdPipelineFlushCacheBarriers( thisVFrame.cmdBuff, initBuffersBarriers );

		initBuffers = 1;
	}

	this->pDevice.FlushDescriptorUpdates( this->descManager.pendingUpdates );
	this->descManager.pendingUpdates.resize( 0 );

	// TODO: must reset somewhere
	gpuData.timeMs = VkCmdReadGpuTimeInMs( thisVFrame.cmdBuff, thisVFrame.frameTimer );
	vkCmdResetQueryPool( thisVFrame.cmdBuff, thisVFrame.frameTimer.queryPool, 0, thisVFrame.frameTimer.queryCount );
	{
		vk_time_section timePipeline = { thisVFrame.cmdBuff, thisVFrame.frameTimer.queryPool, 0 };
		VkViewport viewport = this->swapchain.GetRenderViewport();
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &viewport );

		VkClearValue clearVals[ 2 ] = {};

		vkCmdBindDescriptorSets( thisVFrame.cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, 
								 vk.descMngr.globalPipelineLayout, 0, 1, &vk.descMngr.set, 0, 0 );

		struct { u64 vtxAddr, transfAddr, camIdx; } zPrepassPush = {
			globVertexBuff.devicePointer, instDescBuff.devicePointer, thisVFrame.frameDescIdx };

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			gfxZPrepass,
			0,
			&depthWrite,
			vk.descMngr.globalPipelineLayout,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			&zPrepassPush,
			sizeof( zPrepassPush )
		);

		DebugDrawPass(
			thisVFrame.cmdBuff,
			vkDbgCtx.drawAsTriangles,
			0,
			&depthRead,
			vkDbgCtx.dbgTrisBuff,
			vkDbgCtx.pipeProg,
			frameData.mainProjView,
			{ 0,boxTrisVertexCount } );

		vkCmdBindDescriptorSets(
			thisVFrame.cmdBuff,
			VK_PIPELINE_BIND_POINT_COMPUTE,
			vk.descMngr.globalPipelineLayout,
			0, 1,
			&vk.descMngr.set, 0, 0 );

		DepthPyramidMultiPass(
			thisVFrame.cmdBuff,
			rndCtx.compHiZPipeline,
			depthTarget,
			depthPyramid,
			renderPath.depthTargetIdx,
			std::span{renderPath.hizMipIdx},
			renderPath.quadMinSamplerIdx,
			{ 32, 32, 1 }
		);


		VkMemoryBarrier2 clearDrawCountBarrier[] = { VkMakeMemoryBarrier2(
			VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT_KHR, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT_KHR,
			VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR, VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR )
		};
		VkCmdPipelineFlushCacheBarriers( thisVFrame.cmdBuff, clearDrawCountBarrier );

		CullPass(
			thisVFrame.cmdBuff,
			rndCtx.compPipeline,
			depthPyramid,
			renderPath.quadMinSampler,
			thisVFrame.frameDescIdx,
			renderPath.hizTargetIdx,
			renderPath.quadMinSamplerIdx,
			{ 32, 1, 1 }
		);


		vkCmdBindDescriptorSets(
			thisVFrame.cmdBuff,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.descMngr.globalPipelineLayout,
			0, 1, &vk.descMngr.set, 0, 0 );

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			gfxZPrepass,
			0,
			&depthWrite,
			vk.descMngr.globalPipelineLayout,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			&zPrepassPush,
			sizeof( zPrepassPush )
		);

		struct
		{
			u64 vtxAddr, transfAddr, camIdx, mtrlsAddr, lightsAddr, samplerIdx;
		} shadingPush = {
					 globVertexBuff.devicePointer,
					 instDescBuff.devicePointer,
					 thisVFrame.frameDescIdx,
					 materialsBuff.devicePointer,
					 lightsBuff.devicePointer,
					 renderPath.pbrSamplerIdx
		};

		vkCmdBindDescriptorSets(
			thisVFrame.cmdBuff, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.descMngr.globalPipelineLayout, 0, 1, &vk.descMngr.set, 0, 0 );

		DrawIndexedIndirectMerged(
			thisVFrame.cmdBuff,
			rndCtx.gfxMergedPipeline,
			&colorWrite,
			&depthWrite,
			vk.descMngr.globalPipelineLayout,
			indirectMergedIndexBuff,
			drawMergedCmd,
			drawMergedCountBuff,
			&shadingPush,
			sizeof( shadingPush )
		);

		DebugDrawPass(
			thisVFrame.cmdBuff,
			vkDbgCtx.drawAsTriangles,
			&colorRead,
			&depthRead,
			vkDbgCtx.dbgTrisBuff,
			vkDbgCtx.pipeProg,
			frameData.activeProjView,
			{ 0,boxTrisVertexCount } );



		dbgLineGeomCache = ComputeSceneDebugBoundingBoxes( XMLoadFloat4x4A( &frameData.frustTransf ), entities );
		// TODO: might need to double buffer
		std::memcpy( vkDbgCtx.dbgLinesBuff.hostVisible, std::data( dbgLineGeomCache ), BYTE_COUNT( dbgLineGeomCache ) );

		// TODO: remove the depth target from these ?
		if( dbgDraw && ( frameData.freezeMainView || frameData.dbgDraw ) )
		{
			u64 frustBoxOffset = std::size( entities.instAabbs ) * boxLineVertexCount;

			range drawRange = {};
			drawRange.offset = frameData.dbgDraw ? 0 : frustBoxOffset;
			drawRange.size = ( frameData.freezeMainView && frameData.dbgDraw ) ?
				std::size( dbgLineGeomCache ) : ( frameData.freezeMainView ? boxLineVertexCount : frustBoxOffset );


			DebugDrawPass( thisVFrame.cmdBuff,
						   vkDbgCtx.drawAsLines,
						   &colorRead,
						   0,
						   vkDbgCtx.dbgLinesBuff,
						   vkDbgCtx.pipeProg,
						   frameData.activeProjView,
						   drawRange );

			if( frameData.dbgDraw )
			{
				DrawIndirectPass( thisVFrame.cmdBuff,
								  gfxDrawIndirDbg,
								  &colorRead,
								  0,
								  drawCmdAabbsBuff,
								  drawCountDbgBuff.hndl,
								  dbgDrawProgram,
								  frameData.activeProjView );
			}

		}

		AverageLuminancePass(
			thisVFrame.cmdBuff,
			rndCtx.compAvgLumPipe,
			avgLumCompProgram,
			colorTarget,
			frameData.elapsedSeconds );

		FinalCompositionPass( thisVFrame.cmdBuff,
							  rndCtx.compTonemapPipe,
							  colorTarget,
							  tonemapCompProgram,
							  sc.imgs[ imgIdx ],
							  sc.imgViews[ imgIdx ] );

		VkImageMemoryBarrier2KHR compositionEndBarriers[] = {
			VkMakeImageBarrier2( colorTarget.hndl,
								 VK_ACCESS_2_SHADER_READ_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 0, 0,
								 VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL_KHR,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
								 VK_IMAGE_ASPECT_COLOR_BIT ),
			VkMakeImageBarrier2( sc.imgs[ imgIdx ],
								 VK_ACCESS_2_SHADER_WRITE_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT_KHR,
								 VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT_KHR | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
								 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR ,
								 VK_IMAGE_LAYOUT_GENERAL,
								 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
								 VK_IMAGE_ASPECT_COLOR_BIT ) };

		VkCmdPipelineImgLayoutTransitionBarriers( thisVFrame.cmdBuff, compositionEndBarriers );

		VkViewport uiViewport = { 0, 0, ( float ) sc.width, ( float ) sc.height, 0, 1.0f };
		vkCmdSetViewport( thisVFrame.cmdBuff, 0, 1, &uiViewport );

		auto swapchainUIRW = VkMakeAttachemntInfo(
			sc.imgViews[ imgIdx ], VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE, {} );
		ImguiDrawUiPass( imguiVkCtx, thisVFrame.cmdBuff, &swapchainUIRW, 0, currentFrameIdx );


		VkImageMemoryBarrier2KHR presentWaitBarrier[] = { VkMakeImageBarrier2(
			sc.imgs[ imgIdx ],
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT_KHR,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
			0, 0,
			VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_ASPECT_COLOR_BIT ) };

		VkCmdPipelineImgLayoutTransitionBarriers( thisVFrame.cmdBuff, presentWaitBarrier );
	}

	constexpr VkPipelineStageFlags waitDstStageMsk = 
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;// VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;


	VkSubmitInfo submitInfo = VkMakeSubmitInfo(
		{ thisVFrame.cmdBuff }, { thisVFrame.canGetImgSema },
		{ thisVFrame.canPresentSema, rndCtx.timelineSema }, { 0, rndCtx.vFrameIdx }, waitDstStageMsk );

	VkPresentInfoKHR presentInfo = { 
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &thisVFrame.canPresentSema,
		.swapchainCount = 1,
		.pSwapchains = &sc.swapchain,
		.pImageIndices = &imgIdx
	};
	VK_CHECK( vkQueuePresentKHR( dc.gfxQueue, &presentInfo ) );

}

#undef HTVK_NO_SAMPLER_REDUCTION
#undef VK_APPEND_DESTROYER
#undef VK_CHECK