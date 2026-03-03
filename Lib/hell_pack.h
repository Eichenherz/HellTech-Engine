#ifndef __HELL_PACK_H__
#define __HELL_PACK_H__

#include "core_types.h"
#include "ht_error.h"

#include "ht_utils.h"

#include "ht_gfx_types.h"

#include <span>

#include "range_utils.h"

constexpr u32 HELLPACK_VERSION = 1;
constexpr u64 HELLPACK_MAGIC =
( u64( 'H' ) )       |
( u64( 'E' ) << 8 )  |
( u64( 'L' ) << 16 ) |
( u64( 'L' ) << 24 ) |
( u64( 'P' ) << 32 ) |
( u64( 'A' ) << 40 ) |
( u64( 'C' ) << 48 ) |
( u64( 'K' ) << 56 );

constexpr char HELLPACK_MESH_DIR[] = "Mesh/";

// TODO: how to enforce these are dds ?
constexpr char HELLPACK_TEX_DIR[] = "Tex/";

enum class hellpack_entry_t : u32
{
	LEVEL = 0,
	MESH,
	TEXTURE,
	COUNT
};

struct hellpack_data_ref
{
	u64 offsetInBytes : 32;
	u64 sizeInBytes : 32;
};

struct hellpack_file_header
{
	u64					magic;
	u64					fileSizeBytes;
	u32					version;
	u32   		        entriesCount; // TODO: enforce an explicit ordering on the entires, rn they're as in the struct
	hellpack_entry_t    type;
};

struct hellpack_level
{
	typed_view<world_node>    nodes;
	typed_view<material_desc> materials;
};

using index_t = u8;
struct hellpack_mesh_asset
{
	typed_view<packed_vtx>	vertices;
	typed_view<index_t>		triangles;
	typed_view<meshlet>		meshlets;
	float3					aabbMin;
	float3					aabbMax;
};

struct hellpack_texture_asset
{
	byte_view ddsData;
};

template<typename HPK_T>
HPK_T HpkReadBinaryBlob( std::span<const u8> blob )
{
	HT_ASSERT( std::size( blob ) >= sizeof( hellpack_file_header ) );

	const u8* base = std::data( blob );
	const u64 sizeInBytes = std::size( blob );

	const hellpack_file_header& h = ( const hellpack_file_header& ) ( *base );

	HT_ASSERT( HELLPACK_MAGIC == h.magic );
	HT_ASSERT( HELLPACK_VERSION == h.version );
	HT_ASSERT( h.fileSizeBytes == sizeInBytes );
	HT_ASSERT( h.entriesCount > 0 );

	u64 entryTableOffsetInBytes = FwdAlign( sizeof( h ), alignof( hellpack_data_ref ) );
	std::span<const hellpack_data_ref> entryTable = { ( const hellpack_data_ref* ) ( base + entryTableOffsetInBytes ), h.entriesCount };

	if constexpr( std::is_same_v<HPK_T, hellpack_level> )
	{
		HT_ASSERT( hellpack_entry_t::LEVEL == h.type );
		HT_ASSERT( 2 == h.entriesCount );

		return hellpack_level{
			.nodes = { 
				( const world_node* ) ( base + entryTable[ 0 ].offsetInBytes ), 
				( u32 ) ( entryTable[ 0 ].sizeInBytes / sizeof( world_node ) ) 
		    },
			.materials = { 
				( const material_desc* ) ( base + entryTable[ 1 ].offsetInBytes ), 
				( u32 ) ( entryTable[ 1 ].sizeInBytes / sizeof( material_desc ) ) 
		    }
		};
	}
	else if constexpr( std::is_same_v<HPK_T, hellpack_mesh_asset> )
	{
		HT_ASSERT( hellpack_entry_t::MESH == h.type );
		HT_ASSERT( 4 == h.entriesCount );

		return hellpack_mesh_asset{
			.vertices = { 
				( const packed_vtx* ) ( base + entryTable[ 0 ].offsetInBytes ), 
				( u32 ) ( entryTable[ 0 ].sizeInBytes / sizeof( packed_vtx ) ) 
			},
			.triangles = { 
				( const u8* ) ( base + entryTable[ 1 ].offsetInBytes ), 
				( u32 ) entryTable[ 1 ].sizeInBytes
			},
			.meshlets = { 
				( const meshlet* ) ( base + entryTable[ 2 ].offsetInBytes ), 
				( u32 ) ( entryTable[ 2 ].sizeInBytes / sizeof( meshlet ) ) 
			},
			.aabbMin = *( const float3* ) ( base + entryTable[ 3 ].offsetInBytes ),
			.aabbMax = *( const float3* ) ( base + entryTable[ 3 ].offsetInBytes + sizeof( float3 ) )
		};
	}
	else if constexpr( std::is_same_v<HPK_T, hellpack_texture_asset> )
	{
		//NOTE: hellpack_texture_asset is used directly as a .dds
		return byte_view{ blob };
	}
	else static_assert( false, "Unsupported HPK_T" );
}

#endif // !__HELL_PACK_H__
