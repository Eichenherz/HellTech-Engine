#ifndef __HELL_PACK_H__
#define __HELL_PACK_H__

#include "ht_core_types.h"
#include "ht_error.h"
#include "ht_utils.h"
#include "ht_gfx_types.h"
#include "ht_math.h"

#include <span>
#include <vector>

#include "range_utils.h"

struct bit_stream
{
	std::vector<u64>	qwords;
	u64					cursorInBits = 0;  // NOTE: lsb

	const u64* begin() const { return std::data( qwords ); }
	const u64* end()   const { return std::data( qwords ) + std::size( qwords ); }

	void AppendBits( u32 inBitStream, u32 bitDepth )
	{
		HT_ASSERT( bitDepth < 64 );
		u64 bitStream = u64( inBitStream ) & ( ( 1ull << bitDepth ) - 1 );

		u64 qwBucket = cursorInBits >> 6;
		u32 bitOffset = cursorInBits & 63;
		u32 howManyBitWillFit = 64 - bitOffset;
		bool carryOver = bitDepth > howManyBitWillFit;

		if( u64 sz = std::size( qwords ); sz <= ( qwBucket + u64( carryOver ) ) )
		{
			qwords.resize( sz + 64, 0 );
		}

		qwords[ qwBucket ] |= bitStream << bitOffset;
		if( carryOver )
		{
			qwords[ qwBucket + 1 ] |= bitStream >> howManyBitWillFit;
		}

		cursorInBits += bitDepth;
	}
};

using index_t = u8;

constexpr char HELLPACK_MESH_DIR[] = "Mesh/";

// TODO: how to enforce these are dds ?
constexpr char HELLPACK_TEX_DIR[] = "Tex/";


// TODO: do we need bigger files ?
template<typename T>
struct hpk_relative_ref
{
	u64 offsetInBytes 	: 32;
	u64 sizeInBytes 	: 32;
};

template<typename T> struct hpk_view_of { using type = T; };
template<CONTIGUOUS_RANGE_T R> struct hpk_view_of<R> { using type = std::span<const std::ranges::range_value_t<R>>; };


#define HPK_MESH_ASSET( X )										  \
	X( bit_stream,                   vtxPosBitstream			) \
	X( std::vector<packed_vtx_attr>, vertexAttrs				) \
	X( std::vector<index_t>,         triIndices					) \
	X( std::vector<gpu_meshlet>,     meshlets					) \
	X( aabb_t<float3>,               aabb						) \
	X( float4,                       lodErrors					) \
	X( u32x2,                        packed16x4_lodMltCounts	)

struct hpk_mesh_view;

struct hpk_mesh_asset
{
	using view_t = hpk_mesh_view;

#define X( T, n ) T n;
	HPK_MESH_ASSET( X )
#undef X
};

struct hpk_mesh_view
{
#define X( T, n ) hpk_view_of<T>::type n;
	HPK_MESH_ASSET( X )
#undef X
};


#define HPK_LEVEL_ASSET( X )					  \
	X( std::vector<world_node>,		nodes		) //\
	//X( std::vector<material_desc>,  materials	)

struct hpk_level_view;

struct hpk_level_asset
{
	using view_t = hpk_level_view;

#define X( T, n ) T n;
	HPK_LEVEL_ASSET( X )
#undef X
};

struct hpk_level_view
{
#define X( T, n ) hpk_view_of<T>::type n;
	HPK_LEVEL_ASSET( X )
#undef X
};

struct hellpack_texture_asset
{
	std::span<u8> ddsData;
};

using hellpack_blob = std::vector<u8>;

template<typename HPK_ASSET_T>
hellpack_blob HpkSerializeAsset( const HPK_ASSET_T& a );
template<typename HPK_ASSET_T>
HPK_ASSET_T::view_t HpkDeserializeAsset( std::span<const u8> fileBlob );


#endif // !__HELL_PACK_H__
