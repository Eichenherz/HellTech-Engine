#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
depth_prepass_params pushBlock;

[shader( "vertex" )]
float4 DepthPrepassVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin("DrawIndex")]]
    in u32 drawId   : DRAW_ID
) : SV_Position {
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

    device_addr<packed_vtx> pVtxBuff = { gGlobData.vtxAddr };
    packed_vtx vtx = pVtxBuff[ vtxID ];
    return mul( float4( vtx.px, vtx.py, vtx.pz, 1.0f ), mvp );
}