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
    draw_meshlet_cmd_data drawData = BufferLoad<draw_meshlet_cmd_data>( pushBlock.drawDataIdx, drawId );

    gpu_instance inst = BufferLoad<gpu_instance>( pushBlock.instBuffIdx, drawData.globalInstId );
    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );

    gpu_meshlet mlt = device_ptr<gpu_meshlet>( gGlobData.mltAddr )[ drawData.globalMltId ];
    float3 pos = DecodeVertexFromMegaBuff( mlt, drawData.vtxPosOffsetInBits, vtxID );

    device_ptr<packed_vtx_attr> pVtxBuff = { gGlobData.vtxAttrsAddr };
    packed_vtx_attr vtx = pVtxBuff[ vtxID + drawData.vtxAttrOffset + mlt.vtxAttrsOffset ];

    unpacked_tbn tbn = UnpackTBN( vtx.encodedTBN );
    float3 n = DecodeOctaNormal( float2( tbn.octNx, tbn.octNy ) );
    float3 t = DecodeTanFromAngle( n, tbn.tanAngle );

    meshlet_vs_out vsOut = ( meshlet_vs_out ) 0;
    vsOut.pos       = mul( float4( pos, 1.0f ), mul( f4x3_To_f4x4_Affine( inst.toWorld ), cam.mainViewProj ) );
    vsOut.n         = normalize( mul( float4( n, 0.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz );
    vsOut.t         = normalize( mul( float4( t, 0.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz );
    vsOut.wrldPos   = mul( float4( pos, 1.0f ), f4x3_To_f4x4_Affine( inst.toWorld ) ).xyz;
    vsOut.tanSgn    = tbn.bitanSign;
    return vsOut;
}