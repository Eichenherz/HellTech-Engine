#pragma once

#include "core_types.h"

//namespace std
//{
//	template<class, class> class vector;
//}

// TODO: purge this shit
#include <vector>
#include "r_data_structs.h"
#include <DirectXCollision.h>

struct range
{
	u64 offset : 32;
	u64 size : 32;
};

// TODO: formalzie aabb
struct model
{
	std::vector<float>	vertices;
	std::vector<u32>	indices;
	range				positionRange;
	range				normalsRange;
	range				uvRange;
	range				tangentsRange;
	float				aabbMinMax[ 6 ];
	u32					vtxCount;
	u32					idxCount;
	u32					triCount;
	u32					flags;
};

enum texture_format_type : u8
{
	TEXTURE_FORMAT_RBGA8_SRGB,
	TEXTURE_FORMAT_RBGA8_UNORM,
	TEXTURE_FORMAT_BC1_RGB_SRGB,
	TEXTURE_FORMAT_BC5_UNORM,
	TEXTURE_FORMAT_COUNT
};

enum pbr_texture_type : u8
{
	PBR_TEXTURE_BASE_COLOR = 0,
	PBR_TEXTURE_ORM = 1,
	PBR_TEXTURE_NORMALS = 2,
	PBR_TEXTURE_COUNT
};

// TODO: improve
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
	range				texBinRange;
	sampler_config		samplerConfig;
	u16					width;
	u16					height;
	pbr_texture_type	format;
};

struct pbr_material
{
	image_metadata	textureMeta[ PBR_TEXTURE_COUNT ];
	float			baseColorFactor[ 3 ];
	float			metallicFactor;
	float			roughnessFactor;
};

using PfnReadFile = std::vector<u8>( * )( const char* );

void LoadGlbFile(
	const std::vector<u8>& glbData,

	DirectX::BoundingBox& outAabb,
	std::vector<vertex>& vertices,
	std::vector<u32>& indices,
	std::vector<u8>& textureBinData,
	std::vector<pbr_material>& catalogue );

void MeshoptMakeLods(
	const std::vector<vertex>& vertices,
	u64							maxLodCount,
	std::vector<u32>& lodIndices,
	std::vector<u32>& idxBuffer,
	std::vector<mesh_lod>& outMeshLods );

std::vector<u8> CmpCompressTexture( const std::vector<u8>& texBin, u64 widthInBytes, u64 heightInBytes );
