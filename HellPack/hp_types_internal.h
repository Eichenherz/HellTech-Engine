#pragma once

#ifndef __HP_TYPES_INTERNAL_H__
#define __HP_TYPES_INTERNAL_H__

#include <ht_core_types.h>
#include <ht_vec_types.h>
#include <ht_renderer_types.h>

#include <vector>
#include <ht_fixed_string.h>

#include <ankerl/unordered_dense.h>

enum class raw_mesh_topology_t : u32
{
    MESH,
    POINTS
};

struct raw_mesh
{
	fixed_string<128>   name;
	std::vector<float3> pos;
	std::vector<float3> normals;
	std::vector<float4> tans;
	std::vector<float2> uvs;
	std::vector<u32>    indices;
	aabb_t<float3>		aabb;
	u32                 materialIdx;
    raw_mesh_topology_t topology;
};

enum class image_channels_t : u8
{
	UNKNOWN = 0,
	R = 1,
	RG = 2,
	RGB = 3,
	RGBA = 4
};

enum class image_bit_depth_t : u8
{
	UNKNOWN = 0,
	B8  = 8,
	B16 = 16,
	B32 = 32
};

enum class image_pixel_type : u8
{
	UNKNOWN = 0,
	UBYTE,
	USHORT,
	FLOAT32
};

struct image_metadata
{
	u16					width;
	u16					height;
	image_channels_t	component;
	image_bit_depth_t	bits;
	image_pixel_type	pixelType;
};

struct raw_image_view
{
	std::span<const u8> data;
	image_metadata		metadata;
};

struct raw_material_info
{
    fixed_string<512>   name;

	float4		        baseColFactor;
	float		        metallicFactor;
	float		        roughnessFactor;
	float		        alphaCutoff;
	float3		        emissiveFactor;

	u16 		        baseColorIdx;
	u16 		        metallicRoughnessIdx;
	u16 		        normalIdx;
	u16 		        occlusionIdx;
	u16 		        emissiveIdx;
	u16 		        samplerIdx;

	alpha_mode	        alphaMode;
};

struct packed_trs;

struct raw_node
{
	packed_trs	    toWorld;
    aabb_t<float3>  aabb;
	u64			    meshHash;
};

template<TRIVIAL_T T>
struct ankerl_hash_as_bytes
{
	// NOTE: tells Ankerl to not mix the hash
	using is_avalanching = void;

	u64 operator()( const T& n ) const
	{
		return ankerl::unordered_dense::hash<std::string_view>{}( std::string_view{ ( const char* ) &n, sizeof( n ) } );
	}
};

constexpr bool operator==( const packed_trs& a, const packed_trs& b ) { return ht::all( a.t == b.t ) && ht::all( a.r == b.r ) && ht::all( a.s == b.s ); }
constexpr bool operator==( const raw_node& a, const raw_node& b ) { return ( a.meshHash == b.meshHash ) && ( a.toWorld == b.toWorld ); }

template<> struct ankerl::unordered_dense::hash<raw_node> : ankerl_hash_as_bytes<raw_node> {};

// NOTE: stupid C++
struct u32x3_eq
{
	bool operator()( const u32x3& a, const u32x3& b ) const { return a == b; }
};

struct triangle_pos
{
	float3 v0;
	float3 v1;
	float3 v2;
};

constexpr bool operator==( const triangle_pos& a, const triangle_pos& b )
{
	return ht::all( a.v0 == b.v0 ) && ht::all( a.v1 == b.v1 ) && ht::all( a.v2 == b.v2 );
}

template<TRIVIAL_T T>
using mlt_attr_vector = inline_array<T, RASTER_MAX_VTX_PER_MLT>;

using mlt_idx_vector = inline_array<u8, RASTER_MAX_TRIS_PER_MLT * 3>;
using mlt_idx_vector32 = inline_array<u32, RASTER_MAX_TRIS_PER_MLT * 3>;

struct hpk_meshlet
{
    mlt_attr_vector<float3>	pos		= {};
    mlt_attr_vector<float3>	norm	= {};
    mlt_attr_vector<float4>	tan 	= {};
    mlt_attr_vector<float2>	uvs 	= {};
    mlt_idx_vector			indices	= {};
    mlt_idx_vector			idxLod	= {};

    float					lodError = FLT_MAX;
    u16 					vtxCount = 0;
};

constexpr float LOD_MESH_LEVEL_RATIO = 0.25f;
constexpr u64   LODS_PER_MESHLET = 2; // NOTE: includes the src/lod0

struct hpk_meshlets_w_lod
{
    arena_array<hpk_meshlet, virtual_arena>	meshlets        = {};
    float									meshLevelError  = FLT_MAX;
};

template<typename T>
using hpk_virt_array = arena_array<T, virtual_arena>;

using hpk_mesh_name = fixed_string<128>;

inline u64 HpkHashMeshName( const hpk_mesh_name& name )
{
    return ankerl::unordered_dense::hash<std::string_view>{}( name );
}

#endif // !__HP_TYPES_INTERNAL_H__
