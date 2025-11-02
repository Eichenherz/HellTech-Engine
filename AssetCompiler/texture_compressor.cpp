#include "texture_compressor.h"
#include "asset_compiler.h"

#include "ht_error.h"

#include <nvtt/nvtt.h>

struct nvtt_compressor
{
	nvtt::Context context;

	inline nvtt_compressor()
	{
		context.enableCudaAcceleration( true );
		HT_ASSERT( context.isCudaAccelerationEnabled() );
	}


};

inline texture_type MapNvttTextureType( nvtt::TextureType t )
{
	switch( t )
	{
	case nvtt::TextureType_2D:   return TEXTURE_TYPE_2D;
	case nvtt::TextureType_3D:   return TEXTURE_TYPE_3D;
	case nvtt::TextureType_Cube: return TEXTURE_TYPE_CUBE;
	default:               return TEXTURE_TYPE_2D;
	}
}

struct nvtt_surface
{
	nvtt::Surface texture;

	nvtt_surface( std::span<const u8> texData )
	{
		texture.loadFromMemory( std::data( texData ), std::size( texData ) );
	}

	inline texture_metadata GetTextureMetadata() const
	{
		texture_metadata meta = {
			.width = ( u32 ) texture.width(),
			.height = ( u32 ) texture.height(),
			.type = MapNvttTextureType( texture.type() ),
			.mipCount = ( u8 ) texture.countMipmaps()
		};

		if( i32 depth = texture.depth(); depth > 1 )
		{
			HT_ASSERT( false );
		}


	}
};