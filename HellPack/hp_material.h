#ifndef __HP_MATERIAL_H__
#define __HP_MATERIAL_H__

#include "core_types.h"

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
	float metallicFactor;
	float roughnessFactor;
	float alphaCutoff;
	float3 emissiveFactor;

	u16 samplerIdx;

	alpha_mode alphaMode;
};

#endif // !__HP_MATERIAL_H__
