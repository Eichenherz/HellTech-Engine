#pragma once

#include "core_types.h"

// TODO: remove these includes
#include <vector>
#include <span>

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

struct uuid128
{
	u64 dataChunk[ 2 ];

	bool operator==( const uuid128& other ) const
	{
		return ( this->dataChunk[ 0 ] == other.dataChunk[ 0 ] ) && 
			( this->dataChunk[ 1 ] == other.dataChunk[ 1 ] );
	}
};

struct image_metadata
{
	//uuid128         uuid;
	u64             nameHash;
	range			texBinRange;
	u16				width;
	u16				height;
	texture_format	format;
	texture_type	type;
	u8				mipCount = 1;
	u8				layerCount = 1;
};

// TODO: must add dataOffset and have the ranges offset into that
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

using PfnReadFile = std::vector<u8>( * )( const char* );

void CompileGlbAssetToBinary( const std::vector<u8>& glbData, std::vector<u8>& drakAsset );
template<typename T> extern u64 MeshoptReindexMesh( std::span<T> vtxSpan, std::span<u32> idxSpan );
template<typename T> extern void MeshoptOptimizeMesh( std::span<T> vtxSpan, std::span<u32> idxSpan );