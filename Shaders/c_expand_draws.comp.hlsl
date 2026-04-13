#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
draw_expansion_params pushBlock;

[numthreads(32, 1, 1)]
[shader("compute")]
void ExpandDrawsCsMain( u32x3 globalDispatchID : SV_DispatchThreadID, u32 groupFlatIdx : SV_GroupIndex )
{
    if( globalDispatchID.x >= pushBlock.drawsCount )
	{
		return;
	}

    visible_instance thisVisInstance = BufferLoad<visible_instance>( pushBlock.srcBufferIdx, globalDispatchID.x );
    u32 meshletOffset = thisVisInstance.meshletOffset;
    u32 meshletCount = thisVisInstance.meshletCount;
    u32 laneOffset = WavePrefixSum( meshletCount );
    u32 waveTotal = WaveActiveSum( meshletCount );
    u32 waveBase = 0;
    if( WaveIsFirstLane() )
    {
        waveBase = BufferAtomicAdd( pushBlock.counterIdx, waveTotal );
    }
    
    waveBase = WaveReadLaneFirst( waveBase );
    uint globalOffset = waveBase + laneOffset;

    for( uint mlti = 0; mlti < meshletCount; ++mlti )
    {
        device_addr<gpu_meshlet> ptr = { gGlobData.mltAddr };
        gpu_meshlet currentMeshlet = ptr[ mlti + meshletOffset ];

        visible_meshlet visMeshlet = {
            thisVisInstance.instId,
            currentMeshlet.vtxOffset,
            currentMeshlet.triOffset,
            currentMeshlet.vtxCount,
            currentMeshlet.triCount,
            0 // padding
        };

        uint slotIdx = globalOffset + mlti;
        BufferStore<visible_meshlet>( pushBlock.dstBufferIdx, visMeshlet, slotIdx );
    }
}