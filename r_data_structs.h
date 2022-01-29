#ifdef __cplusplus

#pragma once

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


#endif

#define ALIGNAS( x ) __declspec( align( x ) )

// TODO: math lib
// TODO: alignment ?
using mat4 = DirectX::XMFLOAT4X4A;
using mat3 = DirectX::XMFLOAT3X3;
using vec4 = DirectX::XMFLOAT4;
using vec3 = DirectX::XMFLOAT3;
using vec2 = DirectX::XMFLOAT2;
using uint = u32;

#else

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_control_flow_attributes : require
#extension GL_EXT_null_initializer : require

const float invPi = 0.31830988618;
const float PI = 3.14159265359;

#define ALIGNAS( x )

#endif

// TODO: use mat4x3
struct global_data
{
	mat4	proj;
	mat4	mainView;
	mat4	activeView;
	vec3	worldPos;
	float	pad0;
	vec3	camViewDir;
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
	mat4 localToWorld;
	vec3 pos;
	float scale;
	vec4 rot;

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
	vec3 pos;
	vec3 col;
	float radius;
};
// TODO: mip-mapping
struct material_data
{
	vec3 baseColFactor;
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
	vec3	center;
	vec3	extent;

	vec3    coneAxis;
	vec3    coneApex;

	int8_t	coneX, coneY, coneZ, coneCutoff;

	uint    dataOffset;
	uint8_t vertexCount;
	uint8_t triangleCount;
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
	vec3		aabbMin;
	vec3		aabbMax;
	vec3		center;
	vec3		extent;

	mesh_lod	lods[ 4 ];
	uint		vertexCount;
	uint		vertexOffset;
	uint8_t		lodCount;
};

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
	vec2 invRes;
};

struct avg_luminance_info
{
	float minLogLum;
	float invLogLumRange;
	float dt;
};

ALIGNAS( 16 ) struct dbg_vertex
{
	vec4 pos;
	vec4 col;
};

struct imgui_vertex
{
	float x, y;
	float u, v;
	uint  rgba8Unorm;
};

// TODO: re-order these
const uint VK_GLOBAL_SLOT_STORAGE_BUFFER = 0;
const uint VK_GLOBAL_SLOT_UNIFORM_BUFFER = 1;
const uint VK_GLOBAL_SLOT_SAMPLED_IMAGE = 2;
const uint VK_GLOBAL_SLOT_SAMPLER = 3;
const uint VK_GLOBAL_SLOT_STORAGE_IMAGE = 4;

const uint VK_FRAME_DESC_SET = 0;
const uint VK_GLOBAL_DESC_SET = 1;

#ifndef __cplusplus

#ifdef BINDLESS

layout( binding = 0 ) uniform sampler samplers[];
layout( binding = 1 ) readonly buffer global { global_data g; } ssbos[];
layout( binding = 2 ) writeonly uniform coherent image2D storageImages[];
layout( binding = 3 ) uniform texture2D sampledImages[];

#endif

#ifdef GLOBAL_RESOURCES

layout( set = VK_FRAME_DESC_SET, binding = 0, std430 ) uniform global{ global_data g; } globalsCam[];

layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_BUFFER ) readonly buffer vtx_buff{ vertex data[]; } buff0[];
layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_BUFFER ) readonly buffer inst_buff{ instance_desc data[]; } buff1[];
layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_BUFFER ) readonly buffer mlet_buff{ meshlet data[]; } buff2[];
layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_BUFFER ) readonly buffer global{ global_data g; } glob[];

layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_SAMPLED_IMAGE ) uniform texture2D sampledImages[];
layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_SAMPLER ) uniform sampler samplers[];
layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_IMAGE, rgba8 ) writeonly uniform coherent image2D swapchainViews[3];
layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_IMAGE, r32f ) writeonly uniform coherent image2D depthViews[];


global_data cam = globalsCam[ 0 ].g;

#endif

#endif // !__cplusplus
