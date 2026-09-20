#pragma once

#ifndef __GLTF_HELPER_H__
#define __GLTF_HELPER_H__

#include <ht_core_types.h>
#include <ht_vec_types.h>

#include <cgltf.h>
#include <ht_math.h>

#include <span>

#include "hp_types_internal.h"

// NOTE: GLTF stores matrices column-major: mIn[col*4 + row]. DirectXMath uses row-major convention,
// so M_dx = M_gltf^T — each GLTF column becomes a DirectXMath row.
inline DirectX::XMMATRIX GetMatrix( std::span<const cgltf_float> mIn )
{
	using namespace DirectX;
	XMMATRIX m = {};
	m.r[ 0 ] = XMVectorSet( mIn[ 0 ],  mIn[ 1 ],  mIn[ 2 ],  mIn[ 3 ]  );
	m.r[ 1 ] = XMVectorSet( mIn[ 4 ],  mIn[ 5 ],  mIn[ 6 ],  mIn[ 7 ]  );
	m.r[ 2 ] = XMVectorSet( mIn[ 8 ],  mIn[ 9 ],  mIn[ 10 ], mIn[ 11 ] );
	m.r[ 3 ] = XMVectorSet( mIn[ 12 ], mIn[ 13 ], mIn[ 14 ], mIn[ 15 ] );
	return m;
}

inline packed_trs XM_CALLCONV GltfComposePackedTRS( packed_trs parent, packed_trs child )
{
	using namespace DirectX;

	XMVECTOR parentT    = DX_XMLoadFloat3( parent.t );
	XMVECTOR parentR    = DX_XMLoadFloat4( parent.r );
	XMVECTOR parentS    = DX_XMLoadFloat3( parent.s );

	XMVECTOR childT     = DX_XMLoadFloat3( child.t );
	XMVECTOR childR     = DX_XMLoadFloat4( child.r );
	XMVECTOR childS     = DX_XMLoadFloat3( child.s );

    XMVECTOR xmTf       = XMVector3Rotate( XMVectorMultiply( childT, parentS ), parentR );

	return {
	    .t = DX_XMStoreFloat3( XMVectorAdd( parentT, xmTf ) ),
	    .r = DX_XMStoreFloat4( XMQuaternionMultiply( childR, parentR ) ),
	    .s = DX_XMStoreFloat3( XMVectorMultiply( parentS, childS ) )
	};
}

inline packed_trs GetTrsFromNode( const cgltf_node& node )
{
	using namespace DirectX;

	XMVECTOR xmT = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
	XMVECTOR xmR = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
	XMVECTOR xmS = XMVectorSet( 1.0f, 1.0f, 1.0f, 0.0f );
	if( node.has_matrix )
	{
		XMMATRIX m = GetMatrix( node.matrix );
		if( !XMMatrixDecompose( &xmS, &xmR, &xmT, m ) )
		{
			std::cout << "WARNING: XMMatrixDecompose failed.\n";
			xmT = XMVectorSet( 0.0f, 0.0f, 0.0f, 0.0f );
			xmR = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
			xmS = XMVectorSet( 1.0f, 1.0f, 1.0f, 0.0f );
		}
	}
	else
	{
		if( node.has_translation )
		{
			xmT = XMVectorSet( node.translation[ 0 ], node.translation[ 1 ], node.translation[ 2 ], 0.0f );
		}
		if( node.has_rotation )
		{
			xmR = XMVectorSet(
				node.rotation[ 0 ],
				node.rotation[ 1 ],
				node.rotation[ 2 ],
				node.rotation[ 3 ]
			);
		}
		if( node.has_scale )
		{
			xmS = XMVectorSet( node.scale[ 0 ], node.scale[ 1 ], node.scale[ 2 ], 0.0f );
		}
	}

	return {  .t = DX_XMStoreFloat3( xmT ),  .r = DX_XMStoreFloat4( xmR ),  .s = DX_XMStoreFloat3( xmS ) };
}

inline packed_trs GltfGetTRSFromExtGpuInst( const cgltf_node& node, u64 instIdx )
{
	packed_trs trs = IDENTITY_TRS;
	for( const cgltf_attribute& a : std::span{ node.mesh_gpu_instancing.attributes, node.mesh_gpu_instancing.attributes_count } )
	{
		if( !std::strcmp( a.name, "TRANSLATION" ) ) cgltf_accessor_read_float( a.data, instIdx, &trs.t.x, 3 );
		if( !std::strcmp( a.name, "ROTATION" ) )    cgltf_accessor_read_float( a.data, instIdx, &trs.r.x, 4 );
		if( !std::strcmp( a.name, "SCALE" ) )       cgltf_accessor_read_float( a.data, instIdx, &trs.s.x, 3 );
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

inline aabb_t<float3> CgltfGetPosStreamBounds( const cgltf_primitive& prim )
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