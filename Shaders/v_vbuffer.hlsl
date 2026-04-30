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
    draw_indexed_command draw = BufferLoad<draw_indexed_command>( pushBlock.drawBuffIdx, drawId );

    visible_meshlet mlt = BufferLoad<visible_meshlet>( pushBlock.visMltBuffIdx, draw.visMltIdx );

    gpu_instance inst = BufferLoad<gpu_instance>( pushBlock.instBuffIdx, mlt.globalInstId );
    float4x4 toWorld = TrsToFloat4x4( inst.toWorld.t, inst.toWorld.r, inst.toWorld.s );

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );
    float4x4 mvp = mul( toWorld, cam.mainViewProj );

    device_addr<packed_vtx> ptr = { gGlobData.vtxAddr };
    packed_vtx vtx = ptr[ vtxID ];
    float4 pos = mul( float4( vtx.px, vtx.py, vtx.pz, 1.0f ), mvp );

    vbuffer_vs_out vsOut = { pos, mlt.globalMltId, mlt.globalInstId };
    return vsOut;
}