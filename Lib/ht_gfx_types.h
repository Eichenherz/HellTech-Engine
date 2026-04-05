#pragma once

#ifndef __HT_GFX_TYPES_H__
#define __HT_GFX_TYPES_H__

#include "ht_core_types.h"
#include "ht_vec_types.h"

struct alignas( 16 ) packed_trs
{
	float3 t;
	float pad0;
	float4 r;
	float3 s;
	float pad1;
};

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

enum alpha_mode : u8
{
	ALPHA_MODE_OPAQUE,
	ALPHA_MODE_MASK,
	ALPHA_MODE_BLEND,
};

enum sampler_filter_mode_flags : u8
{
	FILTER_NEAREST = 1 << 0,
	FILTER_LINEAR = 1 << 1,
	FILTER_NEAREST_MIPMAP_NEAREST = 1 << 2,
	FILTER_LINEAR_MIPMAP_NEAREST = 1 << 3,
	FILTER_NEAREST_MIPMAP_LINEAR = 1 << 4,
	FILTER_LINEAR_MIPMAP_LINEAR = 1 << 5,
};

enum sampler_wrap_mode_flags : u8
{
	WRAP_CLAMP_TO_EDGE        = 1 << 0,
	WRAP_MIRRORED_REPEAT      = 1 << 1,
	WRAP_REPEAT               = 1 << 2,
};

struct sampler_config
{
	u32 filterModeS : 8;
	u32 filterModeT : 8;
	u32 wrapModeS : 8;
	u32 wrapModeT : 8;

	inline bool operator==( const sampler_config& rhs ) const
	{
		return filterModeS == rhs.filterModeS
			&& filterModeT == rhs.filterModeT
			&& wrapModeS == rhs.wrapModeS
			&& wrapModeT == rhs.wrapModeT;
	}
	inline bool operator!=( const sampler_config& rhs ) const
	{
		return !( *this == rhs );
	}
};

constexpr sampler_config DEFAULT_SAMPLER = {
	.filterModeS = sampler_filter_mode_flags::FILTER_LINEAR,
	.filterModeT = sampler_filter_mode_flags::FILTER_LINEAR,
	.wrapModeS = sampler_wrap_mode_flags::WRAP_REPEAT,
	.wrapModeT = sampler_wrap_mode_flags::WRAP_REPEAT
};

struct material_desc
{
	u64 baseColorHash;
	u64 metallicRoughnessHash;
	u64 normalHash;
	u64 emissiveHash;

	float4 baseColFactor;
	float3 emissiveFactor;
	float metallicFactor;
	float roughnessFactor;
	float alphaCutoff;

	u16 samplerIdx;

	alpha_mode alphaMode;
};

#endif // !__HT_GFX_TYPES_H__
