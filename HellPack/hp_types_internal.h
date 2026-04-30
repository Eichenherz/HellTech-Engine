#ifndef __HP_TYPES_INTERNAL_H__
#define __HP_TYPES_INTERNAL_H__

#include "ht_core_types.h"

#include <vector>
#include <string>

struct raw_mesh
{
	std::string         name;
	std::vector<float3> pos;
	std::vector<float3> normals;
	std::vector<float4> tans;
	std::vector<float2> uvs;
	std::vector<u32>    indices;
	u32                 materialIdx;
};

enum class image_channels_t : u8
{
	UNKNOWN = 0,
	R = 1,
	RG = 2,
	RGB = 3,
	RGBA = 4
};

enum class image_bit_depth_t : u8
{
	UNKNOWN = 0,
	B8  = 8,
	B16 = 16,
	B32 = 32
};

enum class image_pixel_type : u8
{
	UNKNOWN = 0,
	UBYTE,
	USHORT,
	FLOAT32
};

struct image_metadata
{
	u16					width;
	u16					height;
	image_channels_t	component;
	image_bit_depth_t	bits;
	image_pixel_type	pixelType;
};

struct raw_image_view
{
	std::span<const u8> data;
	image_metadata		metadata;
};

struct raw_meshlet
{
	std::vector<packed_vtx> vertices;
	std::vector<u8>			triIndices;
	float3					aabbMin;
	float3					aabbMax;
};

struct raw_material_info
{
	std::string name;

	float4		baseColFactor;
	float		metallicFactor;
	float		roughnessFactor;
	float		alphaCutoff;
	float3		emissiveFactor;

	u16 		baseColorIdx;
	u16 		metallicRoughnessIdx;
	u16 		normalIdx;
	u16 		occlusionIdx;
	u16 		emissiveIdx;
	u16 		samplerIdx;

	alpha_mode	alphaMode;
};

struct mesh_asset
{
	std::vector<packed_vtx>		vertices;
	std::vector<u8>				triIndices;
	std::vector<gpu_meshlet>	meshlets;
	std::array<float3, 2>		aabb; // NOTE: helps with serialization { min, max }
};

struct packed_trs;

struct raw_node
{
	packed_trs	toWorld;
	i32			meshIdx;
};

#endif // !__HP_TYPES_INTERNAL_H__
