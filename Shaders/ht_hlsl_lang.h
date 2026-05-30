#pragma once

#include "ht_renderer_types.h"

namespace spv
{
    static const u32 CapabilityVulkanMemoryModel            = 5345;
    static const u32 CapabilityVulkanMemoryModelDeviceScope = 5346;
    static const u32 OpImageRead                            = 98;
    static const u32 OpImageWrite                           = 99;
	static const u32 MakeTexelAvailable 					= 0x100;
	static const u32 MakeTexelVisible   					= 0x200;
	static const u32 NonPrivateTexel    					= 0x400;
	// OPERATION SCOPE are defined in vk::
	// GLSL stuff
	static const u32 NumWorkGroups							= 24; // gl_NumWorkGroups
	static const u32 SubgroupId								= 40; // gl_SubgroupID
	static const u32 NumSubgroups							= 38; // gl_NumSubgroups
	// KHR ext
	static const u32 ComputeDerivativeGroupLinearKHR		= 5350;
}


#ifndef __HELLTECH_HT_HLSL_LANG_H__
#define __HELLTECH_HT_HLSL_LANG_H__

#define NOINTERP nointerpolation

#define NUMTHREADS( x, y, z ) \
    static const u32x3 GROUP_SIZE = u32x3( x, y, z ); \
    [numthreads( x, y, z )]

[[vk::ext_builtin_input( spv::NumWorkGroups )]]
static const u32x3 HT_WORKGROUP_COUNT;

[[vk::ext_builtin_input( spv::SubgroupId )]]
static const u32 HT_WAVE_ID_WITHIN_WG;

[[vk::ext_builtin_input( spv::NumSubgroups )]]
static const u32 HT_WAVE_COUNT_PER_WG;

[[vk::ext_instruction( spv::OpImageRead )]]
float4 ImageReadCoherent(
	RWTexture2D<float>		imgRef,
	i32x2					coord,
	[[vk::ext_literal]] u32	imageOperands,
	u32						scope );

[[vk::ext_instruction( spv::OpImageWrite )]]
void ImageWriteCoherent(
	RWTexture2D<float>		imgRef,
	i32x2					coord,
	float4					texel,
	[[vk::ext_literal]] u32	imageOperands,
	u32						scope );

static const u32 SPV_COHERENT_READ_OPERANDS		= spv::NonPrivateTexel | spv::MakeTexelVisible;
static const u32 SPV_COHERENT_WRITE_OPERANDS	= spv::NonPrivateTexel | spv::MakeTexelAvailable;

#define MAX_DESCRIPTOR_COUNT 0xFFFF

// NOTE: taken from vulkanised_2023_setting_up_a_bindless_rendering_pipeline
#define ITERATE_TEXTURE_TYPES( GENERATOR, ... ) \
	GENERATOR( i32, 	##__VA_ARGS__ ) 		\
	GENERATOR( u32, 	##__VA_ARGS__ ) 		\
	GENERATOR( float, 	##__VA_ARGS__ ) 		\
	GENERATOR( i32x2, 	##__VA_ARGS__ ) 		\
	GENERATOR( u32x2,	##__VA_ARGS__ ) 		\
	GENERATOR( float2,	##__VA_ARGS__ )			\
	GENERATOR( i32x3,	##__VA_ARGS__ ) 		\
	GENERATOR( u32x3,	##__VA_ARGS__ ) 		\
	GENERATOR( float3,	##__VA_ARGS__ )			\
	GENERATOR( i32x4,	##__VA_ARGS__ ) 		\
	GENERATOR( u32x4,	##__VA_ARGS__ ) 		\
	GENERATOR( float4,	##__VA_ARGS__ )

#define TEXTURE_TYPE_SLOT_GENERATOR( native_type, texture_type, slot ) \
	[[vk::binding( slot )]] texture_type<native_type> g##texture_type##_##native_type[ MAX_DESCRIPTOR_COUNT ];

#define DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( texture_type, slot ) \
   ITERATE_TEXTURE_TYPES( TEXTURE_TYPE_SLOT_GENERATOR, texture_type, slot )

[[vk::binding( 0 )]] SamplerState samplers[ MAX_DESCRIPTOR_COUNT ];

[[vk::binding( 1 )]] RWByteAddressBuffer storageBuffers[ MAX_DESCRIPTOR_COUNT ];

DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( RWTexture2D, 2 )
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( Texture2D, 3 ) // NOTE: in Vk these are "sampled" images, while RW == "storage"


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

u32 BufferAtomicOr( u32 buffIdx, u32 newValue, u32 idx = 0, u32 offsetInBytes = 0 )
{
	u32 oldValue;
	storageBuffers[ buffIdx ].InterlockedOr( idx * sizeof( u32 ) + offsetInBytes, newValue, oldValue );
	return oldValue;
}

static const global_data gGlobData = BufferLoad<global_data>( GLOB_DATA_BINDING_SLOT );

template<typename T>
struct device_ptr
{
	u64 addr;

	T operator[]( u64 idx ) { return vk::BufferPointer<T>( addr + idx * sizeof( T ) ).Get(); }

	void Store( u64 idx, T val ) { vk::BufferPointer<T>( addr + idx * sizeof( T ) ).Get() = val; }
};

float HTLoadCoherentImageFloat( u32 imgIdx, i32x2 pix )
{
	return ImageReadCoherent( gRWTexture2D_float[ imgIdx ], pix, SPV_COHERENT_READ_OPERANDS,
		vk::QueueFamilyScope ).x;
}

void HTStoreCoherentImageFloat( u32 imgIdx, i32x2 pix, float val )
{
	ImageWriteCoherent( gRWTexture2D_float[ imgIdx ], pix, float4( val, 0.0f, 0.0f, 0.0f ),
		SPV_COHERENT_WRITE_OPERANDS, vk::QueueFamilyScope );
}

float4 HTQuadBroadcast( float v )
{
	float v0 = v;
	float v1 = QuadReadAcrossX( v );
	float v2 = QuadReadAcrossY( v );
	float v3 = QuadReadAcrossDiagonal( v );

	return float4( v0, v1, v2, v3 );
}

bool HTIsQuadLeader( u32x2 quadID )
{
	return all( 0 == ( quadID & 1 ) );
}

u32 HTWaveReserveGlobalSlot( in bool cond, in u32 counterDescIdx )
{
	u32 lanesTrue = WaveActiveCountBits( cond );
	u32 offsetForWave = 0;
	if( lanesTrue > 0 )
	{
		if( WaveIsFirstLane() )
		{
			offsetForWave = BufferAtomicAdd( counterDescIdx, lanesTrue );
		}
	}

	u32 laneOffset = WavePrefixCountBits( cond );
	return WaveReadLaneFirst( offsetForWave ) + laneOffset;
}

#endif //!__HELLTECH_HT_HLSL_LANG_H__