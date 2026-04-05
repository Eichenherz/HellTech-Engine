[[vk::push_constant]]
meshlet_issue_draws_params pushBlock;

[numthreads(32, 1, 1)]
[shader("compute")]
void ExpandDrawsCsMain( uint3 globalDispatchID : SV_DispatchThreadID, uint groupFlatIdx : SV_GroupIndex )
{
    if( globalDispatchID.x >= pushBlock.instCount )
	{
		return;
	}

    meshlet currentMeshlet = BufferLoad<meshlet>( pushBlock.srcBufferIdx, globalDispatchID.x );
    uint mletIdxCount = currentMeshlet.triCount * 3;

    uint waveDrawOffset = WaveActiveCountBits( true );
    uint waveDrawBase = 0;
    if( WaveIsFirstLane() )
    {
        waveDrawBase = BufferAtomicAdd( pushBlock.drawCmdCounterIdx, waveDrawOffset );
    }
    
    waveDrawBase = WaveReadLaneFirst( waveDrawBase );
    uint drawSlot = waveDrawBase + WavePrefixCountBits( true );
    // write draw

    uint laneOffset = WavePrefixSum( mletIdxCount );
    uint waveTotal = WaveActiveSum( mletIdxCount );
    uint waveIndexBase = 0;
    if( WaveIsFirstLane() )
    {
        waveIndexBase = BufferAtomicAdd( pushBlock.idxCounterIdx, waveTotal );
        // TODO: clamp and write to host that we dropped some triangles so we can adjust
    }
    
    waveIndexBase = WaveReadLaneFirst( waveIndexBase );
    uint globalOffset = waveIndexBase + laneOffset;

    for( uint mlti = 0; mlti < mletIdxCount; ++mlti )
    {
        uint slotIdx = globalOffset + mlti;
        meshlet currentMeshlet = BufferLoad<meshlet>( currentMesh.hMeshletBuffer, mlti );
        BufferStore<meshlet>( pushBlock.dstBufferIdx, currentMeshlet, slotIdx );
    }
}