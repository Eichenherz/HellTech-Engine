#pragma once

#ifndef __HT_RENDERER_TYPES_H__
#define __HT_RENDERER_TYPES_H__

#ifdef __cplusplus

#include "ht_core_types.h"
#include "ht_vec_types.h"

#define ALIGNAS( x ) alignas( x )

#define STATIC_ASSERT( expr, str ) static_assert( expr, str )

#else

static const float invPi = 0.31830988618f;
static const float PI = 3.14159265359f;

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

#define ALIGNAS( x )

#define STATIC_ASSERT( expr, str )

#endif

struct view_data
{
	float4x4	proj;
	float4x4	mainView;
	float4x4	prevView;
	float4x4	mainViewProj;
	float4x4	prevViewProj;
	float3	    worldPos;
	float		pad0;
	float3		camViewDir;
	float		pad1;
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

struct packed_vtx
{
	float	px;
	float	py;
	float	pz;
	float	tu;
	float	tv;
	float	octNX;
	float	octNY;
	float	tanAngle;
	u16		tanSign;
};

// TODO: compressed coords u8, u16
struct vertex
{
	float px;
	float py;
	float pz;
	float tu;
	float tv;
	u32 snorm8octTanFrame;
};

// TODO: compress data more ?
struct gpu_instance
{
	packed_trs	toWorld;
	u32			meshIdx;
	u32			mtrlIdx;
};

// NOTE: weird alignments bc this will be read by the GPU !
struct gpu_mesh
{
	float3	minAabb;
	float3	maxAabb;
	u32		meshletOffset;
	u32		vtxOffset;
	u32		triOffset;
	u32		meshletCount;
	u32		vtxCount;
	u32		triCount;
};

struct gpu_meshlet
{
	float3	minAabb;
	float3	maxAabb;
	u32		vtxOffset;
	u32		triOffset;
	u16		vtxCount;
	u16		triCount;
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

struct draw_indexed_command
{
	u32		visMltIdx;
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

struct draw_indirect
{
	u64 drawIdx;
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndirectCommand cmd;
#else
	u32    vertexCount;
	u32    instanceCount;
	u32    firstVertex;
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
	float4 pos;
	float4 col;
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
	u32 		instId;
	u32 		meshletOffset;
	u32 		meshletCount;
	u32 		vtxOffset;
	u32 		triOffset;
};

struct visible_meshlet
{
	packed_trs	toWorld;
	//u32 		instId;
	u32 		absVtxOffset;
	u32 		absTriOffset;
	u16			vtxCount;
	u16			triCount;
};

struct culling_params
{
	u32 instCount;
	u32 visInstCacheIdx;
	u32 instDescIdx;
	u32 meshDescIdx;
	u32 camIdx;
	u32 hizTexIdx;
	u32 hizSamplerIdx;
	u32 visibleItemsCountIdx;
	u32 visibleItemsIdx;
	u32	isLatePass;
};

struct draw_expansion_params
{
	u32 workCounterIdxConst;
	u32 srcBufferIdx;
	u32 visMltBufferIdx;
	u32 visMltCounterIdxIdx;
	u32 counterIdx;
	u32 instDescIdx;
};

struct meshlet_issue_draws_params
{
	u32 visMltCountIdx;
	u32 workCountIdx;
	u32 srcBufferIdx;
	u32 drawCmdCounterIdx;
	u32 drawCmdsBuffIdx;
};

struct indirect_dispatcher_params
{
	u32 cullShaderWorkGrX;
	u32 dispatchCmdBuffIdx;
	u32 counterBufferIdx;
};

struct vbuffer_params
{
	u32 drawBuffIdx;
	u32 visMltBuffIdx;
	u32 camIdx;
};

struct vbuffer_dbg_draw_params
{
	u32 srcIdx;
	u32 dstIdx;
};

struct lambertian_clay_params
{
	float2	texResolution; // NOTE: asserted in the renderer that they're eq size
	u32		vbuffIdx;
	u32		dstIdx;
	u32		visMltBuffIdx;
	u32		camIdx;
};

struct global_data
{
	u64 mltAddr;
	u64 vtxAddr;
	u64 triAddr;
};

static const u64 GLOB_DATA_BINDING_SLOT = 0;

#endif // !__HT_RENDERER_TYPES_H__
