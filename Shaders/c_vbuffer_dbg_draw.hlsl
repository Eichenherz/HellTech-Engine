#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

#include "vbuffer.h"

[[vk::push_constant]]
vbuffer_dbg_draw_params pushBlock;

// NOTE: src and dst assumed to be the same dimensions, asserted on the host
float3 ColorHash( u32x2 v )
{
	u32 seed = v.x ^ ( v.y * 2654435761u );
	seed ^= seed >> 16;
	seed *= 0x45d9f3bu;
	seed ^= seed >> 16;
	return float3(
		( ( seed       ) & 0xFFu ) / 255.0f,
		( ( seed >>  8 ) & 0xFFu ) / 255.0f,
		( ( seed >> 16 ) & 0xFFu ) / 255.0f
	);
}

[numthreads(16, 16, 1)]
[shader("compute")]
void VBufferDbgDrawCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
	u32x2 vbuffPixel = gTexture2D_u32x2[ pushBlock.srcIdx ].Load( i32x3( globalDispatchID.xy, 0 ) );
	if( !VBufferIsValidPixel( vbuffPixel ) )
	{
		return;
	}

	float3 col = ColorHash( vbuffPixel );
	gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( col, 1.0f );
}
