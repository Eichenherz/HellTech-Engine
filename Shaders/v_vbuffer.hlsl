#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

#include "vbuffer.h"

[[vk::push_constant]]
vbuffer_params pushBlock;

[shader("vertex")]
vbuffer_vs_out VBufferVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin("DrawIndex")]]
    in u32 drawId   : DRAW_ID
) {
    draw_meshlet_command draw = BufferLoad<draw_meshlet_command>( pushBlock.drawBuffIdx, drawId );

    gpu_instance inst = BufferLoad<gpu_instance>( pushBlock.instBuffIdx, draw.globalInstId );
    float4x4 toWorld = float4x4(
        float4( inst.toWorld[ 0 ], 0.0f ),
        float4( inst.toWorld[ 1 ], 0.0f ),
        float4( inst.toWorld[ 2 ], 0.0f ),
        float4( inst.toWorld[ 3 ], 1.0f )
	);

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );
    float4x4 mvp = mul( toWorld, cam.mainViewProj );

    device_ptr<packed_vtx> pVtxBuff = { gGlobData.vtxAddr };
    packed_vtx vtx = pVtxBuff[ vtxID ];
    float4 pos = mul( float4( vtx.px, vtx.py, vtx.pz, 1.0f ), mvp );

    vbuffer_vs_out vsOut = { pos, draw.globalMltId, draw.globalInstId };
    return vsOut;
}