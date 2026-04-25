#pragma once

#include "ht_renderer_types.h"

#ifndef __HELLTECH_HT_HLSL_LANG_H__
#define __HELLTECH_HT_HLSL_LANG_H__

#define NUMTHREADS( x, y, z ) \
    static const u32x3 GROUP_SIZE = uint3( x, y, z ); \
    [numthreads( x, y, z )]

[[vk::ext_builtin_input(/* NumWorkgroups */ 24)]]
static const u32x3 WORK_GROUP_COUNT; // gl_NumWorkGroups

[[vk::ext_builtin_input(/* SubgroupId */ 40)]]
static const u32 WAVE_ID_WITHIN_WG; // gl_SubgroupID

[[vk::ext_builtin_input(/* NumSubgroups */ 38)]]
static const u32 WAVE_COUNT_PER_WG; // gl_NumSubgroups

#define MAX_DESCRIPTOR_COUNT 0xFFFF

// NOTE: taken from vulkanised_2023_setting_up_a_bindless_rendering_pipeline
#define ITERATE_TEXTURE_TYPES( GENERATOR, ... ) \
	GENERATOR( i32, ##__VA_ARGS__ ) \
	GENERATOR( u32, ##__VA_ARGS__ ) \
	GENERATOR( float, ##__VA_ARGS__ ) \
	GENERATOR( i32x2, ##__VA_ARGS__ ) \
	GENERATOR( u32x2, ##__VA_ARGS__ ) \
	GENERATOR( float2, ##__VA_ARGS__ ) \
	GENERATOR( i32x3, ##__VA_ARGS__ ) \
	GENERATOR( u32x3, ##__VA_ARGS__ ) \
	GENERATOR( float3, ##__VA_ARGS__ ) \
	GENERATOR( i32x4, ##__VA_ARGS__ ) \
	GENERATOR( u32x4, ##__VA_ARGS__ ) \
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


template<typename T>
T BufferLoad( u32 buffIdx, u32 idx = 0, u32 offsetInBytes = 0 )
{
	return storageBuffers[ buffIdx ].template Load<T>( idx * sizeof( T ) + offsetInBytes );
}

template<typename T>
void BufferStore( u32 buffIdx, T value, u32 idx = 0, u32 offsetInBytes = 0 )
{
	storageBuffers[ buffIdx ].Store<T>( idx * sizeof( T ) + offsetInBytes, value );
}

//template<typename T>
u32 BufferAtomicAdd( u32 buffIdx, u32 newValue, u32 idx = 0, u32 offsetInBytes = 0 )
{
	u32 oldValue;
	//if( is_same<T, uint>() || is_same<T, int>() )
	//{
		storageBuffers[ buffIdx ].InterlockedAdd( idx * sizeof( u32 ) + offsetInBytes, newValue, oldValue );
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

static const global_data gGlobData = BufferLoad<global_data>( GLOB_DATA_BINDING_SLOT );

template<typename T>
struct device_addr
{
	u64 addr;

	T operator[]( u64 idx )
	{
		return vk::RawBufferLoad<T>( addr + idx * sizeof( T ) );
	}
};

u32x3 FetchTriangleFromMegaBuff( u64 globalIdxInBytes )
{
	device_addr<u32> triBuff = { gGlobData.triAddr };
	u64 lo = triBuff[ globalIdxInBytes >> 2 ];
	u64 hi = triBuff[ ( globalIdxInBytes >> 2 ) + 1 ];
	u64 shift = ( globalIdxInBytes & 3 ) * 8;
	u64 raw = ( ( hi << 32 ) | lo ) >> shift;
	return unpack_u8u32( u32( raw & 0xFFFFFF ) ).xyz;
}

#endif //!__HELLTECH_HT_HLSL_LANG_H__