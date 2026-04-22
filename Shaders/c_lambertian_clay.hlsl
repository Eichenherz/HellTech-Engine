#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

#include "vbuffer.h"

[[vk::push_constant]]
lambertian_clay_params pushBlock;

[numthreads(16, 16, 1)]
[shader("compute")]
void LambertianClayCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
    u32x2 vbuffPixel = gTexture2D_u32x2[ pushBlock.vbuffIdx ].Load( i32x3( globalDispatchID.xy, 0 ) );
    if( !VBufferIsValidPixel( vbuffPixel ) )
    {
        return;
    }

    u32 triIdx = vbuffPixel.x;
    u32 instIdx = vbuffPixel.y;

    device_addr<packed_vtx> vtxBuff = { gGlobData.vtxAddr };
    packed_vtx v0 = vtxBuff[ triIdx * 3 + 0 ];
    packed_vtx v1 = vtxBuff[ triIdx * 3 + 1 ];
    packed_vtx v2 = vtxBuff[ triIdx * 3 + 2 ];

    instance_desc instDesc = BufferLoad<instance_desc>( pushBlock.instDescIdx, instIdx );
    float4x4 toWorld = TrsToFloat4x4( instDesc.toWorld.t, instDesc.toWorld.r, instDesc.toWorld.s );

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );
    float4x4 mvp = mul( toWorld, mul( cam.mainView, cam.proj ) );

    float4 p0 = mul( float4( v0.px, v0.py, v0.pz, 1.0f ), mvp );
    float4 p1 = mul( float4( v1.px, v1.py, v1.pz, 1.0f ), mvp );
    float4 p2 = mul( float4( v2.px, v2.py, v2.pz, 1.0f ), mvp );

    float2 s0 = ClipSpaceToScreenSpace( p0, pushBlock.texResolution );
    float2 s1 = ClipSpaceToScreenSpace( p1, pushBlock.texResolution );
    float2 s2 = ClipSpaceToScreenSpace( p2, pushBlock.texResolution );

    float3 affineBary = ComputeAffineBarycentricsFromScreenSapce( float2( globalDispatchID.xy ), s0, s1, s2 );

    // Perspective-correct — need per-vertex W
    float3 invW = float3( 1.0f / p0.w, 1.0f / p1.w, 1.0f / p2.w );
    float3 baryW = affineBary * invW;
    float3 perspBary  = baryW / ( baryW.x + baryW.y + baryW.z );

    float3 n0 = DecodeOctaNormal( float2( v0.octNX, v0.octNY ) );
    float3 n1 = DecodeOctaNormal( float2( v1.octNX, v1.octNY ) );
    float3 n2 = DecodeOctaNormal( float2( v2.octNX, v2.octNY ) );

    float3 nInterp = perspBary.x * n0 + perspBary.y * n1 + perspBary.z * n2;
    float3 N = normalize( QuatRot( instDesc.toWorld.r, nInterp ) );

    float3 greyClay = float3( 0.7, 0.7, 0.7 );
    float  shade     = saturate( dot( N, normalize( float3( 0.5f, 1.0f, 0.3f ) ) ) );
    float3 col       = greyClay * ( shade * 0.8f + 0.2f );

    gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( col, 1.0f );
}