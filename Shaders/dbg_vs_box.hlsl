#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
dbg_box_params pushBlock;

[shader("vertex")]
void DbgBoxVsMain(
    in  u32     vtxId   : SV_VertexID,
    in  u32     instId  : SV_InstanceID,
    out float4  posOut  : SV_Position,
    out float4  colOut  : COLOR0
)
{
    dbg_aabb_instance dbgInst = device_addr<dbg_aabb_instance>( pushBlock.instBuffAddr )[ instId ];

    // NOTE: aabb must be in [-0.5f, 0.5f]
    float3 center = ( dbgInst.maxAabb + dbgInst.minAabb ) * 0.5f;
    float3 extent = dbgInst.maxAabb - dbgInst.minAabb;

    float4x4 aabbTransf = float4x4(
        extent.x, 0.0f,     0.0f,     0.0f,
        0.0f,     extent.y, 0.0f,     0.0f,
        0.0f,     0.0f,     extent.z, 0.0f,
        center.x, center.y, center.z, 1.0f
    );

    view_data cam = BufferLoad<view_data>( pushBlock.camIdx );

    float4x4 mvp = mul( mul( aabbTransf, dbgInst.toWorld ), cam.mainViewProj );

    dbg_vertex dbgVtx = device_addr<dbg_vertex>( pushBlock.vtxBuffAddr )[ vtxId ];

    float4 pos = mul( float4( dbgVtx.pos, 1.0f ), mvp );

    posOut = pos;
    colOut = dbgInst.color;
}