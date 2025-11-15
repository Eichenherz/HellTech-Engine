#ifndef __TEXTURE_COMPRESSOR_H__
#define __TEXTURE_COMPRESSOR_H__

#include <iostream>

#include "ht_error.h"
#include "asset_compiler.h"

#include <nvtt/nvtt.h>

inline void NvttMsgCallback( nvtt::Severity severity, nvtt::Error error, const char* message, const void* userData )
{
	if( severity == nvtt::Severity_Error )
	{
		const char* pErrStr = nvtt::errorString( error );
		std::cout << std::format( "ERROR: {}, MSG: {}", pErrStr, message );
	}
}

struct nvtt_batch_handler : public nvtt::OutputHandler
{
	u8* pBinOutData;
	u64 binOutDataMaxSize;
	std::vector<range> outputRanges;
	u64 offsetInBytes = 0;

	nvtt_batch_handler( u8* pBinOut, u64 size ) : pBinOutData{ pBinOut }, binOutDataMaxSize{ size } {}

	void beginImage( int size, int width, int height, int depth, int face, int mipLevel ) override {}

	bool writeData( const void* data, int size ) override
	{
		errno_t err = memcpy_s( pBinOutData + offsetInBytes, binOutDataMaxSize, data, size );
		if( err )
		{
			char buffer[ 256 ] = {};
			strerror_s( buffer, sizeof( buffer ), err );
			assert( false );
			return false;
		}
		outputRanges.push_back( { .offset = offsetInBytes, .size = ( u64 ) size } );
		offsetInBytes += size;

		return true;
	}

	void endImage() override {}
};

struct nvtt_surface_handler : public nvtt::OutputHandler
{
	u8* pBinOutData;
	u64 binOutDataMaxSize;
	std::vector<range> outputRanges;
	u64 offsetInBytes = 0;

	nvtt_surface_handler( u8* pBinOut, u64 size ) : pBinOutData{ pBinOut }, binOutDataMaxSize{ size } {}

	void beginImage( int size, int width, int height, int depth, int face, int mipLevel ) override {}

	bool writeData( const void* data, int size ) override
	{
		errno_t err = memcpy_s( pBinOutData + offsetInBytes, binOutDataMaxSize, data, size );
		if( err )
		{
			char buffer[ 256 ] = {};
			strerror_s( buffer, sizeof( buffer ), err );
			assert( false );
			return false;
		}
		outputRanges.push_back( { .offset = offsetInBytes, .size = ( u64 ) size } );
		offsetInBytes += size;

		return true;
	}

	void endImage() override {}
};

static inline texture_format MapMaterialMapToTextureFormat( material_map_type type )
{
	switch( type )
	{
	case material_map_type::BASE_COLOR:			return TEXTURE_FORMAT_BC7_SRGB;
	case material_map_type::NORMALS:			return TEXTURE_FORMAT_BC5;
	case material_map_type::METALLIC_ROUGHNESS:	return TEXTURE_FORMAT_BC7_LINEAR;
	case material_map_type::OCCLUSION:			return TEXTURE_FORMAT_BC4;
	case material_map_type::EMISSIVE:			return TEXTURE_FORMAT_BC7_LINEAR;
	default:									return TEXTURE_FORMAT_BC7_LINEAR;
	}
}
static inline texture_type MapNvttTextureType( nvtt::TextureType t )
{
	switch( t )
	{
	case nvtt::TextureType_2D:   return TEXTURE_TYPE_2D;
	case nvtt::TextureType_3D:   return TEXTURE_TYPE_3D;
	case nvtt::TextureType_Cube: return TEXTURE_TYPE_CUBE;
	default:               return TEXTURE_TYPE_2D;
	}
}
static inline texture_format MapNvttFormatToTextureFormat( nvtt::Format fmt )
{
	switch( fmt )
	{
	case nvtt::Format_RGBA:        return texture_format::TEXTURE_FORMAT_RBGA8_UNORM;
	case nvtt::Format_BC1:         return texture_format::TEXTURE_FORMAT_BC1;
	case nvtt::Format_BC1a:        return texture_format::TEXTURE_FORMAT_BC1A;
	case nvtt::Format_BC2:         return texture_format::TEXTURE_FORMAT_BC2;
	case nvtt::Format_BC3:         return texture_format::TEXTURE_FORMAT_BC3;
	case nvtt::Format_BC3n:        return texture_format::TEXTURE_FORMAT_BC3_NORMAL_MAP;
	case nvtt::Format_BC3_RGBM:    return texture_format::TEXTURE_FORMAT_BC3_RGBM;
	case nvtt::Format_BC4:         return texture_format::TEXTURE_FORMAT_BC4;
	case nvtt::Format_BC4S:        return texture_format::TEXTURE_FORMAT_BC4_SIGNED;
	case nvtt::Format_BC5:         return texture_format::TEXTURE_FORMAT_BC5;
	case nvtt::Format_BC5S:        return texture_format::TEXTURE_FORMAT_BC5_SIGNED;
	case nvtt::Format_BC7:         return texture_format::TEXTURE_FORMAT_BC7_LINEAR;
	default:                        return texture_format::TEXTURE_FORMAT_UNDEFINED;
	}
}
static inline nvtt::Format MapTextureFormatToNvttFormat( texture_format fmt )
{
	switch( fmt )
	{
	case texture_format::TEXTURE_FORMAT_RBGA8_UNORM:      return nvtt::Format_RGBA;
	case texture_format::TEXTURE_FORMAT_BC1:              return nvtt::Format_BC1;
	case texture_format::TEXTURE_FORMAT_BC1A:             return nvtt::Format_BC1a;
	case texture_format::TEXTURE_FORMAT_BC2:              return nvtt::Format_BC2;
	case texture_format::TEXTURE_FORMAT_BC3:              return nvtt::Format_BC3;
	case texture_format::TEXTURE_FORMAT_BC3_NORMAL_MAP:   return nvtt::Format_BC3n;
	case texture_format::TEXTURE_FORMAT_BC3_RGBM:         return nvtt::Format_BC3_RGBM;
	case texture_format::TEXTURE_FORMAT_BC4:              return nvtt::Format_BC4;
	case texture_format::TEXTURE_FORMAT_BC4_SIGNED:       return nvtt::Format_BC4S;
	case texture_format::TEXTURE_FORMAT_BC5:              return nvtt::Format_BC5;
	case texture_format::TEXTURE_FORMAT_BC5_SIGNED:       return nvtt::Format_BC5S;
	case texture_format::TEXTURE_FORMAT_BC7_LINEAR:       return nvtt::Format_BC7;
	case texture_format::TEXTURE_FORMAT_BC7_SRGB:         return nvtt::Format_BC7;
	default:                            assert( false );  return nvtt::Format_RGBA;
	}
}
inline texture_rect NvttGetSurafceRect( const nvtt::Surface& surface )
{
	return {
		.width = ( u16 ) surface.width(),
		.height = ( u16 ) surface.height(),
		.depth = ( u16 ) surface.depth()
	};
}

