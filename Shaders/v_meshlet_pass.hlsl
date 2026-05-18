#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"
#include "ht_hlsl_math.h"
#include "ht_dbg_common.h"

[[vk::push_constant]]
meshlet_pass_params pushBlock;


[shader( "vertex" )]
meshlet_vs_out MeshletPassVsMain(
    in u32 vtxID    : SV_VertexID,
    [[vk::builtin( "DrawIndex" )]]
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

    float3 n = DecodeOctaNormal( float2( vtx.octNX, vtx.octNY ) );
    float3 t = DecodeTanFromAngle( n, vtx.tanAngle );
    t = normalize( mul( float4( t, 0.0f ), toWorld ).xyz );
    n = normalize( mul( float4( n, 0.0f ), toWorld ).xyz );

    float3 worldPos = mul( float4( vtx.px, vtx.py, vtx.pz, 1.0f ), toWorld ).xyz;

    meshlet_vs_out vsOut = { pos, n, t, worldPos, bool( vtx.tanSign ) ? -1.0f : 1.0f };
    return vsOut;
}