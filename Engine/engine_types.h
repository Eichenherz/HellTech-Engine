#pragma once

#ifndef __ENGINE_TYPES_H__
#define __ENGINE_TYPES_H__

#include "core_types.h"

#include "ht_vec_types.h"

#include "hell_pack.h"

#include "r_data_structs.h"

#include "ht_slot_buffer.h"

#include "vk_resources.h"

#include "ht_fixed_string.h"

struct frame_data
{
	std::vector<view_data>  views;
	float4x4                frustTransf;
	float                   elapsedSeconds;
	bool                    freezeMainView;
	bool                    dbgDraw;
};

struct gpu_data
{
	float timeMs;
};

struct gpu_mesh_payload
{
	vk_buffer meshletBuffer;
	vk_buffer vertexBuffer;
	vk_buffer triangleBuffer;
};

// NOTE: weird alignments bc this will be read by the GPU !
struct gpu_mesh
{
	alignas( 16 ) vec3	minAabb;
	alignas( 16 ) vec3	maxAabb;
	char                padding[ 4 ];
	desc_hndl32			hMeshletBuffer;
	desc_hndl32			hVertexBuffer;
	desc_hndl32			hTriangleBuffer;
};

struct renderer_mesh_component
{
	gpu_mesh_payload payload;
	gpu_mesh         desc;
};

using HRNDMESH32 = slot_buffer<renderer_mesh_component>::hndl32;

enum class upload_resp_t
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

struct renderer_upload_resp
{
	gpu_mesh_payload payload;
	gpu_mesh         desc;
	HRNDMESH32       hSlot;
	upload_resp_t    respType;
};

struct gpu_instance
{
	packed_trs transform;
	HRNDMESH32 meshIdx;
	u16 materialIdx;
};

#endif // !__ENGINE_TYPES_H__

