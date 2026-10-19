#pragma once

#ifndef __GLTF_HELPER_H__
#define __GLTF_HELPER_H__

#include <ht_core_types.h>
#include <ht_vec_types.h>

#include <cgltf.h>
#include <range_utils.h>

#include <span>
#include <print>

#include "hp_types_internal.h"
#include "hpk_gltf_types.h"

// NOTE: many helpers are inspired by `cgltf`
HT_FORCEINLINE const hpk_gltf_accessor* HpkGetGltfAccessor( const hpk_gltf_data& data, u32 accessorIdx )
{
    return ( ~0u == accessorIdx ) ? nullptr : &data.accessors[ accessorIdx ];
}

constexpr u64 HpkGltfNumComponents( hpk_gltf_accessor_type accType )
{
    switch( accType )
    {
    case hpk_gltf_accessor_type::SCALAR:    return 1;
    case hpk_gltf_accessor_type::VEC2:      return 2;
    case hpk_gltf_accessor_type::VEC3:      return 3;
    case hpk_gltf_accessor_type::VEC4:      return 4;
    case hpk_gltf_accessor_type::MAT2:      return 4;
    case hpk_gltf_accessor_type::MAT3:      return 9;
    case hpk_gltf_accessor_type::MAT4:      return 16;
    }
    return ~0ull;
}

constexpr u64 HpkGltfComponentSz( hpk_gltf_component_type compType )
{
    switch( compType )
    {
    case hpk_gltf_component_type::BYTE:
    case hpk_gltf_component_type::UNSIGNED_BYTE:    return 1;
    //
    case hpk_gltf_component_type::SHORT:
    case hpk_gltf_component_type::UNSIGNED_SHORT:   return 2;
    //
    case hpk_gltf_component_type::UNSIGNED_INT:
    case hpk_gltf_component_type::FLOAT:            return 4;
    //
    case hpk_gltf_component_type::NONE:             return 0;
    }
    return ~0ull;
}

constexpr u64 HpkGltfGetAccessorSz( hpk_gltf_accessor_type accType, hpk_gltf_component_type compType )
{
    u64 compSz = HpkGltfComponentSz( compType );
    if( ( hpk_gltf_accessor_type::MAT2 == accType ) && ( 1 == compSz ) ) return 8 * compSz;
    if( ( hpk_gltf_accessor_type::MAT3 == accType ) && ( ( 1 == compSz ) || ( 2 == compSz ) ) ) return 12 * compSz;
    return compSz * HpkGltfNumComponents( accType );
}

constexpr const u8* HpkGltfBufferViewData( const hpk_gtlf_buffer_view& view, std::span<const u8> bin )
{
    return ( std::size( bin ) ) ? &bin[ 0 ] + view.byteOffset : nullptr;
}

// NOTE: can't be replaced by a span, this handles interleaved data as well
template<TRIVIAL_T T>
struct hpk_strided_view
{
    const u8*   data        = nullptr;
    u64         elemCount   = 0;
    u64         strideBytes = 0;

    hpk_strided_view() = default;
    hpk_strided_view( const hpk_gltf_data& data, const hpk_gltf_accessor& accessor )
    {
        HT_ASSERT( ~0u != accessor.bufferView );
        const hpk_gtlf_buffer_view& view = data.bufferViews[ accessor.bufferView ];

        u64 elemSize = HpkGltfNumComponents( accessor.type ) * HpkGltfComponentSz( accessor.componentType );
        u64 strideInBytes = ( 0 == view.byteStride ) ? elemSize : view.byteStride;
        HT_ASSERT( ( strideInBytes >= elemSize ) && ( sizeof( T ) == elemSize ) );

        u64 baseOffset = view.byteOffset + accessor.byteOffset;
        HT_ASSERT( baseOffset < std::size( data.binChunk ) );

        this->data          = &data.binChunk[ 0 ] + baseOffset;
        this->elemCount     = accessor.count;
        this->strideBytes   = strideInBytes;
    }

    constexpr u64 size() const { return elemCount; }

    struct iterator
    {
        // NOTE: don't change these names !
        using iterator_concept = std::random_access_iterator_tag;
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;

        const u8*   ptr         = nullptr;
        u64         strideBytes = 0;

        T operator*() const
        {
            T out;
            std::memcpy( &out, ptr, sizeof( T ) );
            return out;
        }

        T operator[]( difference_type n ) const { return *( *this + n ); }

        iterator& operator++() { ptr += strideBytes; return *this; }
        iterator& operator--() { ptr -= strideBytes; return *this; }
        iterator operator++( int ) { iterator tmp = *this; ++( *this ); return tmp; }
        iterator operator--( int ) { iterator tmp = *this; --( *this ); return tmp; }

        iterator& operator+=( difference_type n ) { ptr += n * ( difference_type ) strideBytes; return *this; }
        iterator& operator-=( difference_type n ) { ptr -= n * ( difference_type ) strideBytes; return *this; }

