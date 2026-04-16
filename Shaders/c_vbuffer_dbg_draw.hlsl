#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
vbuffer_dbg_draw_params pushBlock;

// NOTE: src and dst assumed to be the same dimensions, asserted on the host
float3 ColorHash( float2 v )
{
	u32 seed = asuint( v.x ) ^ ( asuint( v.y ) * 2654435761u );
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
	float2 texel = gTexture2D_u32x2[ pushBlock.srcIdx ].Load( i32x3( globalDispatchID.xy, 0 ) );
	float3 col   = ColorHash( texel );

	gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( col, 1.0f );
}
