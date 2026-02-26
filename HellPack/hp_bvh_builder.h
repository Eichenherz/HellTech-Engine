#ifndef __HP_BVH_BUILDER_H__
#define __HP_BVH_BUILDER_H__

#include "core_types.h"
#include "hp_math.h"
#include "hp_mesh.h"
#include "hp_bvh.h"

#include <bvh/v2/bvh.h>
#include <bvh/v2/vec.h>
#include <bvh/v2/node.h>
#include <bvh/v2/thread_pool.h>
#include <bvh/v2/sweep_sah_builder.h>

#include <optional>

inline bvh2_node_ref32   MakeBvh2NodeRef( u32 nodeIndex )
{
	assert( nodeIndex <= BVH2_NODE_MASK );
	return nodeIndex & BVH2_NODE_MASK;
}
inline bvh2_node_ref32   MakeBvh2LeafRef( u32 basePrimIds, u32 count )
{
	assert( count >= 1 && count <= MAX_LEAF_PRIM_COUNT );

	u32 c = ( count - 1u ) & ( ( 1u << BVH2_LEAF_COUNT_BITS ) - 1u );

	return BVH2_LEAF_BIT | ( basePrimIds & BVH2_LEAF_BASE_MASK ) | ( c << BVH2_LEAF_COUNT_SHIFT );
}

using scalar  = float;
using vec3    = bvh::v2::Vec<scalar, 3>;
using bbox_t  = bvh::v2::BBox<scalar, 3>;
using node_t  = bvh::v2::Node<scalar, 3>;

inline vec3 FromFloat3( float3 in )
{
	return { in.x, in.y, in.z };
}
inline float3 ToFloat3( vec3 in )
{
	return { in[ 0 ], in[ 1 ], in[ 2 ] };
}
inline aabb_t<float3> GetAabb( bbox_t in )
{
	return { .min = ToFloat3( in.min ), .max = ToFloat3( in.max ) };
}

auto TriangleAabbView( 
	const std::ranges::random_access_range auto& pos,
	const std::ranges::random_access_range auto& indices 
) {
	auto TriangleToAabb = [&] ( u32 tri )
	{
		u32 base = 3u * tri;

		float3 p0 = pos[ indices[ base + 0 ] ];
		float3 p1 = pos[ indices[ base + 1 ] ];
		float3 p2 = pos[ indices[ base + 2 ] ];

		return aabb_t<float3>{
			.min = fminf( p0, fminf( p1, p2 ) ),
			.max = fmaxf( p0, fmaxf( p1, p2 ) )
		};
	};

	u32 triCount = std::size( indices ) / 3u;
	return std::views::iota( 0u, triCount ) | std::views::transform( TriangleToAabb );
}

struct bvh_output
{
	std::vector<gpu_bvh2_node> gpuNodes;
	std::vector<u64> primitiveIndices;
	aabb_t<float3> topLevelAabb;
};

// NOTE: not thread safe, use one instance per thread !
struct bvh_builder
{
	std::vector<bbox_t> boundingBoxes;
	std::vector<vec3> centers;

	inline bvh::v2::Bvh<node_t> BuildSweptSahFromAabbs( const std::ranges::forward_range auto& aabbs )
	{
		using namespace bvh::v2;

		const u64 aabbCount = std::size( aabbs );

		boundingBoxes.resize( 0 );
		centers.resize( 0 );
		boundingBoxes.reserve( aabbCount );
		centers.reserve( aabbCount );

		for( const aabb_t<float3>& aabb : aabbs )
		{
			bbox_t bb = { FromFloat3( aabb.min ), FromFloat3( aabb.max ) };
			boundingBoxes.push_back( bb );
			centers.push_back( bb.get_center() );
		}

		SweepSahBuilder<node_t>::Config config = { 
			.min_leaf_size = MIN_LEAF_PRIM_COUNT, .max_leaf_size = MAX_LEAF_PRIM_COUNT };
		return SweepSahBuilder<node_t>::build( boundingBoxes, centers, config );
	}

	static bvh2_node_ref32 EmitChildDfs( const bvh::v2::Bvh<node_t>& bvh, u32 madChildId, std::vector<gpu_bvh2_node>& gpuNodes )
	{
		const auto& c = bvh.nodes[ madChildId ];

		if( c.index.is_leaf() )
		{
			return MakeBvh2LeafRef( c.index.first_id(), c.index.prim_count() );
		}

		u32 first = c.index.first_id();
		u32 c0Mad = first + 0u;
		u32 c1Mad = first + 1u;

		const auto& c0 = bvh.nodes[ c0Mad ];
		const auto& c1 = bvh.nodes[ c1Mad ];

		aabb_t<float3> c0Aabb = GetAabb( c0.get_bbox() );
		aabb_t<float3> c1Aabb = GetAabb( c1.get_bbox() );

		// NOTE: preorder allocation => subtree-contiguous DFS layout
		u32 outId = ( u32 ) std::size( gpuNodes );
		gpuNodes.emplace_back();

		gpu_bvh2_node& o = gpuNodes[ outId ];
		o.min = { c0Aabb.min, c1Aabb.min };
		o.max = { c0Aabb.max, c1Aabb.max };

		// NOTE: recurse left then right => DFS layout
		o.childIdx[ 0 ] = EmitChildDfs( bvh, c0Mad, gpuNodes );
		o.childIdx[ 1 ] = EmitChildDfs( bvh, c1Mad, gpuNodes );

		return MakeBvh2NodeRef( outId );
	}

	inline auto LinearizeNonDegenerateBvhDfsPacked_Recursive( const bvh::v2::Bvh<node_t>& bvh )
	{
		std::vector<gpu_bvh2_node> gpuNodes;
		gpuNodes.reserve( std::size( bvh.nodes ) );

		const auto& root = bvh.nodes[ 0 ];
		// NOTE: Degenerate BVH must be processed separately 
		HP_ASSERT( !root.index.is_leaf() );

		u32 first = root.index.first_id();
		u32 c0Mad = first + 0u;
		u32 c1Mad = first + 1u;

		const auto& c0 = bvh.nodes[ c0Mad ];
		const auto& c1 = bvh.nodes[ c1Mad ];

		aabb_t<float3> c0Aabb = GetAabb( c0.get_bbox() );
		aabb_t<float3> c1Aabb = GetAabb( c1.get_bbox() );

		gpuNodes.emplace_back();

		gpu_bvh2_node& rootGpu = gpuNodes[ 0 ];
		rootGpu.min = { c0Aabb.min, c1Aabb.min };
		rootGpu.max = { c0Aabb.max, c1Aabb.max };

		rootGpu.childIdx[ 0 ] = EmitChildDfs( bvh, c0Mad, gpuNodes );
		rootGpu.childIdx[ 1 ] = EmitChildDfs( bvh, c1Mad, gpuNodes );

		return gpuNodes;
	}

	inline bvh_output BuildBvhOverPrimitives( const std::ranges::forward_range auto& aabbs )
	{
		bvh::v2::Bvh<node_t> bvh = BuildSweptSahFromAabbs( aabbs );
		std::vector<gpu_bvh2_node> gpuBvh = LinearizeNonDegenerateBvhDfsPacked_Recursive( bvh );

		return {
			.gpuNodes = std::move( gpuBvh ),
			.primitiveIndices = std::move( bvh.prim_ids ),
			.topLevelAabb = GetAabb( bvh.nodes[ 0 ].get_bbox() )
		};
	}
};

#endif // !__HP_BVH_BUILDER_H__
