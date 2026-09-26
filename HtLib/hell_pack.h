#pragma once

#ifndef __HELL_PACK_H__
#define __HELL_PACK_H__

#include <ht_core_types.h>
#include <ht_gfx_types.h>
#include <ht_hash.h>
#include <ht_macros.h>

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

    u64     firstNodeOffsetInBytes;
    u64     nodeCount;

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

HT_DEF_STRUCT_W_HASH( hpk_sector_desc,
    u64     firstNode : 32;
    u64     nodeCount : 32;
    i32x2   idx;
);

constexpr u64 HPK_FORMAT_VERSION  = hpk_file_footer_LAYOUT_HASH
    ^ hpk_lod_desc_LAYOUT_HASH
    ^ hpk_mesh_desc_LAYOUT_HASH
    ^ hpk_sector_desc_LAYOUT_HASH;
constexpr u64 HPK_CONTENT_VERSION = world_node_LAYOUT_HASH;


#endif // !__HELL_PACK_H__
