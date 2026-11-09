#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_unpacking.h"

#include "vbuffer.h"

[[vk::push_constant]]
lambertian_clay_params pushBlock;

[shader( "compute" )]
[numthreads( 16, 16, 1 )]
void LambertianClayCsMain( u32x3 globalDispatchID : SV_DispatchThreadID )
{
    if( any( globalDispatchID.xy >= pushBlock.vbuffRes ) ) return;

    u32x2 rawPixel = gTexture2D_u32x2[ pushBlock.vbuffIdx ].Load( i32x3( globalDispatchID.xy, 0 ) );
    if( !VBufferIsValidPixel( rawPixel ) )
    {
        gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( 0.0f, 0.0f, 0.0f, 1.0f );
        return;
    }

    vbuffer_pixel       vBuffPixel  = VBufferUnpackPixel( rawPixel );

    gpu_meshlet         mlt         = device_ptr<gpu_meshlet>( g_GlobalData.mltAddr )[ vBuffPixel.mltId ];
    gpu_instance        inst        = device_ptr<gpu_instance>( g_GlobalData.instAddr )[ vBuffPixel.instId ];
    // NOTE: this is fucking stupid but we'll see later
    RHNDL32( gpu_mesh ) hMesh       = device_ptr<RHNDL32( gpu_mesh )>( g_GlobalData.meshRefAddr )[ inst.meshIdx ];
   	gpu_mesh            meshDesc    = device_ptr<gpu_mesh>( g_GlobalData.meshDescAddr )[ hMesh.idx ];

    u32x2 mltVtxStreamSizes = GPUMeshletComputeVtxStreamSizes( mlt );

    u64 mltPayloadAddr      = g_GlobalData.pldAddr + meshDesc.lods[ vBuffPixel.lodId ].payloadStartInBytes + mlt.offsetInPayload;

    u32 triStartInBytes     = vBuffPixel.triId * 3 + dot( u32x2( 1, 1 ), mltVtxStreamSizes );
    u32x3 vtxIDs    = FetchTriangleIndicesFromMegaBuff( device_ptr<u32>( mltPayloadAddr ), triStartInBytes );

    float3 p0       = DecodeVertexFromMegaBuff( mlt, device_ptr<u32>( mltPayloadAddr ), vtxIDs.x );
    float3 p1       = DecodeVertexFromMegaBuff( mlt, device_ptr<u32>( mltPayloadAddr ), vtxIDs.y );
    float3 p2       = DecodeVertexFromMegaBuff( mlt, device_ptr<u32>( mltPayloadAddr ), vtxIDs.z );

    view_data cam   = device_ptr<view_data>( g_GlobalData.viewsAddr )[ g_GlobalData.mainCamIdx ];
    float4x4 mvp    = mul( f4x3_To_f4x4_Affine( inst.toWorld ), cam.mainViewProj );

    float4 clip0    = mul( float4( p0, 1.0f ), mvp );
    float4 clip1    = mul( float4( p1, 1.0f ), mvp );
    float4 clip2    = mul( float4( p2, 1.0f ), mvp );

    float3 ndc0     = clip0.xyz / clip0.w;
    float3 ndc1     = clip1.xyz / clip1.w;
    float3 ndc2     = clip2.xyz / clip2.w;

    float2 pixelNdc = float2( 2.0f, -2.0f ) * ( float2( globalDispatchID.xy ) + 0.5f ) / pushBlock.vbuffRes
        + float2( -1.0f, 1.0f );

    float3 ndcBary = ComputeNDCBarycentrics( pixelNdc, ndc0.xy, ndc1.xy, ndc2.xy );

    // NOTE: Perspective-correct -- need per-vertex W
    float3 invW         = float3( 1.0f / clip0.w, 1.0f / clip1.w, 1.0f / clip2.w );
    float3 baryW        = ndcBary * invW;
    float3 perspBary    = baryW / ( baryW.x + baryW.y + baryW.z );

    device_ptr<oct16x2> vtxAttrBuff = { mltPayloadAddr + mltVtxStreamSizes.x };
    oct16x2 pkd_n0  = vtxAttrBuff[ vtxIDs.x ];
    oct16x2 pkd_n1  = vtxAttrBuff[ vtxIDs.y ];
    oct16x2 pkd_n2  = vtxAttrBuff[ vtxIDs.z ];

    float3 n0       = DecodeOctaNormal( float2( pkd_n0 & 0xffffu, ( pkd_n0 >> 16 ) & 0xffffu ) );
    float3 n1       = DecodeOctaNormal( float2( pkd_n1 & 0xffffu, ( pkd_n1 >> 16 ) & 0xffffu ) );
    float3 n2       = DecodeOctaNormal( float2( pkd_n2 & 0xffffu, ( pkd_n2 >> 16 ) & 0xffffu ) );

    float3 nInterp  = perspBary.x * n0 + perspBary.y * n1 + perspBary.z * n2;
    float3 N        = normalize( mul( float4( nInterp, 0.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ) ).xyz;

    float3 pInterp  = perspBary.x * p0 + perspBary.y * p1 + perspBary.z * p2;
    float3 worldPos = mul( float4( pInterp, 1.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz;

    float3 L        = normalize( float3( 0.5f, 1.0f, 0.3f ) );
    float3 V        = normalize( cam.worldPos - worldPos );   // view dir
    float  nDotL    = dot( N, L );

    // Warm-cool hemispheric (Gooch-ish)
    float3 warm     = float3( 0.55f, 0.52f, 0.48f );   // lit side
    float3 cool     = float3( 0.28f, 0.29f, 0.32f );   // shadow side
    float  t        = nDotL * 0.5f + 0.5f;             // [-1,1] -> [0,1]
    float3 col      = lerp( cool, warm, t );

    // Soft rim
    float  rim      = pow( 1.0f - saturate( dot( N, V ) ), 2.0f );
    col             += rim * 0.15f;

    // Broad soft spec (Blinn-Phong-ish, low exponent)
    float3 H        = normalize( L + V );
    float  spec     = pow( saturate( dot( N, H ) ), 16.0f );
    col += spec     * 0.1f;

    gRWTexture2D_float4[ pushBlock.dstIdx ][ globalDispatchID.xy ] = float4( col, 1.0f );
}