#ifndef __HP_MESH_H__
#define __HP_MESH_H__

#include "core_types.h"

struct float2;
struct float3;
struct float4;

struct alignas( 16 ) packed_trs
{
	float3 t;
	float pad0;
	float4 r;
	float3 s;
	float pad1;
};

constexpr bool LEFT_HANDED = true;
static_assert( LEFT_HANDED );

struct packed_vtx
{
	float3 pos;
	float2 octNormal;
	float tanAngle;
	float u, v;
	u8 tanSign;
};

struct vertex_attrs
{
	float2 octNormal;
	float tanAngle;
	float u, v;
	u8 tanSign;
};

struct meshlet
{
	float3	aabbMin;
	float3	aabbMax;

	u32		vtxOffset;
	u32		triOffset;

	u32		vtxCount : 8;
	u32		triCount : 8;

	u32		padding : 16;
};

struct world_node
{
	packed_trs toWorld;
	u64 meshHash;
	u16 materialIdx;
};

#endif // !__HP_MESH_H__
