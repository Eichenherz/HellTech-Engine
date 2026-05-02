#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
draw_expansion_params pushBlock;

[numthreads(32, 1, 1)]
[shader("compute")]
void ExpandDrawsCsMain( u32x3 globalDispatchID : SV_DispatchThreadID, u32 groupFlatIdx : SV_GroupIndex )
{
	u32 workItems = BufferLoad<u32>( pushBlock.workCounterIdxConst, 0 );
    if( globalDispatchID.x >= workItems )
	{
		return;
	}

    visible_instance thisVisInstance = BufferLoad<visible_instance>( pushBlock.srcBufferIdx, globalDispatchID.x );
    u32 meshletCount = thisVisInstance.meshletCount;

    u32 laneOffset = WavePrefixSum( meshletCount );
    u32 waveTotal = WaveActiveSum( meshletCount );
    u32 waveBase = 0;
    if( WaveIsFirstLane() )
    {
        waveBase = BufferAtomicAdd( pushBlock.visMltCounterIdxIdx, waveTotal );
    }
    waveBase = WaveReadLaneFirst( waveBase );

    u32 globalOffset = waveBase + laneOffset;

    device_addr<gpu_meshlet> ptr = { gGlobData.mltAddr };

    for( u32 mlti = 0; mlti < meshletCount; ++mlti )
    {
        u32 globalMltId = mlti + thisVisInstance.meshletOffset;

        gpu_meshlet currentMeshlet = ptr[ globalMltId ];

        visible_meshlet visMeshlet = {
            thisVisInstance.instId,
            globalMltId,
            currentMeshlet.vtxOffset + thisVisInstance.vtxOffset,
            currentMeshlet.triOffset + thisVisInstance.triOffset,
            currentMeshlet.vtxCount,
            currentMeshlet.triCount * 3 // NOTE: * 3 bc it's an idx count
        };

        u32 slotIdx = globalOffset + mlti;
        BufferStore<visible_meshlet>( pushBlock.visMltBufferIdx, visMeshlet, slotIdx );
    }
}