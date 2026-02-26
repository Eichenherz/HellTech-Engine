#ifndef __HP_BVH_H__
#define __HP_BVH_H__

#include "core_types.h"

#include <array>

struct float2;
struct float3;
struct float4;

// NOTE: alias bc this can refer both to a node or a leaf
using bvh2_node_ref32 = u32;

constexpr u32 BVH_INVALID_REF = 0xffffffffu;
constexpr u32 BVH2_LEAF_BIT   = 0x80000000u;
constexpr u32 BVH2_NODE_MASK  = 0x7fffffffu;

// NOTE: example-leaf: base = bits0..28 (index into prim_ids[]) countMinus1 = bits29..30 (0..3 => count 1..4)
constexpr u32 MIN_LEAF_PRIM_COUNT = 1;
constexpr u32 MAX_LEAF_PRIM_COUNT = 4;
static_assert( MAX_LEAF_PRIM_COUNT >= 1 && MAX_LEAF_PRIM_COUNT <= 8 );

constexpr u32 BVH2_LEAF_COUNT_BITS = std::bit_width( MAX_LEAF_PRIM_COUNT );
constexpr u32 BVH2_LEAF_COUNT_SHIFT = 31u - BVH2_LEAF_COUNT_BITS;
constexpr u32 BVH2_LEAF_BASE_MASK = ( 1u << BVH2_LEAF_COUNT_SHIFT ) - 1u;
constexpr u32 BVH2_LEAF_COUNT_MASK = ( ( 1u << BVH2_LEAF_COUNT_BITS ) - 1u ) << BVH2_LEAF_COUNT_SHIFT;

inline bool  Bvh2IsLeaf( bvh2_node_ref32 ref ) { return ( ref & BVH2_LEAF_BIT ) != 0u; }
inline u32	 Bvh2NodeIdx( bvh2_node_ref32 ref ) { return ( ref & BVH2_NODE_MASK ); }
inline u32	 Bvh2LeafBase( bvh2_node_ref32 ref ) { return ( ref & BVH2_LEAF_BASE_MASK ); }
inline u32	 Bvh2LeafCount( bvh2_node_ref32 ref ) 
{ 
	return ( ( ref & BVH2_LEAF_COUNT_MASK ) >> BVH2_LEAF_COUNT_SHIFT ) + 1u; 
}

inline bool  Bvh2RefIsInvalid( bvh2_node_ref32 ref )
{
	return BVH_INVALID_REF == ref;
}

struct alignas( 64 ) gpu_bvh2_node
{
	std::array<float3, 2> min;
	std::array<float3, 2> max;
	std::array<bvh2_node_ref32, 2> childIdx; 
};

#endif // !__HP_BVH_H__