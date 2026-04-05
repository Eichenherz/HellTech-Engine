[[vk::push_constant]]
expand_params pushBlock;

[numthreads(32, 1, 1)]
[shader("compute")]
void ExpandDrawsCsMain( uint3 globalDispatchID : SV_DispatchThreadID, uint groupFlatIdx : SV_GroupIndex )
{
    if( globalDispatchID.x >= pushBlock.instCount )
	{
		return;
	}

    gpu_mesh currentMesh = BufferLoad<gpu_mesh>( pushBlock.srcBufferIdx, globalDispatchID.x );
    uint meshletCount = currentMesh.meshletCount;
    uint laneOffset = WavePrefixSum( meshletCount );
    uint waveTotal = WaveActiveSum( meshletCount );
    uint waveBase = 0;
    if( WaveIsFirstLane() )
    {
        waveBase = BufferAtomicAdd( pushBlock.counterIdx, waveTotal );
    }
    
    waveBase = WaveReadLaneFirst( waveBase );
    uint globalOffset = waveBase + laneOffset;

    for( uint mlti = 0; mlti < meshletCount; ++mlti )
    {
        uint slotIdx = globalOffset + mlti;
        meshlet currentMeshlet = BufferLoad<meshlet>( currentMesh.hMeshletBuffer, mlti );
        BufferStore<meshlet>( pushBlock.dstBufferIdx, currentMeshlet, slotIdx );
    }
}