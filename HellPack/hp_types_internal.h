#pragma once

#ifndef __HP_TYPES_INTERNAL_H__
#define __HP_TYPES_INTERNAL_H__

#include <ht_core_types.h>

#include "hp_serialization.h"

#include <ankerl/unordered_dense.h>
#include <vector>
#include <string>

struct raw_mesh
{
	std::string         name;
	std::vector<float3> pos;
	std::vector<float3> normals;
	std::vector<float4> tans;
	std::vector<float2> uvs;
	std::vector<u32>    indices;
	u32                 materialIdx;
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
	std::string name;

	float4		baseColFactor;
	float		metallicFactor;
	float		roughnessFactor;
	float		alphaCutoff;
	float3		emissiveFactor;

	u16 		baseColorIdx;
	u16 		metallicRoughnessIdx;
	u16 		normalIdx;
	u16 		occlusionIdx;
	u16 		emissiveIdx;
	u16 		samplerIdx;

	alpha_mode	alphaMode;
};

struct bit_stream
{
	std::vector<u64>	qwords;
	u64					cursorInBits = 0;  // NOTE: lsb

	void AppendBits( u32 inVal, u32 bitLen )
	{
		HT_ASSERT( bitLen < 64 );
		u64 val = u64( inVal ) & ( ( 1ull << bitLen ) - 1 );

		u64 qwBucket = cursorInBits >> 6;
		u32 bitOffset = cursorInBits & 63;
		u32 howManyBitWillFit = 64 - bitOffset;
		bool carryOver = bitLen > howManyBitWillFit;

		if( u64 sz = std::size( qwords ); sz <= ( qwBucket + u64( carryOver ) ) )
		{
			qwords.resize( sz + 64, 0 );
		}

		qwords[ qwBucket ] |= val << bitOffset;
		if( carryOver )
		{
			qwords[ qwBucket + 1 ] |= val >> howManyBitWillFit;
		}

		cursorInBits += bitLen;
	}

	hellpack_serializable_buffer GetSerializableBuffer() const
	{
		HT_ASSERT( std::data( qwords ) && cursorInBits );
		return { MakeTypedView<u8>( ( const u8* ) std::data( qwords ), cursorInBits / 8 ) };
	}
};

struct mesh_asset
{
	bit_stream						vtxPosBitstream;
	std::vector<packed_vtx_attr>	vtxAttrs;
	std::vector<u8>					triIndices;
	std::vector<gpu_meshlet>		meshlets;
	std::array<float3, 2>			aabb; // NOTE: helps with serialization { min, max }
};

struct packed_trs;

struct raw_node
{
	packed_trs	toWorld;
	i32			meshIdx;
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

struct raw_node_eq
{
	bool operator()( const raw_node& a, const raw_node& b ) const
	{
		const packed_trs& at = a.toWorld;
		const packed_trs& bt = b.toWorld;
		return ( a.meshIdx == b.meshIdx )
			&& ( at.t.x == bt.t.x ) && ( at.t.y == bt.t.y ) && ( at.t.z == bt.t.z )
			&& ( at.r.x == bt.r.x ) && ( at.r.y == bt.r.y ) && ( at.r.z == bt.r.z ) && ( at.r.w == bt.r.w )
			&& ( at.s.x == bt.s.x ) && ( at.s.y == bt.s.y ) && ( at.s.z == bt.s.z );
	}
};

struct gpu_meshlet_eq
{
	bool operator()( const gpu_meshlet& a, const gpu_meshlet& b ) const
	{
		return ( a.aabbMin == b.aabbMin ) && ( a.aabbMax == b.aabbMax )
			&& ( a.vtxPosOffsetBits == b.vtxPosOffsetBits )
			&& ( a.triOffset == b.triOffset )
			&& ( a.vtxCount == b.vtxCount )
			&& ( a.triCount == b.triCount )
			&& ( a.posBitDepth == b.posBitDepth );
	}
};

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
	return ( a.v0 == b.v0 ) && ( a.v1 == b.v1 ) &&  ( a.v2 == b.v2 );
}

#endif // !__HP_TYPES_INTERNAL_H__
