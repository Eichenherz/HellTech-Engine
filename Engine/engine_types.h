#pragma once

#ifndef __ENGINE_TYPES_H__
#define __ENGINE_TYPES_H__

#include "ht_core_types.h"
#include "ht_vec_types.h"
#include "hell_pack.h"
#include "r_data_structs.h"
#include "ht_slot_buffer.h"
#include "vk_resources.h"
#include "ht_fixed_string.h"

struct gpu_data
{
	float timeMs;
};

// NOTE: weird alignments bc this will be read by the GPU !
struct gpu_mesh
{
	alignas( 16 ) float3	minAabb;
	alignas( 16 ) float3	maxAabb;
	u32						meshletOffset;
	u32						vtxOffset;
	u32						triOffset;
	u32						meshletCount;
	u32						vtxCount;
	u32						triCount;
};

struct gpu_meshlet
{
	alignas( 16 ) float3	minAabb;
	alignas( 16 ) float3	maxAabb;
	u32						vtxOffset;
	u32						triOffset;
	u32						vtxCount : 8;
	u32						triCount : 8;
	u32						padding : 16;
};

// TODO: maybe don't expose this
using HRNDMESH32 = slot_buffer<u32>::hndl32;

enum class upload_t
{
	TEX = 0,
	MESH
};

struct mesh_upload_req
{
	vfs_path            filepath; // NOTE: we'll use as a name for the gpu resource
	hellpack_mesh_asset htAsset;
	HRNDMESH32          hSlot;
};

struct tex_upload
{
	vfs_path filepath; // NOTE: we'll use as a name for the gpu resource
	byte_view ddsTex;
	// dst slot
};

struct gpu_instance
{
	packed_trs transform;
	HRNDMESH32 meshIdx;
	u16 materialIdx;
};

struct frame_data
{
	std::span<const view_data>		views;
	std::span<const gpu_instance>	instances;
	float4x4						frustTransf;
	float							elapsedSeconds;
	bool							freezeMainView;
	bool							dbgDraw;
};

#endif // !__ENGINE_TYPES_H__

