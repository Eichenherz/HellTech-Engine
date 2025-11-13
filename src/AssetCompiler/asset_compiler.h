#ifndef __ASSET_COMPILER_H__
#define __ASSET_COMPILER_H__

#include <vector>
#include <span>
#include <string_view>
#include <array>

#include "core_types.h"
#include "r_data_structs.h"
#include "ht_error.h"

#include <System/sys_filesystem.h>


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

using virtual_path = std::array<char, 128>;

inline virtual_path MakeVirtPath( std::string_view sv )
{
	virtual_path out = {};
	HT_ASSERT( sizeof( out ) > std::size( sv ) );
	// NOTE: use memcpy bc sv is not guaranteed to end in '\0'. Will not overflow bc of the above, but can read past the end.
	std::memcpy( std::data( out ), std::data( sv ), std::size( sv ) );
	return out;
}

struct material_info
{
	u32 baseColorIdx;
	u32 metallicRoughnessIdx;
	u32 normalIdx;
	u32 occlusionIdx;
	u32 emissiveIdx;
	u32 samplerIdx;

	vec3 baseColFactor;
	//float pad0;
	float metallicFactor;
	float roughnessFactor;
	float alphaCutoff;
	//float pad1;
	vec3 emmisiveFactor;
	//float pad2;

	alpha_mode alphaMode;
};

enum texture_format : u8
{
	TEXTURE_FORMAT_UNDEFINED,
	TEXTURE_FORMAT_RBGA8_SRGB,
	TEXTURE_FORMAT_RBGA8_UNORM,
	TEXTURE_FORMAT_BC1, 
	TEXTURE_FORMAT_BC1A,
	TEXTURE_FORMAT_BC2, 
	TEXTURE_FORMAT_BC3, 
	TEXTURE_FORMAT_BC3_NORMAL_MAP,
	TEXTURE_FORMAT_BC3_RGBM,
	TEXTURE_FORMAT_BC4,
	TEXTURE_FORMAT_BC4_SIGNED,
	TEXTURE_FORMAT_BC5,
	TEXTURE_FORMAT_BC5_SIGNED,
	TEXTURE_FORMAT_BC6_UNSIGNED,
	TEXTURE_FORMAT_BC6_SIGNED,
	TEXTURE_FORMAT_BC7_SRGB, 
	TEXTURE_FORMAT_BC7_LINEAR,
	TEXTURE_FORMAT_COUNT
};

enum texture_type : u8
{
	TEXTURE_TYPE_2D,
	TEXTURE_TYPE_3D,
	TEXTURE_TYPE_CUBE,
	TEXTURE_TYPE_COUNT
};

enum class material_map_type : u8
{
	BASE_COLOR,
	NORMALS,
	METALLIC_ROUGHNESS,
	OCCLUSION,
	EMISSIVE,
	COUNT
};

enum gltf_sampler_filter : u8
{
	GLTF_SAMPLER_FILTER_NEAREST = 0,
	GLTF_SAMPLER_FILTER_LINEAR = 1,
	GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_NEAREST = 2,
	GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_NEAREST = 3,
	GLTF_SAMPLER_FILTER_NEAREST_MIPMAP_LINEAR = 4,
	GLTF_SAMPLER_FILTER_LINEAR_MIPMAP_LINEAR
};

enum gltf_sampler_address_mode : u8
{
	GLTF_SAMPLER_ADDRESS_MODE_REPEAT = 0,
	GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 1,
	GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 2
};

struct texture_rect
{
	u16 width;
	u16 height;
	u16 depth;
};

struct texture_metadata
{
	u16				width;
	u16				height;
	texture_format	format;
	texture_type	type;
	u8				mipCount;
	u8				layerCount;
};

struct texture_entry
{
	range texRangeInBytes;
	texture_metadata metadata;
};



struct image_metadata
{
	u64				nameHash;
	range			texBinRange;
	u16				width;
	u16				height;
	texture_format	format;
	texture_type	type;
	u8				mipCount = 1;
	u8				layerCount = 1;
};

// TODO: count bytes or elements
struct drak_file_footer
{
	range meshesByteRange;
	range mtrlsByteRange;
	range imgsByteRange;
	range vtxByteRange;
	range idxByteRange;
	range texBinByteRange;
	range mletsByteRange;
	range mletsDataByteRange;

	u32	compressedSize;
	u32	originalSize;

	char magik[ 4 ] = "DRK";
	u32 drakVer = 0;
	u32 contentVer = 0;
};

void GltfConditionAssetFile( path filePath );
void CompileGlbAssetToBinary( const std::vector<u8>& glbData, std::vector<u8>& drakAsset );
template<typename T> extern u64 MeshoptReindexMesh( std::span<T> vtxSpan, std::span<u32> idxSpan );
template<typename T> extern void MeshoptOptimizeMesh( std::span<T> vtxSpan, std::span<u32> idxSpan );

#endif // !__ASSET_COMPILER_H__
