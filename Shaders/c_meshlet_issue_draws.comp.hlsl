#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
meshlet_issue_draws_params pushBlock;

[numthreads(32, 1, 1)]
[shader("compute")]
void IssueMeshletDrawsCsMain( u32x3 globalDispatchID : SV_DispatchThreadID, u32 groupFlatIdx : SV_GroupIndex )
{
    if( globalDispatchID.x >= pushBlock.mltCount )
	{
		return;
	}

    u32 waveDrawOffset = WaveActiveCountBits( true );
    u32 waveDrawBase = 0;
    if( WaveIsFirstLane() )
    {
        waveDrawBase = BufferAtomicAdd( pushBlock.drawCmdCounterIdx, waveDrawOffset );
    }
    
    waveDrawBase = WaveReadLaneFirst( waveDrawBase );
    u32 drawSlot = waveDrawBase + WavePrefixCountBits( true );

	visible_meshlet currentMeshlet = BufferLoad<visible_meshlet>( pushBlock.srcBufferIdx, globalDispatchID.x );
	draw_command draw = { currentMeshlet.instId, currentMeshlet.triCount * 3, 1,
		currentMeshlet.triOffset, currentMeshlet.vtxOffset, 0 };

	BufferStore<draw_command>( pushBlock.drawCmdsBuffIdx, draw, drawSlot );
}