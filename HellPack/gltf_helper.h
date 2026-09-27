#pragma once

#ifndef __GLTF_HELPER_H__
#define __GLTF_HELPER_H__

#include <ht_core_types.h>
#include <ht_vec_types.h>

#include <cgltf.h>
#include <ht_math.h>

#include <span>
#include <print>

#include "hp_types_internal.h"

#define HT_CGLTF_SPAN( ptr ) std::span{ ( ptr ), ( ptr ## _count ) }

HT_FORCEINLINE packed_trs GltfComposePackedTRS( packed_trs parent, packed_trs child )
{
	return {
	    .t = parent.t + ht::vec_rot( child.t * parent.s, parent.r ),
	    .r = ht::quat_mul( child.r, parent.r ),
	    .s = parent.s * child.s
	};
}

inline packed_trs GetTrsFromNode( const cgltf_node& node )
{
	if( node.has_matrix )
	{
		using namespace DirectX;

		// NOTE: GLTF stores matrices column-major: mIn[col*4 + row]. DirectXMath uses row-major convention,
		// so M_dx = M_gltf^T — each GLTF column becomes a DirectXMath row.
		XMMATRIX m = DX_XMLoadFloat4x4A( *( const float4x4* ) node.matrix );
		XMVECTOR xmT, xmR, xmS;
		if( !XMMatrixDecompose( &xmS, &xmR, &xmT, m ) )
		{
			std::println( stdout, "WARNING: XMMatrixDecompose failed.\n" );
			return IDENTITY_TRS;
		}
		return { .t = DX_XMStoreFloat3( xmT ), .r = DX_XMStoreFloat4( xmR ), .s = DX_XMStoreFloat3( xmS ) };
	}

	const float* t = node.translation;
	const float* r = node.rotation;
	const float* s = node.scale;
	return {
		.t = node.has_translation	? float3{ t[ 0 ], t[ 1 ], t[ 2 ] }	: IDENTITY_TRS.t,
		.r = node.has_rotation		? quat4{ r[ 0 ], r[ 1 ], r[ 2 ], r[ 3 ] }	: IDENTITY_TRS.r,
		.s = node.has_scale			? float3{ s[ 0 ], s[ 1 ], s[ 2 ] }	: IDENTITY_TRS.s
	};
}

inline packed_trs GltfGetTRSFromExtGpuInst( const cgltf_node& node, u64 instIdx )
{
	packed_trs trs = IDENTITY_TRS;
	for( const cgltf_attribute& a : std::span{ node.mesh_gpu_instancing.attributes, node.mesh_gpu_instancing.attributes_count } )
	{
		if( !std::strcmp( a.name, "TRANSLATION" ) ) cgltf_accessor_read_float( a.data, instIdx, &trs.t.x, 3 );
		if( !std::strcmp( a.name, "ROTATION" ) ) cgltf_accessor_read_float( a.data, instIdx, ( float* ) &trs.r, 4 );
		if( !std::strcmp( a.name, "SCALE" ) ) cgltf_accessor_read_float( a.data, instIdx, &trs.s.x, 3 );
	}
	return trs;
}

inline bool CgltfIsNodeHidden( const cgltf_node& node )
{
	return std::ranges::any_of( std::span{ node.extensions, node.extensions_count },
	[]( const cgltf_extension& ext )
	{
		return !std::strcmp( ext.name, "KHR_node_visibility" ) && std::strstr( ext.data, "false" );
	} );
}

constexpr raw_mesh_topology_t CgltfPrimitiveTypeToTopology( cgltf_primitive_type gltfPrimType )
{
	switch( gltfPrimType )
	{
	case cgltf_primitive_type_triangles:	return raw_mesh_topology_t::MESH;
	case cgltf_primitive_type_points:		return raw_mesh_topology_t::POINTS;
	default:								break;
	}
	HT_ASSERT( 0 && "Unsupported gltf primitive type" );
	return raw_mesh_topology_t::MESH;
}

constexpr alpha_mode CgltfAlphaModeToEnum( cgltf_alpha_mode gltfAlphaMode )
{
	if( cgltf_alpha_mode_mask == gltfAlphaMode )  return ALPHA_MODE_MASK;
	if( cgltf_alpha_mode_blend == gltfAlphaMode ) return ALPHA_MODE_BLEND;
	return ALPHA_MODE_OPAQUE;
}
constexpr sampler_filter_mode_flags CgltfFilterToFlags( cgltf_filter_type gltfFilter )
{
	switch( gltfFilter )
	{
	case cgltf_filter_type_nearest:                	return FILTER_NEAREST;
	case cgltf_filter_type_linear:                 	return FILTER_LINEAR;
	case cgltf_filter_type_nearest_mipmap_nearest: 	return FILTER_NEAREST_MIPMAP_NEAREST;
	case cgltf_filter_type_linear_mipmap_nearest:  	return FILTER_LINEAR_MIPMAP_NEAREST;
	case cgltf_filter_type_nearest_mipmap_linear:  	return FILTER_NEAREST_MIPMAP_LINEAR;
	case cgltf_filter_type_linear_mipmap_linear:   	return FILTER_LINEAR_MIPMAP_LINEAR;
	default:										break;
	}
	return FILTER_LINEAR;
}
constexpr sampler_wrap_mode_flags CgltfWrapToFlags( cgltf_wrap_mode gltfWrap )
{
	switch( gltfWrap )
	{
	case cgltf_wrap_mode_clamp_to_edge:   return WRAP_CLAMP_TO_EDGE;
	case cgltf_wrap_mode_mirrored_repeat: return WRAP_MIRRORED_REPEAT;
	case cgltf_wrap_mode_repeat:          return WRAP_REPEAT;
	default:                              break;
	}

	return WRAP_CLAMP_TO_EDGE;
}

inline fixed_string<256> CgltfGetBufferFilePath( std::string_view basePath, const cgltf_accessor* pAccessor )
{
    if( !pAccessor || !pAccessor->buffer_view->buffer->uri ) return {};
    return { "{}\\{}", basePath, pAccessor->buffer_view->buffer->uri };
}

inline aabb_t<float3a> CgltfGetPosStreamBounds( const cgltf_primitive& prim )
{
    const cgltf_accessor* pAccessor = cgltf_find_accessor( &prim, cgltf_attribute_type_position, 0 );
    // NOTE: gltf mandates this attr be present together with its bounds
    HT_ASSERT( pAccessor && pAccessor->has_min && pAccessor->has_max );

    return {
        .min = { pAccessor->min[ 0 ], pAccessor->min[ 1 ], pAccessor->min[ 2 ] },
        .max = { pAccessor->max[ 0 ], pAccessor->max[ 1 ], pAccessor->max[ 2 ] }
    };
}

#endif //!__GLTF_HELPER_H__