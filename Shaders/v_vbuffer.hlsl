#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"

#include "vbuffer.h"

[[vk::push_constant]]
vbuffer_params pushBlock;

[shader("vertex")]
vbuffer_vs_out VBufferVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin("DrawIndex")]]
    in u32 drawId   : DRAW_ID
) {
    device_addr<packed_vtx> ptr = { gGlobData.vtxAddr };
    packed_vtx thisVtx = ptr[ vtxID ];

    draw_command draw = BufferLoad<draw_command>( pushBlock.drawBuffIdx, drawId );

    instance_desc currentInst = BufferLoad<instance_desc>( pushBlock.instBuffIdx, draw.instIdx );
    float4x4 toWorld = TrsToFloat4x4( currentInst.toWorld.t, currentInst.toWorld.r, currentInst.toWorld.s );

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );

    float4x4 mvp = mul( toWorld, mul( cam.mainView, cam.proj ) );
    float4 pos = mul( float4( thisVtx.pos, 1.0f ), mvp );

    vbuffer_vs_out vsOut = { pos, draw.instIdx, draw.firstIndex };
    return vsOut;
}