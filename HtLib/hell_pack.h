#pragma once

#ifndef HELLTECH_HELL_PACK_H
#define HELLTECH_HELL_PACK_H

#include <ht_core_types.h>
#include <ht_gfx_types.h>
#include <ht_hash.h>
#include <ht_macros.h>


#include <range_utils.h>

constexpr u64   GRID_SECTOR_DIM_IN_METERS   = 256;
constexpr u64   PLAYER_LOAD_RADIUS_METERS   = 3 * GRID_SECTOR_DIM_IN_METERS;
constexpr float GRID_INV_SCALE              = 1.0f / float( GRID_SECTOR_DIM_IN_METERS );
constexpr u64   ZSTD_COMPRESSION_LEVEL      = 19;
/*
 *---------------------------------------------------------------------------------------
 *                                  HELL_PACK LAYOUT
 *---------------------------------------------------------------------------------------
 *  0th byte
 *  [ { All LOD0 }{ All LOD1 }{ All LOD2 }{ All LOD3 }...{ LOD_DESC }{ MESH_DESC }{ NODES }{ SECTOR_DESC } FOOTER ]
 *                                                                          A          |
 *                                                                          |___HASH___|
 */
// NOTE: .hpk format is an unaligned byte dump, so in order to properly read it must be ( mem )copied first
constexpr char  HPK_MAGIC[ sizeof( u64 ) ] = { 'H', 'E', 'L', 'L', 'P', 'A', 'C', 'K' } ;

HT_DEF_STRUCT_W_HASH( hpk_file_header,
    u64     magic;
    u64     fileFormatVersion;
    u64     contentVersion;

    u64     firstSectorsOffsetInBytes;
    u64     sectorsCount;

    u64     firstNodeOffsetInBytes;
    u64     nodeCount;

    u64     firstMeshDescOffsetInBytes;
    u64     meshDescCount;

    u64     firstLodDescOffsetInBytes;
    u64     lodDescCount;
);

HT_DEF_STRUCT_W_HASH( hpk_lod_desc,
    u64 fileOffsetInBytes     = 0; // TODO: theoretically we can't have zero offsets for these; would mean empty file
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
    u64         hashed          = 0;
    pkd_fp32x3  aabbMin         = {};
    pkd_fp32x3  aabbMax         = {};
    pkd_fp32x4  lodErrs         = {};
    u64         firstLod : 56   = 0;
    u64         lodCount : 8    = 0;
    u64         padding         = 0;
);

HT_DEF_STRUCT_W_HASH( hpk_sector_desc,
    u64     firstNodeIdx    = 0;
    u64     nodeCount       = 0;
    i16x2   secID           = {};
);

constexpr u64 HPK_FORMAT_VERSION    = hpk_file_header_LAYOUT_HASH;
constexpr u64 HPK_CONTENT_VERSION   = hpk_lod_desc_LAYOUT_HASH ^ hpk_mesh_desc_LAYOUT_HASH ^ hpk_sector_desc_LAYOUT_HASH;

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

    static u64 Key( i16x2 id ) { return std::bit_cast<u32>( id ); }
    static u64 Key( const hpk_sector_desc& d ) { return Key( d.secID.xy ); }

    u64  operator()( const auto& v ) const { return ankerl::unordered_dense::hash<u64>{}( Key( v ) ); }
    bool operator()( const auto& a, const auto& b ) const { return Key( a ) == Key( b ); }
};

#endif // !HELLTECH_HELL_PACK_H
