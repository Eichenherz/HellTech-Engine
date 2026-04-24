#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
meshlet_issue_draws_params pushBlock;

[numthreads(32, 1, 1)]
[shader("compute")]
void IssueMeshletDrawsCsMain( u32x3 globalDispatchID : SV_DispatchThreadID, u32 groupFlatIdx : SV_GroupIndex )
{
	u32 workCount = BufferLoad<u32>( pushBlock.workCountIdx, 0 );
    if( globalDispatchID.x >= workCount )
	{
		return;
	}

	u32 mltCount = BufferLoad<u32>( pushBlock.visMltCountIdx, 0 );
	// NOTE: bc we use the visible_meshlet buff as our primitives too we need to offset into it
    // mltCount >= workCount always !
	u32 mltOffset = mltCount - workCount;

    u32 waveDrawOffset = WaveActiveCountBits( true );
    u32 waveDrawBase = 0;
    if( WaveIsFirstLane() )
    {
        waveDrawBase = BufferAtomicAdd( pushBlock.drawCmdCounterIdx, waveDrawOffset );
    }
    waveDrawBase = WaveReadLaneFirst( waveDrawBase );

    u32 drawSlot = waveDrawBase + WavePrefixCountBits( true );

	u32 workItemIdx = mltOffset + globalDispatchID.x;
	visible_meshlet currentMeshlet = BufferLoad<visible_meshlet>( pushBlock.srcBufferIdx, workItemIdx );
	draw_indexed_command draw = { globalDispatchID.x, currentMeshlet.triCount * 3, 1,
		currentMeshlet.absTriOffset, currentMeshlet.absVtxOffset, 0 };

	BufferStore<draw_indexed_command>( pushBlock.drawCmdsBuffIdx, draw, drawSlot );
}