#pragma once

#include "ht_renderer_types.h"

#ifndef __HELLTECH_HT_HLSL_LANG_H__
#define __HELLTECH_HT_HLSL_LANG_H__

#define NUMTHREADS( x, y, z ) \
    static const uint3 GROUP_SIZE = uint3( x, y, z ); \
    [numthreads( x, y, z )]

[[vk::ext_builtin_input(/* NumWorkgroups */ 24)]]
static const uint3 WORK_GROUP_COUNT; // gl_NumWorkGroups

[[vk::ext_builtin_input(/* SubgroupId */ 40)]]
static const uint WAVE_ID_WITHIN_WG; // gl_SubgroupID

[[vk::ext_builtin_input(/* NumSubgroups */ 38)]]
static const uint WAVE_COUNT_PER_WG; // gl_NumSubgroups

#define MAX_DESCRIPTOR_COUNT 0xFFFF

// NOTE: taken from vulkanised_2023_setting_up_a_bindless_rendering_pipeline
#define ITERATE_TEXTURE_TYPES( GENERATOR, ... ) \
	GENERATOR( int, ##__VA_ARGS__ ) \
	GENERATOR( uint, ##__VA_ARGS__ ) \
	GENERATOR( float, ##__VA_ARGS__ ) \
	GENERATOR( int2, ##__VA_ARGS__ ) \
	GENERATOR( uint2, ##__VA_ARGS__ ) \
	GENERATOR( float2, ##__VA_ARGS__ ) \
	GENERATOR( int3, ##__VA_ARGS__ ) \
	GENERATOR( uint3, ##__VA_ARGS__ ) \
	GENERATOR( float3, ##__VA_ARGS__ ) \
	GENERATOR( int4, ##__VA_ARGS__ ) \
	GENERATOR( uint4, ##__VA_ARGS__ ) \
	GENERATOR( float4, ##__VA_ARGS__ )

#define TEXTURE_TYPE_SLOT_GENERATOR( native_type, texture_type, slot ) \
	[[vk::binding( slot )]] texture_type<native_type> g##texture_type##_##native_type[ MAX_DESCRIPTOR_COUNT ];

#define DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( texture_type, slot ) \
   ITERATE_TEXTURE_TYPES( TEXTURE_TYPE_SLOT_GENERATOR, texture_type, slot )

[[vk::binding( 0 )]] SamplerState samplers[ MAX_DESCRIPTOR_COUNT ];
//[[vk::binding( 1 )]] ByteAddressBuffer storageBuffers[ MAX_DESCRIPTOR_COUNT ];
[[vk::binding( 1 )]] RWByteAddressBuffer storageBuffers[ MAX_DESCRIPTOR_COUNT ];
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( RWTexture2D, 2 )
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( Texture2D, 3 )

[[vk::binding( MESHLET_BUFF_BINDING )]]	StructuredBuffer<gpu_meshlet>	megaMeshletBuffer;
[[vk::binding( VTX_BUFF_BINDING )]]		StructuredBuffer<packed_vtx>	megaVtxBuffer;
[[vk::binding( TRI_BUFF_BINDING )]]		ByteAddressBuffer               megaTriBuffer;

template<typename T>
T BufferLoad( uint buffIdx, uint idx = 0, uint offsetInBytes = 0 )
{
	return storageBuffers[ buffIdx ].template Load<T>( idx * sizeof( T ) + offsetInBytes );
}

template<typename T>
void BufferStore( uint buffIdx, T value, uint idx = 0, uint offsetInBytes = 0 )
{
	storageBuffers[ buffIdx ].Store<T>( idx * sizeof( T ) + offsetInBytes, value );
}

//template<typename T>
uint BufferAtomicAdd( uint buffIdx, uint newValue, uint idx = 0, uint offsetInBytes = 0 )
{
	uint oldValue;
	//if( is_same<T, uint>() || is_same<T, int>() )
	//{
		storageBuffers[ buffIdx ].InterlockedAdd( idx * sizeof( uint ) + offsetInBytes, newValue, oldValue );
	//}
	//else if( is_same<T, uint64_t>() || is_same<T, int64_t>() )
	//{
		//storageBuffersRW[ buffIdx ].InterlockedAdd64( ( idx + offset ) * sizeof( T ), newValue, oldValue );
	//}
	//else if( is_same<T, float>() )
	//{
		//storageBuffersRW[ buffIdx ].InterlockedAddFloat( ( idx + offset ) * sizeof( T ), newValue, oldValue );
	//}
	//else
	//{
	//	static_assert(false, "Unsupported atomic type");
	//}
	return oldValue;
}


#endif //!__HELLTECH_HT_HLSL_LANG_H__