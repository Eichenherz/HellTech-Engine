#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_unpacking.h"

[[vk::push_constant]]
depth_prepass_params pushBlock;

[shader( "vertex" )]
float4 DepthPrepassVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin("DrawIndex")]]
    in u32 drawId   : DRAW_ID
) : SV_Position {

    draw_meshlet_cmd_data drawData = BufferLoad<draw_meshlet_cmd_data>( pushBlock.drawDataIdx, drawId );

    gpu_meshlet mlt = device_ptr<gpu_meshlet>( gGlobData.mltAddr )[ drawData.globalMltId ];
    float3 pos = DecodeVertexFromMegaBuff( mlt, drawData.vtxPosOffsetInBits, vtxID );

    gpu_instance inst = BufferLoad<gpu_instance>( pushBlock.instBuffIdx, drawData.globalInstId );
    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );

    return mul( float4( pos, 1.0f ), mul( f4x3_To_f4x4_Affine( inst.toWorld ), cam.mainViewProj ) );
}