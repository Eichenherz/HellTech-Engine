#ifndef __R_DATA_STRUCTS_H__
#define __R_DATA_STRUCTS_H__

#ifdef __cplusplus

#include "core_types.h"

// TODO: no clang ?
#ifdef __clang__
// NOTE: clang-cl on VS issue
#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

#elif _MSC_VER >= 1916

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>


struct frame_resources
{
	DirectX::XMFLOAT4X4A	proj;
	DirectX::XMFLOAT4X4A	mainView;
	DirectX::XMFLOAT4X4A	activeView;
	DirectX::XMFLOAT4X4A    frustTransf;
	DirectX::XMFLOAT4X4A    activeProjView;
	DirectX::XMFLOAT4X4A    mainProjView;
	DirectX::XMFLOAT3	    worldPos;
	DirectX::XMFLOAT3	    camViewDir;
	float                   elapsedSeconds;
	bool                    freezeMainView;
	bool                    dbgDraw;
};


#endif

#define ALIGNAS( x ) __declspec( align( x ) )

// TODO: math lib
// TODO: alignment ?
using float4x4 = DirectX::XMFLOAT4X4A;
using float3x3 = DirectX::XMFLOAT3X3;
using float4 = DirectX::XMFLOAT4;
using float3 = DirectX::XMFLOAT3;
using float2 = DirectX::XMFLOAT2;
using uint = u32;

#else

static const float invPi = 0.31830988618f;
static const float PI = 3.14159265359f;

#define ALIGNAS( x )

#endif

struct view_data
{
	float4x4	proj;
	float4x4	mainView;
	float4x4	prevView;
	float4x4	mainViewProj;
	float4x4	prevViewProj;
	float3	    worldPos;
	float		pad0;
	float3		camViewDir;
	float		pad1;
};

// TODO: compressed coords u8, u16
struct vertex
{
	float px;
	float py;
	float pz;
	float tu;
	float tv;
	// TODO: name better
	uint snorm8octTanFrame;
};

// TODO: compress data more ?
ALIGNAS( 16 ) struct instance_desc
{
	float4x4 localToWorld;
	float3 pos;
	float scale;
	float4 rot;

	uint meshIdx;
	uint mtrlIdx;
	uint transfIdx;
};
//ALIGNAS( 16 ) struct light_data
//{
//	vec3 pos;
//	float radius;
//	vec3 col;
//};
struct light_data
{
	float3 pos;
	float3 col;
	float radius;
};
// TODO: mip-mapping
struct material_data
{
	float3 baseColFactor;
	float metallicFactor;
	float roughnessFactor;
	uint baseColIdx;
	uint occRoughMetalIdx;
	uint normalMapIdx;
	uint hash;
};

// TODO: should store lod
//struct meshlet
//{
//	float3	center;
//	float3	extent;
//
//	float3    coneAxis;
//	float3    coneApex;
//
//	int8_t	coneX, coneY, coneZ, coneCutoff;
//
//	uint    dataOffset;
//	uint8_t vertexCount;
//	uint8_t triangleCount;
//};

struct mesh_lod
{
	uint indexCount;
	uint indexOffset;
	uint meshletCount;
	uint meshletOffset;
};

//struct mesh_desc
//{
//	float3		aabbMin;
//	float3		aabbMax;
//	float3		center;
//	float3		extent;
//
//	mesh_lod	lods[ 4 ];
//	uint		vertexCount;
//	uint		vertexOffset;
//	uint8_t		lodCount;
//};

struct dispatch_command
{
#if defined( __cplusplus ) && defined( __VK )
	VkDispatchIndirectCommand cmd;
#else
	uint localSizeX;
	uint localSizeY;
	uint localSizeZ;
#endif
};

// TODO: rename
struct draw_command
{
	uint	drawIdx;
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndexedIndirectCommand cmd;
#else
	uint    indexCount;
	uint    instanceCount;
	uint    firstIndex;
	uint    vertexOffset;
	uint    firstInstance;
#endif
};

struct draw_indirect
{
	uint64_t drawIdx;
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndirectCommand cmd;
#else
	uint    vertexCount;
	uint    instanceCount;
	uint    firstVertex;
	uint    firstInstance;
#endif
};

struct downsample_info
{
	uint mips;
	uint workGroupCount;
	float2 invRes;
};

struct avg_luminance_info
{
	float minLogLum;
	float invLogLumRange;
	float dt;
};

ALIGNAS( 16 ) struct dbg_vertex
{
	float4 pos;
	float4 col;
};

struct imgui_vertex
{
	float x, y;
	float u, v;
	uint  rgba8Unorm;
};

struct luminance_histogram
{
	uint finalLumSum;
	uint finalTailValsCount;
};

#ifndef __cplusplus

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
#define ITERATE_TEXTURE_TYPES(GENERATOR, ...) \
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

#define TEXURE_TYPE_SLOT_GENERATOR( native_type, texture_type, slot ) \
	[[vk::binding( slot )]] texture_type<native_type> g##texture_type##_##native_type[ MAX_DESCRIPTOR_COUNT ];

#define DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( texture_type, slot ) \
   ITERATE_TEXTURE_TYPES(TEXURE_TYPE_SLOT_GENERATOR, texture_type, slot)

[[vk::binding( 0 )]] SamplerState samplers[MAX_DESCRIPTOR_COUNT];
//[[vk::binding( 1 )]] ByteAddressBuffer storageBuffers[MAX_DESCRIPTOR_COUNT];
[[vk::binding( 1 )]] RWByteAddressBuffer storageBuffers[MAX_DESCRIPTOR_COUNT];
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( RWTexture2D, 2 ) 
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( Texture2D, 3 ) 

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

#endif // !__cplusplus


#endif // !__R_DATA_STRUCTS_H__
