#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

#include "vbuffer.h"

[[vk::push_constant]]
vbuffer_dbg_draw_params pushBlock;

float3 ColorPixelHash( in vbuffer_pixel v )
{
	// NOTE: ^ CONST so nothing gets to be zero basically
	// NOTE: use unpacked so garbage upper bits in rawPixel can't leak in
    u32 seed = v.mltId 	* 2654435761u
             ^ v.triId  * 40503u
             ^ v.instId * 73856093u
             ^ 0x9E3779B9u;

    seed ^= seed >> 16;
    seed *= 0x45d9f3bu;
    seed ^= seed >> 16;

    return float3(
        ( ( seed       ) & 0xFFu ) / 255.0f,
        ( ( seed >>  8 ) & 0xFFu ) / 255.0f,
        ( ( seed >> 16 ) & 0xFFu ) / 255.0f
    );
}

// NOTE: need to reconstruct manually
float4 ColorId( u32 id )
{
	u32 offsettedId = id;// + 1; // NOTE: + 1 to avoid black
	return float4(
		( ( offsettedId       ) & 0xFFu ) / 255.0f,
		( ( offsettedId >>  8 ) & 0xFFu ) / 255.0f,
		( ( offsettedId >> 16 ) & 0xFFu ) / 255.0f,
		( ( offsettedId >> 24 ) & 0xFFu ) / 255.0f
	);
}

[numthreads(16, 16, 1)]
[shader("compute")]
void VBufferDbgDrawCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
	u32x2 rawPixel = gTexture2D_u32x2[ pushBlock.srcIdx ].Load( i32x3( globalDispatchID.xy, 0 ) );

	float4 col = float4( 0.0f, 0.0f, 0.0f, 1.0f );
	if( VBufferIsValidPixel( rawPixel ) )
	{
		vbuffer_pixel vBuffPixel = VBufferUnpackPixel( rawPixel );
		//col = float4( ColorPixelHash( vBuffPixel ), 1.0f);
		//col = ColorId( vBuffPixel.mltId );
		col = ColorId( vBuffPixel.triId );
		//col = ColorId( vBuffPixel.instId );
	}

	gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = col;
}
