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
	u32					meshletCount;
	desc_hndl32			hVertexBuffer;
	u32					vertexCount;
	desc_hndl32			hTriangleBuffer;
	u32					triangleCount;
};

// TODO: use a better container
struct renderer_mesh_components
{
	template<typename T> 
	using stretchybuf_t = virtual_stretchy_buffer<T>;

	using slotbuf_t = slot_buffer<u32>;
	using hndl32_t = slotbuf_t::hndl32;
	
	slotbuf_t                       slots;
	// NOTE: yes, these will have holes, they won't be large in theory so it's all fine
	stretchybuf_t<gpu_mesh_payload> payloads = { slotbuf_t::MAX_ENTRIES_RESERVED };
	stretchybuf_t<gpu_mesh>         descs    = { slotbuf_t::MAX_ENTRIES_RESERVED };

	inline hndl32_t PushEntry( const gpu_mesh_payload& pl, const gpu_mesh& desc ) 
	{
		hndl32_t h = slots.PushEntry( {} );

		payloads.resize( h.slotIdx + 1 );
		descs.resize( h.slotIdx + 1 );

		payloads[ h.slotIdx ] = pl;
		descs[ h.slotIdx ] = desc;

		return h;
	}

	inline void DeleteEntry( hndl32_t h )
	{
		slots.RemoveEntry( h );
		// NOTE: if it reaches here we didn't trigger any assert
		gpu_mesh_payload& pl = payloads[ h.slotIdx ];
		gpu_mesh& meshDesc = descs[ h.slotIdx ];

		std::memset( &pl, 0, sizeof( pl ) );
		std::memset( &meshDesc, 0, sizeof( meshDesc ) );
	}

	struct mesh_comp_ref
	{
		gpu_mesh_payload& payload;
		gpu_mesh&         desc;
	};
	inline mesh_comp_ref operator[]( hndl32_t h )
	{
		slots[ h ];
		// NOTE: if it reaches here we didn't trigger any assert ( slots[ h ] will assert if it's not valid )
		gpu_mesh_payload& pl = payloads[ h.slotIdx ];
		gpu_mesh& meshDesc = descs[ h.slotIdx ];
		return { .payload = pl, .desc = meshDesc };
	}
};

using HRNDMESH32 = renderer_mesh_components::hndl32_t;

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

