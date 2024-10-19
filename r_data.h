#ifdef __cplusplus

#pragma once

#include "core_types.h"

#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>


struct frame_data
{
	DirectX::XMFLOAT4X4A	proj;
	DirectX::XMFLOAT4X4A	mainView;
	DirectX::XMFLOAT4X4A	activeView;
	DirectX::XMFLOAT4X4A    frustTransf;
	DirectX::XMFLOAT4X4A    activeProjView;
	DirectX::XMFLOAT4X4A    mainProjView;
	DirectX::XMFLOAT3	worldPos;
	DirectX::XMFLOAT3	camViewDir;
	float   elapsedSeconds;
	bool    freezeMainView;
	bool    dbgDraw;
};

#define ALIGNAS( x ) __declspec( align( x ) )

// TODO: math lib
// TODO: alignment ?
using float4x4 = DirectX::XMFLOAT4X4A;
using float4 = DirectX::XMFLOAT4;
using float3 = DirectX::XMFLOAT3;
using float2 = DirectX::XMFLOAT2;
using uint = u32;

#else

#define ALIGNAS( x )

#endif



// TODO: use mat4x3
struct global_data
{
	float4x4	proj;
	float4x4	mainView;
	float4x4	activeView;
	float3	worldPos;
	float	pad0;
	float3	camViewDir;
	float	pad1;
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
struct meshlet
{
	float3	center;
	float3	extent;

	float3    coneAxis;
	float3    coneApex;

	uint coneX : 8;
	uint coneY : 8;
	uint coneZ : 8;
	uint coneCutoff : 8;

	uint    dataOffset;

	uint vertexCount : 8;
	uint triangleCount : 8;
};

struct mesh_lod
{
	uint indexCount;
	uint indexOffset;
	uint meshletCount;
	uint meshletOffset;
};

struct mesh_desc
{
	float3		aabbMin;
	float3		aabbMax;
	float3		center;
	float3		extent;

	mesh_lod	lods[ 4 ];
	uint		vertexCount;
	uint		vertexOffset;
	uint		lodCount;
};

struct dispatch_indirect
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



#if __HLSL_VERSION == 2021

[[ vk::binding( 0 ) ]] SamplerState samplerTable[];
[[ vk::binding( 1 ) ]] ByteAddressBuffer bufferTable[];
[[ vk::binding( 1 ) ]] globallycoherent RWByteAddressBuffer uavBufferTable[];
[[ vk::binding( 2 ) ]] globallycoherent RWTexture2D<float> storageImageTable[];
[[ vk::binding( 3 ) ]] Texture2D textureTable[];

#endif