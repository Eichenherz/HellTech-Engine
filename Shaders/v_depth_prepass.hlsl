#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_unpacking.h"

[[vk::push_constant]]
depth_prepass_params pushBlock;

[shader( "vertex" )]
// NOTE: we need precise here and on the meshlet pass otherwise we'll get tears due to mis match
// only needed when doing depth prepass tho
precise float4 DepthPrepassVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin("DrawIndex")]]
    in u32 drawId   : DRAW_ID
) : SV_Position {

    draw_meshlet_cmd_data drawData = device_ptr<draw_meshlet_cmd_data>( pushBlock.drawDataAddr )[ drawId ];

    gpu_meshlet mlt = device_ptr<gpu_meshlet>( g_GlobalData.mltAddr )[ drawData.globalMltId ];
    u64 localPayloadAddr = g_GlobalData.pldAddr + drawData.meshPayloadStartInBytes + mlt.offsetInPayload;
    float3 pos = DecodeVertexFromMegaBuff( mlt, device_ptr<u32>( localPayloadAddr ), vtxID );

    gpu_instance inst = device_ptr<gpu_instance>( g_GlobalData.instAddr )[ drawData.globalInstId ];
    view_data cam = device_ptr<view_data>( g_GlobalData.viewsAddr )[ g_GlobalData.mainCamIdx ];

    return mul( float4( pos, 1.0f ), mul( f4x3_To_f4x4_Affine( inst.toWorld ), cam.mainViewProj ) );
}