struct texture_data
{
	std::span<const u8> bin;
};

struct compression_batch
{
	std::vector<texture_data> texturesBin;
	texture_format format;
	material_map_type mapType;

	compression_batch() = default;

	explicit compression_batch( material_map_type batchMaterialMapType ) : 
		format{ MapMaterialMapToTextureFormat( batchMaterialMapType ) }, mapType{ batchMaterialMapType } {}

	inline void Append( texture_data tex )
	{
		texturesBin.push_back( tex );
	}
};

struct nvtt_surface
{
	nvtt::Surface surf;
	nvtt::Format format;
	bool isNormalMap;
	bool isSrgb;
};

struct nvtt_batch
{
	std::vector<nvtt::Surface> surfaces;
	nvtt::Format format;
	bool isNormalMap;
	bool isSrgb;

	inline nvtt_batch( const compression_batch& batch )
	{
		format = MapTextureFormatToNvttFormat( batch.format );
		isNormalMap = material_map_type::NORMALS == batch.mapType;
		isSrgb = TEXTURE_FORMAT_BC7_SRGB == batch.format;
		for( const texture_data& t : batch.texturesBin )
		{
			nvtt::Surface surf;
			HT_ASSERT( surf.loadFromMemory( std::data( t.bin ), std::size( t.bin ) ) );
			surf.setNormalMap( isNormalMap );
			surfaces.emplace_back( surf );
		}
	}
};

struct nvtt_compressor
{
	nvtt::Context context;

	inline nvtt_compressor()
	{
		nvtt::setMessageCallback( NvttMsgCallback, nullptr );

		context.enableCudaAcceleration( true );
		HT_ASSERT( context.isCudaAccelerationEnabled() );
	}

	inline u64 GetEstimatedBatchSize( const nvtt_batch& batch ) const
	{
		u64 batchSizeInBytes = 0;

		nvtt::CompressionOptions compressionOptions = {};
		compressionOptions.setFormat( batch.format );
		compressionOptions.setQuality( nvtt::Quality_Fastest );
		for( const nvtt::Surface& surface : batch.surfaces )
		{
			const texture_rect rect = NvttGetSurafceRect( surface );
			batchSizeInBytes += GetEstimatedCompressedSize( rect, 1, compressionOptions );
		}
		return batchSizeInBytes;
	}

	inline u64 GetEstimatedCompressedSize( texture_rect rect, u32 mipCount, const nvtt::CompressionOptions& opts ) const
	{
		return context.estimateSize( rect.width, rect.height, rect.depth, mipCount, opts );
	}

	inline void ProcessBatch( nvtt_batch& batch, nvtt_batch_handler& outHandler ) 
	{
		nvtt::OutputOptions outOpts;
		outOpts.setOutputHandler( &outHandler );
		outOpts.setSrgbFlag( batch.isSrgb );

		nvtt::CompressionOptions compressionOptions = {};
		compressionOptions.setFormat( batch.format );
		compressionOptions.setQuality( nvtt::Quality_Fastest );
		nvtt::BatchList batchList;
		// NOTE: we only move to GPU when making the BatchList !
		for( nvtt::Surface& surface : batch.surfaces )
		{
			surface.ToGPU();
			batchList.Append( &surface, 0, 0, &outOpts );
		}

		context.compress( batchList, compressionOptions ); 
	}
};


#endif // !__TEXTURE_COMPRESSOR_H__
