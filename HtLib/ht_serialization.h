#ifndef HPK_ASSET_T
#ifndef __HT_SERIALIZATION_H__
#define __HT_SERIALIZATION_H__

#include <ht_fixed_vector.h>
#include <vector>
#include <span>

#include "hell_pack.h"

#include "range_utils.h"

constexpr u32 HELLPACK_VERSION = 2;
constexpr u64 HELLPACK_MAGIC =
    ( u64( 'H' ) )       |
    ( u64( 'E' ) << 8 )  |
    ( u64( 'L' ) << 16 ) |
    ( u64( 'L' ) << 24 ) |
    ( u64( 'P' ) << 32 ) |
    ( u64( 'A' ) << 40 ) |
    ( u64( 'C' ) << 48 ) |
    ( u64( 'K' ) << 56 );

enum class hpk_entry_t : u32
{
    LEVEL = 0,
    MESH,
    TEXTURE,
    COUNT
};

struct hellpack_file_header
{
    u64			magic;
    u64			fileSizeBytes;
    u32			version;
    u32			layoutHash;
    hpk_entry_t type;
};

template<typename T> struct hpk_rel_view_of { using type = T; };
template<CONTIGUOUS_RANGE_T R> struct hpk_rel_view_of<R> { using type = hpk_relative_ref<std::ranges::range_value_t<R>>; };

template<typename T> constexpr bool hpk_is_rel_ref = false;
template<typename T> constexpr bool hpk_is_rel_ref<hpk_relative_ref<T>> = true;

template<typename T> concept HPK_REL_REF_T = hpk_is_rel_ref<std::remove_cvref_t<T>>;

struct hpk_mesh_relative_view
{
#define X( T, n ) hpk_rel_view_of<T>::type n;
    HPK_MESH_ASSET( X )
#undef X
};

struct hpk_level_relative_view
{
#define X( T, n ) hpk_rel_view_of<T>::type n;
    HPK_LEVEL_ASSET( X )
#undef X
};

template<typename ASSET_T> struct hpk_asset_traits;

template<>
struct hpk_asset_traits<hpk_mesh_asset>
{
    using view_t        = hpk_mesh_view;
    using rel_view_t    = hpk_mesh_relative_view;

    static constexpr hpk_entry_t ENTRY_TYPE  = hpk_entry_t::MESH;
    static constexpr u32         LAYOUT_HASH = MurmurHash(
    #define X( T, n ) #T "|" #n "|"
        HPK_MESH_ASSET( X )
    #undef X
        );
};

template<>
struct hpk_asset_traits<hpk_level_asset>
{
    using view_t        = hpk_level_view;
    using rel_view_t    = hpk_level_relative_view;

    static constexpr hpk_entry_t ENTRY_TYPE  = hpk_entry_t::LEVEL;
    static constexpr u32         LAYOUT_HASH = MurmurHash(
    #define X( T, n ) #T "|" #n "|"
        HPK_LEVEL_ASSET( X )
    #undef X
        );
};

template<typename T>
inline std::span<const T> HpkGetAbsSpan( hpk_relative_ref<T> ref, const u8* base )
{
    HT_ASSERT( 0 == ( ( u64( base ) + ref.offsetInBytes ) % alignof( T ) ) );
    HT_ASSERT( 0 == ( ref.sizeInBytes % sizeof( T ) ) );

    return { ( const T* ) ( base + ref.offsetInBytes ), ref.sizeInBytes / sizeof( T ) };
}

struct hpk_placed_blob
{
    std::span<const u8> bytesView;
    u64                 dstOffset = 0;
};


#define HPK_X_BLOB_COUNT( T, n )  + ( CONTIGUOUS_RANGE_T<T> ? 1 : 0 )
#define HPK_X_PLACE( T, n )      .n = Place( a.n ),
#define HPK_X_LOAD( T, n )       .n = Load( relView.n ),

// NOTE: impls per asset by quoted include this file; BS bc of C++'s stupid templates and lack of reflection
#define HPK_ASSET_T			hpk_mesh_asset
#define HPK_STRUCT_MACRO	HPK_MESH_ASSET
#include "ht_serialization.h"

#define HPK_ASSET_T			hpk_level_asset
#define HPK_STRUCT_MACRO	HPK_LEVEL_ASSET
#include "ht_serialization.h"

#endif // !__HT_SERIALIZATION_H__
#else // HPK_ASSET_T

