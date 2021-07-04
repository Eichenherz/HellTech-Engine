#pragma once

#include "core_types.h"

// TODO: remove these includes
#include <vector>
#include "r_data_structs.h"


enum texture_format : u8
{
	TEXTURE_FORMAT_UNDEFINED,
	TEXTURE_FORMAT_RBGA8_SRGB,
	TEXTURE_FORMAT_RBGA8_UNORM,
	TEXTURE_FORMAT_BC1_RGB_SRGB,
	TEXTURE_FORMAT_BC5_UNORM,
	TEXTURE_FORMAT_COUNT
};

enum texture_type : u8
{
	TEXTURE_TYPE_1D,
	TEXTURE_TYPE_2D,
	TEXTURE_TYPE_3D,
	//TEXTURE_TYPE_CUBE,
	TEXTURE_TYPE_COUNT
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

// TODO: compress even more ?
struct sampler_config
{
	gltf_sampler_filter			min;
	gltf_sampler_filter			mag;
	gltf_sampler_address_mode	addrU;
	gltf_sampler_address_mode	addrV;
	//gltf_sampler_address_mode	addrW;
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

struct binary_mesh_desc
{
	//MESH_ATTRIBUTE_TYPE attributeType[ 6 ];
	float	aabbMin[ 3 ];
	float	aabbMax[ 3 ];
	range	vtxRange;
	range	lodRanges[ 4 ];
	u32		materialIndex;
	u8		lodCount = 1;
};

struct drak_file_header
{
	char magik[ 4 ] = "DRK";
	u32 drakVer = 0;
	u32 contentVer = 0;
};
// NOTE - all offsets and ranges are relative to dataOffset
struct drak_file_desc
{
	range vtxRange;
	range idxRange;
	range texRange;
	u32	dataOffset;
	u32	compressedSize;
	u32	originalSize;
	u32	meshesCount;
	u32	mtrlsCount;
	u32	texCount;
};

using PfnReadFile = std::vector<u8>( * )( const char* );

void CompileGlbAssetToBinary( const std::vector<u8>& glbData, std::vector<u8>& drakAsset );
