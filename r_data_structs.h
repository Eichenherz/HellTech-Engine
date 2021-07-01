#ifdef __cplusplus

#pragma once

#include "core_types.h"

// TODO: remove

#undef __clang__
#define _XM_NO_XMVECTOR_OVERLOADS_
#include <DirectXMath.h>
#define __clang__

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

const float invPi = 0.31830988618;
const float PI = 3.14159265359;

#define ALIGNAS( x )

#endif

struct global_data
{
	mat4	proj;
	mat4	mainView;
	mat4	activeView;
	vec3	camPos;
	float	pad0;
	vec3	camViewDir;
	float	pad1;
	//vec4	viewMove;
	//vec4	viewQuat;
};

// NOTE: bda == buffer device address
struct global_bdas
{
	uint64_t vtxAddr;
	uint64_t idxAddr;
	uint64_t meshDescAddr;
	uint64_t lightsDescAddr;
	//uint64_t meshletsOffset;
	//uint64_t meshletsVtxOffset;
	//uint64_t meshletsIdxOffset;
	uint64_t mtrlsAddr;
	uint64_t instDescAddr;
};
// TODO: compressed coords u8, u16
struct vertex
{
	float px;
	float py;
	float pz;
	float tu;
	float tv;
	//uint mi;
	uint8_t snorm8octNx;
	uint8_t snorm8octNy;
	uint8_t snorm8tanAngle;
	uint8_t pad;
};

// TODO: compress data more ?
ALIGNAS( 16 ) struct instance_desc
{
	vec3 pos;
	float scale;
	vec4 rot;

	uint meshIdx;
	uint mtrlIdx;
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

struct meshlet
{
	vec3	center;
	float	radius;

	int8_t	coneX, coneY, coneZ, coneCutoff;

	uint	vtxBufOffset;
	uint	triBufOffset;
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

// TODO: remove mtrlIdx
struct mesh
{
	vec3		center;
	float		radius;

	uint		vertexCount;
	uint		vertexOffset;

	//uint		materialIndex;

	mesh_lod	lods[ 4 ];
	uint		lodCount;
};

struct mesh_desc
{
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
	uint	drawIdx;
#if defined( __cplusplus ) && defined( __VK )
	VkDrawIndirectCommand cmd;
#else
	uint    vertexCount;
	uint    instanceCount;
	uint    firstVertex;
	uint    firstInstance;
#endif
};

// TODO: align
struct cull_info
{	
	vec4	planes[ 4 ];

	float	frustum[ 4 ];

	float	zNear;
	float	drawDistance;
	float	projWidth;
	float	projHeight;

	float	pyramidWidthPixels;
	float	pyramidHeightPixels;

	uint	drawCallsCount;
};

struct cam_frustum
{
	vec4	planes[ 4 ];

	float	frustum[ 4 ];

	float	zNear;
	float	drawDistance;
	float	projWidth;
	float	projHeight;
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

#ifndef __cplusplus

#ifdef GLOBAL_RESOURCES

layout( set = 1, binding = 1, std430 ) uniform global{ global_data g; } globalsCam[];
layout( set = 1, binding = 1, std430 ) uniform glob_bdas{ global_bdas bdas; } globalsBdas[];
layout( set = 1, binding = 2 ) uniform texture2D sampledImages[];
layout( set = 1, binding = 3 ) uniform sampler samplers[];

global_data cam = globalsCam[ 0 ].g;
global_bdas bdas = globalsBdas[ 1 ].bdas;

#endif

#endif // !__cplusplus
