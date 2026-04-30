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
	byte_view	vtxAsBytes;
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
	bool vBuff			= false;
	bool freezeMainView = false;
	bool dbgDraw		= false;

};

struct frame_data
{
	std::span<const view_data>		views;
	std::span<const instance_desc>	instances;
	float4x4						frustTransf;
	float							elapsedSeconds;
	renderer_dbg_draw				dbgDrawFlags;
};

#endif // !__ENGINE_TYPES_H__