template<>
inline hellpack_blob HpkSerializeAsset<HPK_ASSET_T>( const HPK_ASSET_T& a )
{
    using hpk_traits = hpk_asset_traits<HPK_ASSET_T>;
    using hpk_rel_view = hpk_asset_traits<HPK_ASSET_T>::rel_view_t;

    constexpr u64 viewOffset = FwdAlignPot( sizeof( hellpack_file_header ), alignof( hpk_rel_view ) );
    constexpr u32 blobCount  = 0 HPK_STRUCT_MACRO( HPK_X_BLOB_COUNT );

    fixed_vector<hpk_placed_blob, blobCount> blobs;
    u64 cursor = viewOffset + sizeof( hpk_rel_view );

    auto Place = [ & ]<typename T>( const T& src ) -> typename hpk_rel_view_of<T>::type
    {
        if constexpr( CONTIGUOUS_RANGE_T<T> )
        {
            using elem_t = std::ranges::range_value_t<T>;

            u64 sz = std::ranges::size( src ) * sizeof( elem_t );

            HT_ASSERT( 0 != sz );
            cursor = FwdAlignPot( cursor, alignof( elem_t ) );
            HT_ASSERT( ( cursor + sz ) < u64( u32( -1 ) ) );

            blobs.emplace_back( std::span<const u8>{ ( const u8* ) std::ranges::data( src ), sz }, cursor );

            hpk_relative_ref<elem_t> ref = { cursor, sz };
            cursor += sz;

            return ref;
        }
        else return src;
    };

    // NOTE: braced init lists are sequenced left to right, so cursor advances in field order
    const hpk_rel_view relView = { HPK_STRUCT_MACRO( HPK_X_PLACE ) };
    HT_ASSERT( blobCount == std::size( blobs ) );

    hellpack_file_header header = {
        .magic         = HELLPACK_MAGIC,
        .fileSizeBytes = cursor,
        .version       = HELLPACK_VERSION,
        .layoutHash    = hpk_traits::LAYOUT_HASH,
        .type          = hpk_traits::ENTRY_TYPE
    };

    hellpack_blob blob( cursor, 0 );
    std::memcpy( std::data( blob ),              &header, sizeof( header ) );
    std::memcpy( std::data( blob ) + viewOffset, &relView, sizeof( relView ) );

    for( const hpk_placed_blob& b : blobs )
    {
        HT_ASSERT( ( b.dstOffset + std::size( b.bytesView ) ) <= std::size( blob ) );
        std::memcpy( std::data( blob ) + b.dstOffset, std::data( b.bytesView ), std::size( b.bytesView ) );
    }

    return blob;
}

template<>
inline HPK_ASSET_T::view_t HpkDeserializeAsset<HPK_ASSET_T>( std::span<const u8> fileBlob )
{
    using hpk_traits = hpk_asset_traits<HPK_ASSET_T>;
    using hpk_rel_view = hpk_asset_traits<HPK_ASSET_T>::rel_view_t;
    using hpk_view_t = hpk_asset_traits<HPK_ASSET_T>::view_t;

    constexpr u64 viewOffset = FwdAlignPot( sizeof( hellpack_file_header ), alignof( hpk_rel_view ) );

    const u8*   base = std::data( fileBlob );
    const auto& h    = *( const hellpack_file_header* ) base;

    HT_ASSERT( HELLPACK_MAGIC		    == h.magic );
    HT_ASSERT( HELLPACK_VERSION		    == h.version );
    HT_ASSERT( hpk_traits::LAYOUT_HASH	== h.layoutHash);
    HT_ASSERT( hpk_traits::ENTRY_TYPE	== h.type );
    HT_ASSERT( h.fileSizeBytes <= std::size( fileBlob ) );

    hpk_rel_view relView;
    std::memcpy( &relView, base + viewOffset, sizeof( relView ) );

    auto Load = [ & ]<typename T>( const T& src )
    {
        if constexpr( HPK_REL_REF_T<T> )
        {
            HT_ASSERT( ( src.offsetInBytes + src.sizeInBytes ) <= h.fileSizeBytes );
            return HpkGetAbsSpan( src, base );
        }
        else return src;
    };

    return hpk_view_t{ HPK_STRUCT_MACRO( HPK_X_LOAD ) };
}

#undef HPK_ASSET_T
#undef HPK_STRUCT_MACRO

#endif // !HPK_ASSET_T
