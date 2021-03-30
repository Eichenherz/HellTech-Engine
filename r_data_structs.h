#ifdef __cplusplus

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

#define ALIGNAS( x )

#endif

// TODO: trim , change, etc ?
struct vertex_stage_data
{
	mat3 tbn;
	vec3 fragWorldPos;
	uint mtlIdx;
};

struct global_data
{
	mat4 proj;
	mat4 view;
	vec3 camPos;
	float pad;
	vec3 camViewDir;
};

struct geometry_buffer_info
{
	uint64_t addr;
	uint64_t posOffset;
	uint64_t normOffset;
	uint64_t uvsOffset;
	uint64_t idxOffset;
	uint64_t meshesOffset;
	uint64_t meshletsOffset;
	uint64_t meshletsVtxOffset;
	uint64_t meshletsIdxOffset;
	uint64_t materialsOffset;
	uint64_t drawArgsOffset;
};
//constexpr u64 s = sizeof( geometry_buffer_info );


// TODO: rename
ALIGNAS( 16 ) struct draw_data
{
	vec3 pos;
	float scale;
	vec4 rot;

	uint meshIdx;
	uint bndVolMeshIdx;
};

// TODO: mip-mapping
// TODO: pbr renaming
struct material_data
{
	vec3 diffuseK;
	float dissolve;
	float shininess;
	uint diffuseIdx;
	uint bumpIdx;
	uint hash;
};
//constexpr u64 a = alignof( material_data );
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

struct mesh
{
	vec3 center;
	float radius;

	uint vertexCount;
	uint vertexOffset;

	// TODO: per lod
	uint materialCount;
	uint materialOffset;

	uint lodCount;
	mesh_lod lods[ 8 ];
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

// TODO: align
struct cull_info
{	
	uint64_t geometryBufferAddress;
	uint64_t drawArgsOffset;
	uint64_t meshDataOffset;

	vec4 planes[ 4 ];

	float	frustum[ 4 ];
	vec3	camPos;

	float	zNear;
	float	drawDistance;
	float	projWidth;
	float	projHeight;

	float	pyramidWidthPixels;
	float	pyramidHeightPixels;

	uint	mipLevelsCount;

	uint	drawCallsCount;
	uint	dbgMeshIdx;
};

struct downsample_info
{
	uint mips;
	uint workGroupCount;
	vec2 invRes;
};

#ifndef __cplusplus

//layout( set = 1, binding = 0, scalar ) readonly buffer pos_buffer{ vec3 positions[ 0 ]; } posBuffer[];
//layout( set = 1, binding = 0, scalar ) readonly buffer norm_buffer{ vec3 normals[ 0 ]; } normBuffer[];
layout( set = 1, binding = 1, std430 ) uniform global{ global_data g; } globalsCam[];
layout( set = 1, binding = 1, std430 ) uniform geom_info{ geometry_buffer_info g; } globalsGeom[];
layout( set = 1, binding = 2 ) uniform texture2D sampledImages[];
layout( set = 1, binding = 3 ) uniform sampler samplers[];

global_data cam = globalsCam[ 0 ].g;
geometry_buffer_info g = globalsGeom[ 1 ].g;
#endif // !__cplusplus
