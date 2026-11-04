#pragma once

#ifndef HELLTECH_HT_GFX_TYPES_H
#define HELLTECH_HT_GFX_TYPES_H

#include <ht_core_types.h>
#include <ht_vec_types.h>
#include <ht_math.h>
#include <ht_hash.h>
#include <ht_macros.h>

struct trs_t
{
    float3	t;
    quat4	r;
    float3	s;

    explicit constexpr operator float4x3() const { return TrsToFloat4x3RowMaj( t, r, s ); }
};

HT_FORCEINLINE trs_t GltfComposePackedTRS( trs_t parent, trs_t child )
{
    return {
        .t = parent.t + ht::vec_rot( child.t * parent.s, parent.r ),
        .r = ht::quat_mul( child.r, parent.r ),
        .s = parent.s * child.s
    };
}

struct packed_trs
{
    pkd_fp32x3	t;
    pkd_fp32x4  r;
    pkd_fp32x3	s;

    constexpr packed_trs() = default;
    constexpr packed_trs( pkd_fp32x3 t, pkd_fp32x4 r, pkd_fp32x3 s ) : t{ t }, r{ r }, s{ s } {}
    constexpr packed_trs( const trs_t& v ) : t{ v.t }, r{ v.r }, s{ v.s } {}
    constexpr operator trs_t() const { return { .t = t, .r = r, .s = s }; }
};

constexpr packed_trs IDENTITY_TRS = {
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
    { 1.0f, 1.0f, 1.0f }
};

struct vertex_attrs
{
	float2	octNormal;
	float	tanAngle;
	float	u, v;
	u8		tanSign;
};

HT_DEF_STRUCT_W_HASH( world_node,
	packed_trs	toWorld;
	u64			meshID;
);

enum alpha_mode : u8
{
	ALPHA_MODE_OPAQUE,
	ALPHA_MODE_MASK,
	ALPHA_MODE_BLEND,
};

enum sampler_filter_mode_flags : u8
{
	FILTER_NEAREST					= 1 << 0,
	FILTER_LINEAR					= 1 << 1,
	FILTER_NEAREST_MIPMAP_NEAREST	= 1 << 2,
	FILTER_LINEAR_MIPMAP_NEAREST	= 1 << 3,
	FILTER_NEAREST_MIPMAP_LINEAR	= 1 << 4,
	FILTER_LINEAR_MIPMAP_LINEAR		= 1 << 5,
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
	u32 wrapModeS	: 8;
	u32 wrapModeT	: 8;

	bool operator==( const sampler_config& rhs ) const
	{
		return filterModeS	== rhs.filterModeS
			&& filterModeT	== rhs.filterModeT
			&& wrapModeS	== rhs.wrapModeS
			&& wrapModeT	== rhs.wrapModeT;
	}
};

constexpr sampler_config DEFAULT_SAMPLER = {
	.filterModeS	= sampler_filter_mode_flags::FILTER_LINEAR,
	.filterModeT	= sampler_filter_mode_flags::FILTER_LINEAR,
	.wrapModeS		= sampler_wrap_mode_flags::WRAP_REPEAT,
	.wrapModeT		= sampler_wrap_mode_flags::WRAP_REPEAT
};

struct material_desc
{
	u64			baseColorHash;
	u64			metallicRoughnessHash;
	u64			normalHash;
	u64			emissiveHash;

	float4		baseColFactor;
	pkd_fp32x3	emissiveFactor;
	float		metallicFactor;
	float		roughnessFactor;
	float		alphaCutoff;

	u16			samplerIdx;

	alpha_mode	alphaMode;
};

#endif // !HELLTECH_HT_GFX_TYPES_H
