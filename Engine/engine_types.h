#pragma once

#ifndef __ENGINE_TYPES_H__
#define __ENGINE_TYPES_H__

#include "ht_core_types.h"
#include "ht_vec_types.h"
#include "hell_pack.h"
#include "ht_renderer_types.h"
#include "ht_fixed_string.h"

#include "ht_math.h"

#include <vector>

// CONVENTIONS -----------------------------------------------------------------
// -----------------------------------------------------------------------------
// WORLD BASIS
// NOTE: gltf world coord frame: RH, +X right( -X left ), +Y up, -Z forward
constexpr float3 WORLD_FWD	= { 0.0f,  0.0f, -1.0f };
constexpr float3 WORLD_LEFT = { -1.0f, 0.0f,  0.0f };
constexpr float3 WORLD_UP	= { 0.0f,  1.0f,  0.0f };

constexpr bool IS_WORLD_RH = CrossProd( WORLD_FWD, WORLD_LEFT ) == WORLD_UP;
static_assert( IS_WORLD_RH, "Current convention is RH !!! But basis doesn't match" );
// -----------------------------------------------------------------------------

// TODO: these must be strong typed
using HRNDMESH32 = u32;

using HJOBFENCE32 = u32;

enum class upload_t
{
	TEX = 0,
	MESH
};

struct mesh_upload_req
{
	byte_view	mltAsBytes;
	byte_view	vtxPosAsBytes;
	byte_view	vtxAttrsAsBytes;
	byte_view	triAsBytes;
	HRNDMESH32  hSlot;
};

struct mesh_upload_resp
{
	HRNDMESH32	hSlot;
};

struct tex_upload
{
	vfs_path	filepath; // NOTE: we'll use as a name for the gpu resource
	byte_view	ddsTex;
	// dst slot
};

struct instance_desc
{
	packed_trs	transform;
	HRNDMESH32	meshIdx;
	u16			materialIdx;
};

struct renderer_dbg_draw
{
	bool vBuffPixelHash	= false;
	bool vBuffMeshletId	= false;
	bool freezeMainView = false;
	bool dbgDraw		= false;
	bool drawXRayMode	= false;
	bool toggleInstCull = true;
	bool toggleMltCull	= true;
};

struct frame_data
{
	std::span<const view_data>		views;
	std::span<const instance_desc>	instances;
	float4x4						frustTransf;
	float							elapsedSeconds;
	renderer_dbg_draw				dbgDrawFlags;
};

constexpr u64 INVALID_QUERY = ~u64( 0 );

constexpr bool IsQueryValid( u64 q )
{
	return INVALID_QUERY != q;
}

struct ht_pipeline_stats
{
	fixed_string<64>	name;
	u64 				inputAssemblyVtxNum			= INVALID_QUERY;
	u64 				inputAssemblyPrimitiveNum	= INVALID_QUERY;
	u64 				vsInvocationNum				= INVALID_QUERY;
	u64 				clipInvocationNum			= INVALID_QUERY;
	u64 				clipPrimitiveNum			= INVALID_QUERY;
	u64 				psInvocationCount			= INVALID_QUERY;
	u64 				csInvocationCount			= INVALID_QUERY;
};

struct ht_timed_zone
{
	fixed_string<64>	name;
	float				timeMs;
};


struct gpu_data
{
	std::vector<ht_timed_zone>&		timedZones;
	std::vector<ht_pipeline_stats>& pipelinesStats;
};

#endif // !__ENGINE_TYPES_H__