        friend iterator operator+( iterator it, difference_type n ) { return it += n; }
        friend iterator operator+( difference_type n, iterator it ) { return it += n; }
        friend iterator operator-( iterator it, difference_type n ) { return it -= n; }
        friend difference_type operator-( const iterator& a, const iterator& b )
        {
            return ( a.ptr - b.ptr ) / ( difference_type ) a.strideBytes;
        }

        bool operator==( const iterator& rhs ) const { return ptr == rhs.ptr; }
        auto operator<=>( const iterator& rhs ) const { return ptr <=> rhs.ptr; }
    };

    static_assert( std::random_access_iterator<iterator> );

    iterator begin() const { return { data, strideBytes }; }
    iterator end() const { return { data + elemCount * strideBytes, strideBytes }; }

    T operator[]( u64 i ) const
    {
        HT_ASSERT( i < elemCount );
        return begin()[ ( std::ptrdiff_t ) i ];
    }
};

inline borrowed_array<u32> HpkGltfUnpackIndices(
    const hpk_gltf_data&        data,
    const hpk_gltf_accessor&    accessor,
    virtual_arena&              arena
) {
    HT_ASSERT( hpk_gltf_accessor_type::SCALAR == accessor.type );

    switch( accessor.componentType )
    {
        case hpk_gltf_component_type::UNSIGNED_BYTE: return { arena, std::from_range,
            hpk_strided_view<u8>{ data, accessor } | std::views::transform( HtCastTo<u32> )
        };
        case hpk_gltf_component_type::UNSIGNED_SHORT: return { arena, std::from_range,
            hpk_strided_view<u16>{ data, accessor } | std::views::transform( HtCastTo<u32> )
        };
        case hpk_gltf_component_type::UNSIGNED_INT: return { arena, std::from_range,
            hpk_strided_view<u32>{ data, accessor }
        };
    }

    HT_ASSERT( 0 && "Wrong stream type" );
    return {};
}

constexpr packed_trs GetTrsFromNode( const hpk_gltf_node& node )
{
	if( !ByteEqual( std::array<float, 16>{}, node.matrix ) )
	{
		using namespace DirectX;

		// NOTE: GLTF stores matrices column-major: mIn[col*4 + row]. DirectXMath uses row-major convention,
		// so M_dx = M_gltf^T — each GLTF column becomes a DirectXMath row.
		XMMATRIX m = DX_XMLoadFloat4x4A( *( const float4x4* ) &node.matrix[ 0 ] );
		XMVECTOR xmT, xmR, xmS;
		if( !XMMatrixDecompose( &xmS, &xmR, &xmT, m ) )
		{
			std::println( stdout, "WARNING: XMMatrixDecompose failed.\n" );
			return IDENTITY_TRS;
		}
		return { .t = DX_XMStoreFloat3( xmT ), .r = DX_XMStoreFloat4( xmR ), .s = DX_XMStoreFloat3( xmS ) };
	}

	return {
		.t = *( const float3* ) &node.translation[ 0 ],
		.r = *( const float4* ) &node.rotation[ 0 ],
		.s = *( const float3* ) &node.scale[ 0 ]
	};
}

inline u64 HpkGltfGetExtGpuInstCount( const hpk_gltf_data& data, const hpk_gltf_node& node )
{
	const hpk_gltf_ext_mesh_gpu_instancing_attributes& attrs = node.extensions.EXT_mesh_gpu_instancing.attributes;
	u32 instAcc = ( ~0u != attrs.TRANSLATION ) ? attrs.TRANSLATION : ( ~0u != attrs.ROTATION ) ? attrs.ROTATION : attrs.SCALE;
	return ( ~0u != instAcc ) ? data.accessors[ instAcc ].count : 1;
}

inline packed_trs HpkGltfGetTRSFromExtGpuInst( const hpk_gltf_data& data, const hpk_gltf_node& node, u64 instIdx )
{
	const hpk_gltf_ext_mesh_gpu_instancing_attributes& attrs = node.extensions.EXT_mesh_gpu_instancing.attributes;
	return {
		.t = ( ~0u != attrs.TRANSLATION ) ?
	        hpk_strided_view<float3>{ data, data.accessors[ attrs.TRANSLATION ] }[ instIdx ] : IDENTITY_TRS.t,
		.r = ( ~0u != attrs.ROTATION ) ?
	        hpk_strided_view<float4>{ data, data.accessors[ attrs.ROTATION ] }[ instIdx ] : IDENTITY_TRS.r,
		.s = ( ~0u != attrs.SCALE ) ?
	        hpk_strided_view<float3>{ data, data.accessors[ attrs.SCALE ] }[ instIdx ] : IDENTITY_TRS.s
	};
}

constexpr raw_mesh_topology_t HpkGltfPrimTopology( hpk_gltf_prim_mode gltfPrimType )
{
	switch( gltfPrimType )
	{
	case hpk_gltf_prim_mode::TRIANGLES:	return raw_mesh_topology_t::MESH;
	case hpk_gltf_prim_mode::POINTS:	return raw_mesh_topology_t::POINTS;
	}
	HT_ASSERT( 0 && "Unsupported gltf primitive type" );
	return raw_mesh_topology_t::INVALID;
}

#endif //!__GLTF_HELPER_H__