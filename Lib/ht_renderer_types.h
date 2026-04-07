#pragma once

#ifndef __HT_RENDERER_TYPES_H__
#define __HT_RENDERER_TYPES_H__

#ifdef __cplusplus

#include "ht_core_types.h"
#include "ht_vec_types.h"

#define ALIGNAS( x ) alignas( x )

#else

static const float invPi = 0.31830988618f;
static const float PI = 3.14159265359f;

typedef uint64_t	u64;
typedef uint		u32;
typedef uint16_t	u16;
//typedef uint8_t		u8;

typedef uint3		u32x3;

#define ALIGNAS( x )

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

struct ALIGNAS( 16 ) packed_trs
{
	float3	t;
	float	pad0;
	float4	r;
	float3	s;
	float	pad1;
};

struct packed_vtx
{
	float3	pos;
	float2	octNormal;
	float	tanAngle;
	float	u, v;
	u16		tanSign;
};

struct packed_vertex
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
struct ALIGNAS( 16 ) instance_desc
{
	packed_trs	toWorld;
	u32			meshIdx;
	u32			mtrlIdx;
};

// NOTE: weird alignments bc this will be read by the GPU !
struct gpu_mesh
{
	ALIGNAS( 16 ) float3	minAabb;
	ALIGNAS( 16 ) float3	maxAabb;
	u32						meshletOffset;
	u32						vtxOffset;
	u32						triOffset;
	u32						meshletCount;
	u32						vtxCount;
	u32						triCount;
};

struct gpu_meshlet
{
	ALIGNAS( 16 ) float3	minAabb;
	ALIGNAS( 16 ) float3	maxAabb;
	u32						vtxOffset;
	u32						triOffset;
	u32						vtxCount : 8;
	u32						triCount : 8;
	u32						padding : 16;
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

// TODO: remove
struct compacted_draw_args
{
	u32 nodeIdx;
	u32 materialIdx;
	u32 meshletIdx;
};

// TODO: rename
struct draw_command
{
	u32	drawIdx;
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

struct ALIGNAS( 16 ) dbg_vertex
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
	u32 instId;
	u32 meshletOffset;
	u32 meshletCount;
};

struct visible_meshlet
{
	u32 instId;
	u32 vtxOffset;
	u32 triOffset;
	u32	vtxCount : 8;
	u32	triCount : 8;
	u32	padding : 16;
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
	bool isLatePass;
};

struct draw_expansion_params
{
	u32 drawsCount;
	u32 srcBufferIdx;
	u32 dstBufferIdx;
	u32 counterIdx;
};

struct meshlet_issue_draws_params
{
	u32 mltCount;
	u32 srcBufferIdx;
	u32 drawCmdCounterIdx;
	u32 drawCmdsBuffIdx;
};

// TODO: place these together with stuff from ht_hlsl_lang
static const u32 MESHLET_BUFF_BINDING = 4;
static const u32 VTX_BUFF_BINDING = 5;
static const u32 TRI_BUFF_BINDING = 6;

#endif // !__HT_RENDERER_TYPES_H__
