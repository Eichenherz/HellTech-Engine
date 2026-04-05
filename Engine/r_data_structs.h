#ifndef _R_DATA_STRUCTS_H_
#define _R_DATA_STRUCTS_H_

#ifdef __cplusplus

#include "ht_core_types.h"

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


struct view_data;




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

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require
#extension GL_KHR_shader_subgroup_shuffle: require

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

struct view_data
{
	mat4	proj;
	mat4	mainView;
	mat4	prevView;
	mat4	mainViewProj;
	mat4	prevViewProj;
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
struct meshlet_w_cone
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

struct compacted_draw_args
{
	uint nodeIdx;
	uint materialIdx;
	uint meshletIdx;
};

#if defined( __cplusplus ) && defined( __VK )
using draw_command = VkDrawIndexedIndirectCommand;
#else
// TODO: rename
struct draw_command
{
	uint    indexCount;
	uint    instanceCount;
	uint    firstIndex;
	int    vertexOffset;
	uint    firstInstance;

};
#endif

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

// TODO: uv to u16 or smth
struct imgui_vertex
{
	float x, y;
	float u, v;
	uint  rgba8Unorm;
};

struct dbg_draw_args
{
	mat4 transf;
	uint color;
	uint padding;
};

// TODO: re-order these
const uint VK_GLOBAL_SLOT_STORAGE_BUFFER = 0;
const uint VK_GLOBAL_SLOT_UNIFORM_BUFFER = 1;
const uint VK_GLOBAL_SLOT_SAMPLED_IMAGE = 2;
const uint VK_GLOBAL_SLOT_SAMPLER = 3;
const uint VK_GLOBAL_SLOT_STORAGE_IMAGE = 4;

#ifndef __cplusplus

#ifdef BINDLESS

layout( binding = 0 ) uniform sampler samplers[];
layout( binding = 1 ) buffer views { view_data views[]; } ssbos[];
layout( binding = 2 ) writeonly uniform coherent image2D storageImages[];
layout( binding = 3 ) uniform texture2D sampledImages[];

#endif

#endif // !__cplusplus


#endif // !_R_DATA_STRUCTS_H_
