#pragma once

#ifndef __HT_RENDERER_TYPES_H__
#define __HT_RENDERER_TYPES_H__

#ifdef __cplusplus

#include "ht_core_types.h"
#include "ht_vec_types.h"

#define ALIGNAS( x ) alignas( x )

#define CONSTEXPR constexpr

#define STATIC_ASSERT( expr, str ) static_assert( expr, str )

#else

typedef int			i32;

typedef uint64_t	u64;
typedef uint		u32;
typedef uint16_t	u16;
//typedef uint8_t		u8;

typedef int4		i32x4;
typedef int3		i32x3;
typedef int2		i32x2;

typedef uint4		u32x4;
typedef uint3		u32x3;
typedef uint2		u32x2;

typedef float_16t2	fp16x2;

#define ALIGNAS( x )
// TODO: remove when hlsl gets a constexpr
#define CONSTEXPR static const

#define STATIC_ASSERT( expr, str )

#endif

CONSTEXPR u64 RASTER_MAX_VTX_PER_MLT = 64;
CONSTEXPR u64 RASTER_MAX_TRIS_PER_MLT = 128;

struct view_data
{
	float4x4	proj;
	float4x4	mainView;
	float4x4	prevView;
	float4x4	mainViewProj;
	float4x4	prevViewProj;
	float3	    worldPos;
	float		zNear;
	float3		camViewDir;
	float		pad0;
};

struct packed_trs
{
	float3	t;
	float	pad0;
	float4	r;
	float3	s;
	float	pad1;
};

STATIC_ASSERT( 48 == sizeof( packed_trs ), "Size mismatch!");

// NOTE: octahedron encoded normal + tan angle + bitan sign; will alias bc we will select the bit depth in the end/dec
typedef u32 oct10x2s_a11_s1;

// NOTE: the positions will be given as a bit stream of variable len
struct packed_vtx_attr
{
	oct10x2s_a11_s1 encodedTBN;
	fp16x2			encodedUVs;
};

struct gpu_instance
{
	float4x3	toWorld;
	u32			meshIdx;
	u32			mtrlIdx;
};

STATIC_ASSERT( 56 == sizeof( gpu_instance ), "Size mismatch!");

// NOTE: weird alignments bc this will be read by the GPU !
struct gpu_mesh
{
	float3	aabbMin; // NOTE: we only quantize the meshlet aabbs be they're the main "draw primitive"
	float3	aabbMax;
	u32		meshletOffset;
	u32		vtxPosOffsetInBytes;
	u32		vtxAttrsOffset;
	u32		triOffset;
	u32		meshletCount;
	u32		vtxCount;
	u32		triCount;
};

struct gpu_meshlet
{
	float3	aabbMin;
	float3	aabbMax;
	u32		vtxPosOffsetBits;
	u32		vtxAttrsOffset;
	u32		triOffset;
	u32		vtxCount	: 8;
	u32		triCount	: 8;
	u32		posBitDepth	: 8;
	u32		padding0	: 8;
};

struct dispatch_command
{
#if defined( __cplusplus ) && defined( __VK )
	VkDispatchIndirectCommand cmd;
#else
	u32 localSizeX;
	u32 localSizeY;
	u32 localSizeZ;
#endif
};

struct draw_meshlet_command
{
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndexedIndirectCommand cmd;
#else
	u32    indexCount;
	u32    instanceCount;
	u32    firstIndex;
	u32    vertexOffset;
	u32    firstInstance;
#endif
};

struct draw_meshlet_cmd_data
{
	u32 globalInstId;
	u32 globalMltId;
	u32 vtxAttrOffset;
	u32 vtxPosOffsetInBits;
};

struct draw_instanced_indexed_indirect
{
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndexedIndirectCommand cmd;
#else
	u32    indexCount;
	u32    instanceCount;
	u32    firstIndex;
	u32    vertexOffset;
	u32    firstInstance;
#endif
};

struct downsample_info
{
	float2	invRes;
	u32		mips;
	u32		workGroupCount;
};

struct avg_luminance_info
{
	float minLogLum;
	float invLogLumRange;
	float dt;
};

struct dbg_vertex
{
	float3 pos;
};

struct dbg_aabb_instance
{
	float4x4	toWorld; // NOTE: 4x4 so we can cram in the frustum transfrom too
	float4		color;
	float3		minAabb;
	float3		maxAabb;
};

struct imgui_vertex
{
	float	x, y;
	float	u, v;
	u32		rgba8Unorm;
};

struct luminance_histogram
{
	u32 finalLumSum;
	u32 finalTailValsCount;
};

struct visible_instance
{
	u32 instId;
	u32 meshletOffset;
	u32 meshletCount;
	u32 vtxPosOffsetInBytes;
	u32 vtxAttrsOffset;
	u32 triOffset;
};

