#pragma once

#ifndef __HELL_PACK_H__
#define __HELL_PACK_H__

#include <ht_core_types.h>
#include <ht_gfx_types.h>
#include <ht_hash.h>
#include <ht_macros.h>

#include <array>
#include <span>

constexpr u64   GRID_SECTOR_DIM_IN_METERS   = 256;
constexpr float GRID_INV_SCALE              = 1.0f / float( GRID_SECTOR_DIM_IN_METERS );
constexpr u64   ZSTD_COMPRESSION_LEVEL      = 19;
/*
 *---------------------------------------------------------------------------------------
 *                                  HELL_PACK LAYOUT
 *---------------------------------------------------------------------------------------
 *  0th byte
 *  [ DATA ]....[ { MESH_DESC }{ LODs } ][ { SECTOR_DESC }{ NODE_PAYLOAD } ][ FOOTER ]
 *                      A                                       |
 *                      |_________________HASH__________________|
 */
constexpr char  HPK_MAGIC[]         = { 'H', 'E', 'L', 'L', 'P', 'A', 'C', 'K' } ;

HT_DEF_STRUCT_W_HASH( hpk_file_footer,
    u64     magic;
    u64     fileFormatVersion;
    u64     contentVersion;

    u64     firstSectorsOffsetInBytes;
    u64     sectorsCount;

    u64     firstMeshDescOffsetInBytes;
    u64     meshDescCount;

    u64     firstLodDescOffsetInBytes;
    u64     lodDescCount;
);

HT_DEF_STRUCT_W_HASH( hpk_lod_desc,
    u64 fileOffsetInBytes     = ~0ull;
    u64 storedSzInBytes       = 0;
    u64 posSzInBytes     : 32 = 0;
    u64 normalsSzInBytes : 32 = 0;
    u64 idxBuffSzInBytes : 32 = 0;
    u64 mltsSzInBytes    : 32 = 0;
);

inline bool HpkIsLodCompressed( const hpk_lod_desc& lod )
{
    u64 contentSzInBytes = lod.posSzInBytes + lod.normalsSzInBytes + lod.idxBuffSzInBytes + lod.mltsSzInBytes;
    return lod.storedSzInBytes != contentSzInBytes;
}

HT_DEF_STRUCT_W_HASH( hpk_mesh_desc,
    u64     hashed           = 0;
    float3  aabbMin          = {};
    float3  aabbMax          = {};
    float4  lodErrs          = {};
    u64     firstLod : 56    = 0;
    u64     lodCount : 8     = 0;
);

constexpr u64 NODE_LOD_BIN_COUNT = 256;

HT_DEF_STRUCT_W_HASH( hpk_sector_desc,
    using node_lods = std::array<u32, NODE_LOD_BIN_COUNT>; // TODO: change the size or write dense

    u64     firstNodeOffsetInBytes;
    u64     nodeCount;
    i32x2   idx;
    alignas( 8 )
    node_lods log2LodNodeOffsets; // NOTE: bc we're lazy we'll store the whole sparse set as inclusive scan
);

HT_DEF_STRUCT_W_HASH( hpk_file_view,
    template<typename T>
    using view_t = std::span<const T>;

    view_t<hpk_sector_desc> sectors             = {};
    view_t<hpk_mesh_desc>   meshes              = {};
    view_t<hpk_lod_desc>    lods                = {};
);

constexpr u64 HPK_FORMAT_VERSION  = hpk_file_footer_LAYOUT_HASH
    ^ hpk_lod_desc_LAYOUT_HASH
    ^ hpk_mesh_desc_LAYOUT_HASH
    ^ hpk_sector_desc_LAYOUT_HASH;
constexpr u64 HPK_CONTENT_VERSION = hpk_file_view_LAYOUT_HASH;

inline hpk_file_view HpkGetFileView( std::span<const u8> mem )
{
    hpk_file_footer hpkFooter = *( ( const hpk_file_footer* ) std::end( mem )._Myptr - 1 );
    HT_ASSERT( std::bit_cast<u64>( HPK_MAGIC ) == hpkFooter.magic );
    HT_ASSERT( HPK_FORMAT_VERSION == hpkFooter.fileFormatVersion );
    HT_ASSERT( HPK_CONTENT_VERSION == hpkFooter.contentVersion );

    return {
        .sectors    = {
            ( const hpk_sector_desc* ) ( std::data( mem ) + hpkFooter.firstSectorsOffsetInBytes ), hpkFooter.sectorsCount
        },
        .meshes     = {
            ( const hpk_mesh_desc* ) ( std::data( mem ) + hpkFooter.firstMeshDescOffsetInBytes ), hpkFooter.meshDescCount
        },
        .lods       = {
            ( const hpk_lod_desc* ) ( std::data( mem ) + hpkFooter.firstLodDescOffsetInBytes ), hpkFooter.lodDescCount
        }
    };
}

// TODO: use our own
#include <ankerl/unordered_dense.h>

struct hpk_mesh_desc_key
{
    using is_transparent = void;
    using is_avalanching = void;

    static u64 Key( u64 meshHash ) { return meshHash; }
    static u64 Key( const hpk_mesh_desc& d ) { return d.hashed; }

    u64  operator()( const auto& v ) const { return Key( v ); }
    bool operator()( const auto& a, const auto& b ) const { return Key( a ) == Key( b ); }
};

struct hpk_sector_desc_key
{
    using is_transparent = void;
    using is_avalanching = void;

    static u64 Key( i32x2 idx ) { return std::bit_cast<u64>( idx ); }
    static u64 Key( const hpk_sector_desc& d ) { return Key( d.idx ); }

    u64  operator()( const auto& v ) const { return ankerl::unordered_dense::hash<u64>{}( Key( v ) ); }
    bool operator()( const auto& a, const auto& b ) const { return Key( a ) == Key( b ); }
};

#endif // !__HELL_PACK_H__
