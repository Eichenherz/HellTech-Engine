#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_dbg_common.h"
#include "ht_unpacking.h"

[[vk::push_constant]]
meshlet_pass_params pushBlock;


[shader( "vertex" )]
meshlet_vs_out MeshletPassVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin( "DrawIndex" )]]
    in u32 drawId   : DRAW_ID
) {
    draw_meshlet_cmd_data drawData = device_ptr<draw_meshlet_cmd_data>( pushBlock.drawDataAddr )[ drawId ];

    gpu_instance inst = device_ptr<gpu_instance>( g_GlobalData.instAddr )[ drawData.globalInstId ];
    view_data cam = device_ptr<view_data>( g_GlobalData.viewsAddr )[ g_GlobalData.mainCamIdx ];

    gpu_meshlet mlt = device_ptr<gpu_meshlet>( g_GlobalData.mltAddr )[ drawData.globalMltId ];
    u64 localPayloadAddr = g_GlobalData.pldAddr + drawData.meshPayloadStartInBytes + mlt.offsetInPayload;
    float3 pos = DecodeVertexFromMegaBuff( mlt, device_ptr<u32>( localPayloadAddr ), vtxID );

    u32x2 mltVtxStreamSizes = GPUMeshletComputeVtxStreamSizes( mlt );
    u64 normalsOffset = drawData.meshPayloadStartInBytes + mlt.offsetInPayload + mltVtxStreamSizes.x;
    oct16x2 encN = device_ptr<oct16x2>( g_GlobalData.pldAddr + normalsOffset )[ vtxID ];

    float3 n = DecodeOctaNormal( float2( RestoreSign( encN & 0xffff, 16 ), RestoreSign( encN >> 16, 16 ) ) / 32767.0f );
    //float3 t = DecodeTanFromAngle( n, tbn.tanAngle );

    meshlet_vs_out vsOut = ( meshlet_vs_out ) 0;
    vsOut.pos       = mul( float4( pos, 1.0f ), mul( f4x3_To_f4x4_Affine( inst.toWorld ), cam.mainViewProj ) );
    vsOut.n         = normalize( mul( float4( n, 0.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz );
    //vsOut.t         = normalize( mul( float4( t, 0.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz );
    vsOut.wrldPos   = mul( float4( pos, 1.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz;
    //vsOut.tanSgn    = tbn.bitanSign;
    return vsOut;
}