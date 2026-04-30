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
    u32x2 rawPixel = gTexture2D_u32x2[ pushBlock.vbuffIdx ].Load( i32x3( globalDispatchID.xy, 0 ) );
    if( !VBufferIsValidPixel( rawPixel ) )
    {
        gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( 0.0f, 0.0f, 0.0f, 1.0f );
        return;
    }

    vbuffer_pixel vBuffPixel = VBufferUnpackPixel( rawPixel );

    device_addr<gpu_meshlet> pGpuMeshlets = { gGlobData.mltAddr };
    gpu_meshlet mlt = pGpuMeshlets[ vBuffPixel.mltId ];
    gpu_instance inst = BufferLoad<gpu_instance>( pushBlock.instBuffIdx, vBuffPixel.instId );
    // NOTE: this is fucking stupid but we'll see later
    gpu_mesh mesh = BufferLoad<gpu_mesh>( pushBlock.meshDescIdx, inst.meshIdx );

    u32 triIdx = vBuffPixel.triId * 3 + mlt.triOffset + mesh.triOffset;
    u32 vtxOffset = mlt.vtxOffset + mesh.vtxOffset;

    u32x3 tri = FetchTriangleFromMegaBuff( triIdx ) + vtxOffset;

    device_addr<packed_vtx> vtxBuff = { gGlobData.vtxAddr };
    packed_vtx v0 = vtxBuff[ tri.x ];
    packed_vtx v1 = vtxBuff[ tri.y ];
    packed_vtx v2 = vtxBuff[ tri.z ];

    packed_trs toWorld = inst.toWorld;
    float4x4 toWorld4x4 = TrsToFloat4x4( toWorld.t, toWorld.r, toWorld.s );

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );
    float4x4 mvp = mul( toWorld4x4, cam.mainViewProj );

    float3 p0 = float3( v0.px, v0.py, v0.pz );
    float3 p1 = float3( v1.px, v1.py, v1.pz );
    float3 p2 = float3( v2.px, v2.py, v2.pz );

    float4 clip0 = mul( float4( p0, 1.0f ), mvp );
    float4 clip1 = mul( float4( p1, 1.0f ), mvp );
    float4 clip2 = mul( float4( p2, 1.0f ), mvp );

    float3 ndc0 = clip0.xyz / clip0.w;
    float3 ndc1 = clip1.xyz / clip1.w;
    float3 ndc2 = clip2.xyz / clip2.w;

    float2 pixelNdc = float2( 2.0f, -2.0f ) * ( float2( globalDispatchID.xy ) + 0.5f ) / pushBlock.texResolution
        + float2( -1.0f, 1.0f );

    float3 ndcBary = ComputeNDCBarycentrics( pixelNdc, ndc0.xy, ndc1.xy, ndc2.xy );

    // NOTE: Perspective-correct — need per-vertex W
    float3 invW = float3( 1.0f / clip0.w, 1.0f / clip1.w, 1.0f / clip2.w );
    float3 baryW = ndcBary * invW;
    float3 perspBary  = baryW / ( baryW.x + baryW.y + baryW.z );

    float3 n0 = DecodeOctaNormal( float2( v0.octNX, v0.octNY ) );
    float3 n1 = DecodeOctaNormal( float2( v1.octNX, v1.octNY ) );
    float3 n2 = DecodeOctaNormal( float2( v2.octNX, v2.octNY ) );

    float3 nInterp = perspBary.x * n0 + perspBary.y * n1 + perspBary.z * n2;
    float3 N = normalize( QuatRot( toWorld.r, nInterp / toWorld.s ) );

    float3 pInterp = perspBary.x * p0 + perspBary.y * p1 + perspBary.z * p2;
    float3 worldPos = QuatRot( toWorld.r, pInterp * toWorld.s ) + toWorld.t;

    float3 L   = normalize( float3( 0.5f, 1.0f, 0.3f ) );
    float3 V   = normalize( cam.worldPos - worldPos );   // view dir
    float  nDotL = dot( N, L );

    // Warm-cool hemispheric (Gooch-ish)
    float3 warm = float3( 0.55f, 0.52f, 0.48f );   // lit side
    float3 cool = float3( 0.28f, 0.29f, 0.32f );   // shadow side
    float  t    = nDotL * 0.5f + 0.5f;             // [-1,1] -> [0,1]
    float3 col  = lerp( cool, warm, t );

    // Soft rim
    float  rim  = pow( 1.0f - saturate( dot( N, V ) ), 2.0f );
    col += rim * 0.15f;

    // Broad soft spec (Blinn-Phong-ish, low exponent)
    float3 H    = normalize( L + V );
    float  spec = pow( saturate( dot( N, H ) ), 16.0f );
    col += spec * 0.1f;

    gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( col, 1.0f );
}