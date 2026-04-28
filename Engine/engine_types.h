#pragma once

#ifndef __ENGINE_TYPES_H__
#define __ENGINE_TYPES_H__

#include "ht_core_types.h"
#include "ht_vec_types.h"
#include "hell_pack.h"
#include "ht_renderer_types.h"
#include "ht_fixed_string.h"

struct gpu_data
{
	float timeMs;
};

using HRNDMESH32 = u32;

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
	packed_trs	transform;
	HRNDMESH32	meshIdx;
	u16			materialIdx;
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

