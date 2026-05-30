#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

#include "ht_dbg_common.h"

[[vk::push_constant]]
meshlet_pass_params pushBlock;

[shader( "pixel" )]
float4 MeshletClayPassPsMain( in meshlet_vs_out vsOut ) : SV_Target
{
    // NOTE: bc of interpolators
    float3 t = normalize( vsOut.t );
    float3 n = normalize( vsOut.n );
    // NOTE: orthonormalize t wrt n
    t = normalize( t - dot( t, n ) * n );

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );

    float3 L   = normalize( float3( 0.5f, 1.0f, 0.3f ) );
    float3 V   = normalize( cam.worldPos - vsOut.wrldPos );   // view dir
    float  nDotL = dot( n, L );

    // Warm-cool hemispheric (Gooch-ish)
    float3 warm = float3( 0.55f, 0.52f, 0.48f );   // lit side
    float3 cool = float3( 0.28f, 0.29f, 0.32f );   // shadow side
    float  tLerp = nDotL * 0.5f + 0.5f;             // [-1,1] -> [0,1]
    float3 col  = lerp( cool, warm, tLerp );

    // Soft rim
    float  rim  = pow( 1.0f - saturate( dot( n, V ) ), 2.0f );
    col += rim * 0.15f;

    // Broad soft spec ( Blinn-Phong-ish, low exponent )
    float3 H    = normalize( L + V );
    float  spec = pow( saturate( dot( n, H ) ), 16.0f );
    col += spec * 0.1f;

	return float4( 1.0f, 0.5f, 0.3f, 0.2f );// col, 0.2f );
}