struct meshlet_cull_wok_item
{
	u32 instId;
	u32 globMltId;
	u32 vtxPosOffsetInBytes;
	u32 vtxAttrsOffset;
	u32 triOffset;
};

struct culling_params
{
	u32 instCount;
	u32 occludedInstCounterIdx; // NOTE: will be used in the 2nd pass

	u32 occludedInstBuffIdx;
	u32 instDescIdx;
	u32 meshDescIdx;
	u32 viewBuffIdx;
	u32 camIdx; // NOTE: for debug purposes
	u32 hizTexIdx;
	u32 hizSamplerIdx;
	u32 visibleItemsCountIdx;
	u32 visibleItemsIdx;

	u32	isLatePass;
	u32 toggleCulling;
};
// TODO: rename
struct draw_expansion_params
{
	u32 workCounterIdxConst;
	u32 srcBufferIdx;
	u32 expandedItemsBuffIdx;
	u32 expandedItemsCountIdx;
};

struct indirect_dispatcher_params
{
	u32 cullShaderWorkGrX;
	u32 dispatchCmdBuffIdx;
	u32 counterBufferIdx;
};

struct meshlet_cull_params
{
	u32 mltCountIdx;
	u32 expandedMltsIdx;

	u32 occludedMltBuffIdx;
	u32 occludedMltCountIdx;

	u32 instDescIdx;
	u32 viewBuffIdx;
	u32 camIdx; // NOTE: for debug purposes
	u32 hizTexIdx;
	u32 hizSamplerIdx;

	u32 drawCountIdx;
	u32 drawCmsIdx;
	u32 drawDataIdx;

	u32	isLatePass;
	u32 toggleCulling;
};

struct culling_init_params
{
	u32 visibleInstCounterIdx;
	u32 occludedInstCounterIdx;
	u32 visibleMeshletsCounterIdx;
	u32 occludedMeshletsCounterIdx;
	u32 drawCounterIdx;
	u32 cullShaderWorkGrX;
	u32 dispatchCmdBuffIdx;
	u32 instCount;
	u32 isLatePass;
};

struct vbuffer_params
{
	u32 drawDataBuffIdx;
	u32 instBuffIdx;
	u32 camIdx;
};
// TODO: maybe use the same struct here ?
struct depth_prepass_params
{
	u32 drawDataBuffIdx;
	u32 instBuffIdx;
	u32 mltBuffIdx;
	u32 camIdx;
};
struct meshlet_pass_params
{
	u32 drawDataBuffIdx;
	u32 instBuffIdx;
	u32 camIdx;
};

// NOTE: src and dst assumed to be the same dimensions, asserted on the host
struct vbuffer_dbg_draw_params
{
	u32x2	vbuffRes;
	u32 	srcIdx;
	u32 	dstIdx;
};

struct lambertian_clay_params
{
	u32x2	vbuffRes;
	u32		vbuffIdx; // NOTE: asserted in the renderer that they're eq size
	u32		dstIdx;
	u32		instBuffIdx;
	u32		meshDescIdx;
	u32		camIdx;
};

struct dbg_box_params
{
	u64 instBuffAddr;
	u64 vtxBuffAddr;
	u32 camIdx;
};

struct record_dbg_draw_params
{
	u64 gpuInstCountAddr;
	u64 dbgDrawCmdsAddr;
	u64 dbgDrawCountAddr;
	// TODO: use a dbg obj table or smth
	u32 indexCount;
	u32 firstIndex;
	u32 vertexOffset;
};

struct downsampler_params
{
	u32x2   srcResolution;
	// u32x2 numWorkgroups; // NOTE: only for DX12-hlsl
	u32     mipCount;
	u32		samplerIdx;
	u32		srcSrvIdx;
	u32		dstMipsIdx[ 12 ];
	// NOTE: this is used with globallycoherent, if we have more than 5 mips,
	// only the last group will perform reductions, hence it starts at mip 6 by reading this !!!!
	u32		fifthMipIdx;
	u32		atomicWgCounterIdx;
};

struct multi_pass_downsampler_params
{
	u32x2	srcSize;
	u32x2	dstSize;
	u32     reductionSamplerIdx;
	u32		pointSamplerIdx;
	u32     inImgIdx;
	u32     inImgLod;
	u32     outImgIdx;
	u32     isMip0FromNonPot;
};

struct global_data
{
	u64 mltAddr;
	u64 vtxPosAddr;
	u64 vtxAttrsAddr;
	u64 triAddr;
};

static const u64 GLOB_DATA_BINDING_SLOT = 0;

#endif // !__HT_RENDERER_TYPES_H__
