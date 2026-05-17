#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

#define WG_SIZE 128

[[vk::push_constant]]
draw_expansion_params           pushBlock;

groupshared u32                 ldsWgOffset;
groupshared u32                 ldsWgMltTotalCount;

groupshared visible_instance    ldsWgVisInst[ WG_SIZE ];
groupshared u32                 ldsWgThreadMltOffset[ WG_SIZE ];

groupshared u32                 ldsWavePrefix[ WG_SIZE / 32 ]; // TODO: don't hadrcode

// NOTE: upper bound search
template<u32 N>
u32 FindSrcLane( groupshared u32 ldsThreadOffsets[ N ], u32 slotIdx )
{
    u32 lo = 0;
    [unroll] for( u32 stepSz = WG_SIZE / 2; stepSz > 0; stepSz >>= 1 )
    {
        u32 probe = lo + stepSz;
        bool take = ( probe < WG_SIZE ) && ( ldsThreadOffsets[ probe ] <= slotIdx );
        lo = take ? probe : lo;
    }
    return lo;
}
// TODO: revisit this
[numthreads( WG_SIZE, 1, 1 )]
[shader( "compute" )]
void ExpandDrawsCsMain( u32x3 globalDispatchID : SV_DispatchThreadID, u32 groupFlatIdx : SV_GroupIndex )
{
	if( 0 == groupFlatIdx )
	{
		ldsWgOffset = 0;
		ldsWgMltTotalCount = 0;
	}
	GroupMemoryBarrierWithGroupSync();

	u32 workItems = BufferLoad<u32>( pushBlock.workCounterIdxConst, 0 );
	visible_instance thisVisInstance = { 0, 0, 0, 0, 0 };
    if( globalDispatchID.x < workItems )
	{
		thisVisInstance = BufferLoad<visible_instance>( pushBlock.srcBufferIdx, globalDispatchID.x );
	}
	ldsWgVisInst[ groupFlatIdx ] = thisVisInstance;

	u32 waveMltOffset = WavePrefixSum( thisVisInstance.meshletCount );
    u32 waveMltTotal = WaveActiveSum( thisVisInstance.meshletCount );
    if( WaveIsFirstLane() )
    {
        ldsWavePrefix[ groupFlatIdx / WaveGetLaneCount() ] = waveMltTotal;
    }
    GroupMemoryBarrierWithGroupSync();

    if( 0 == groupFlatIdx )
    {
        u32 total = 0;
        [unroll] for( u32 wi = 0; wi < HT_WAVE_COUNT_PER_WG; ++wi )
        {
            total += ldsWavePrefix[ wi ];
        }

        ldsWgMltTotalCount = total;
        ldsWgOffset = BufferAtomicAdd( pushBlock.expandedItemsCountIdx, ldsWgMltTotalCount );


        u32 exclusivePrefix = 0;
        [unroll] for( u32 wi = 0; wi < HT_WAVE_COUNT_PER_WG; ++wi )
        {
            u32 currentWaveTotal = ldsWavePrefix[ wi ];
            ldsWavePrefix[ wi ] = exclusivePrefix;
            exclusivePrefix += currentWaveTotal;
        }
    }
    GroupMemoryBarrierWithGroupSync();


    ldsWgThreadMltOffset[ groupFlatIdx ] = ldsWavePrefix[ groupFlatIdx / WaveGetLaneCount() ] + waveMltOffset;
    GroupMemoryBarrierWithGroupSync();

	device_addr<gpu_meshlet> pMlts = { gGlobData.mltAddr };
	// NOTE: coop write
	for( u32 perWgMltId = groupFlatIdx; perWgMltId < ldsWgMltTotalCount; perWgMltId += WG_SIZE )
	{
	    u32 srcThreadId  = FindSrcLane( ldsWgThreadMltOffset, perWgMltId );
	    u32 localMlt = perWgMltId - ldsWgThreadMltOffset[ srcThreadId ];

        visible_instance visInst = ldsWgVisInst[ srcThreadId ];
        u32 globalMltId = visInst.meshletOffset + localMlt;
        gpu_meshlet mlt = pMlts[ globalMltId ];

        visible_meshlet visMlt = {
            mlt.minAabb,
            mlt.maxAabb,
            visInst.instId,
            globalMltId,
            mlt.triCount,
            mlt.vtxOffset + visInst.vtxOffset,
            mlt.triOffset + visInst.triOffset
        };

        u32 writeSlotIdx = ldsWgOffset + perWgMltId;
        BufferStore<visible_meshlet>( pushBlock.expandedItemsBuffIdx, visMlt, writeSlotIdx );
	}
}