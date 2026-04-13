#include "ht_renderer_types.h"

//[[vk::constant_id(0)]]
static const uint WAVE_LANE_COUNT = 32;

static const uint NUM_WAVES_PER_WG = ( 16 * 16 ) / WAVE_LANE_COUNT;

static const float EPSILON = 0.005f;
// NOTE: taken from: https://bruop.github.io/exposure/
static const float3 RGB_TO_LUM = float3( 0.2125f, 0.7154f, 0.0721f );

[[vk::push_constant]]
struct {
	avg_luminance_info	avgLumInfo;
	uint				hdrColSrcIdx;
	uint				globalScratchpadIdx;
	uint				atomicWgCounterIdx;
	uint				avgLumIdx;
} pushBlock;

static const uint HISTO_BIN_MIN_CUTOFF = 128;
static const uint HISTO_BIN_MAX_CUTOFF = 230;

uint HdrToHistogramBin( float3 hdrRgb )
{
	float lum = dot( hdrRgb, RGB_TO_LUM );
	// NOTE: avoid log2( 0 )
	if( lum < EPSILON )
	{
		return 0;
	}
	
	float logLum = clamp( ( log2( lum ) - pushBlock.avgLumInfo.minLogLum ) * pushBlock.avgLumInfo.invLogLumRange, 0.0f, 1.0f );
	return uint( logLum * 254.0f + 1.0f );
}

groupshared uint partialSumLDS[ NUM_WAVES_PER_WG ];
groupshared uint tailValsCountLDS[ NUM_WAVES_PER_WG ];

groupshared uint ldsGroupCounter;

NUMTHREADS( 16, 16, 1 )
[WaveSize( WAVE_LANE_COUNT )]
[shader( "compute" )]
void AvgLuminanceCsMain( 
	uint3 globalThreadDispatchID : SV_DispatchThreadID, 
	uint groupFlatIdx : SV_GroupIndex
) {
	Texture2D<float4> hdrColTex = gTexture2D_float4[ pushBlock.hdrColSrcIdx ];
	
	uint2 hdrColTrgSize;
	hdrColTex.GetDimensions( hdrColTrgSize.x, hdrColTrgSize.y );
	
	if( all( globalThreadDispatchID.xy < hdrColTrgSize ) )
	{
		float3 hdrCol = hdrColTex.Load( int3( globalThreadDispatchID.xy, 0 ) ).xyz;
		uint histoBinIdx = HdrToHistogramBin( hdrCol );

		const bool discardPixel = ( histoBinIdx < HISTO_BIN_MIN_CUTOFF ) || ( histoBinIdx > HISTO_BIN_MAX_CUTOFF );
		histoBinIdx *= uint( !discardPixel );

		uint partialLumSum = WaveActiveSum( histoBinIdx );
		uint tailValsPartialCount = ( uint ) countbits( WaveActiveBallot( !bool( histoBinIdx ) ) );

		if( WaveGetLaneIndex() == 0 )
		{
			partialSumLDS[ WAVE_ID_WITHIN_WG ] = partialLumSum;
			tailValsCountLDS[ WAVE_ID_WITHIN_WG ] = tailValsPartialCount;
		}
	}
	
	GroupMemoryBarrierWithGroupSync();
	if( WAVE_ID_WITHIN_WG == 0 )
	{
		uint waveLaneIdx = WaveGetLaneIndex();
		uint lumSumLDS = ( waveLaneIdx < WAVE_COUNT_PER_WG ) ? partialSumLDS[ waveLaneIdx ] : 0u;
		lumSumLDS = WaveActiveSum( lumSumLDS );

		uint tailValsLDS = ( waveLaneIdx < WAVE_COUNT_PER_WG ) ? tailValsCountLDS[ waveLaneIdx ] : 0u;
		tailValsLDS = WaveActiveSum( tailValsLDS );
		if( WaveGetLaneIndex() == 0 )
		{
			uint scratchPad;
			storageBuffers[ pushBlock.globalScratchpadIdx ].InterlockedAdd( 0, lumSumLDS, scratchPad );
			storageBuffers[ pushBlock.globalScratchpadIdx ].InterlockedAdd( 4, tailValsLDS, scratchPad );
		}
	}
	
	DeviceMemoryBarrier();
	
	if( groupFlatIdx == 0 )
	{
		storageBuffers[ pushBlock.atomicWgCounterIdx ].InterlockedAdd( 0, 1, ldsGroupCounter );
	}

	GroupMemoryBarrierWithGroupSync();
	
	if( ldsGroupCounter != ( WORK_GROUP_COUNT.x + WORK_GROUP_COUNT.y - 1 ) )
	{
		return;
	}
	
	if( groupFlatIdx == 0 )
	{
		uint finalLumSum = storageBuffers[ pushBlock.globalScratchpadIdx ].Load<uint>( 0 );
		uint finalTailValsCount = storageBuffers[ pushBlock.globalScratchpadIdx ].Load<uint>( 4 );
		
		float numPixels = hdrColTrgSize.x * hdrColTrgSize.y;
		float numPixelsAfterTailDiscard = max( numPixels - float( finalTailValsCount ), 1.0f );
		float weightedLogAverage = ( float( finalLumSum ) / numPixelsAfterTailDiscard ) - 1.0f;
			
		float invLogLumRange = pushBlock.avgLumInfo.invLogLumRange;
		float weightedAvgLum = exp2( ( ( weightedLogAverage / 254.0f ) * invLogLumRange ) + pushBlock.avgLumInfo.minLogLum );

		float avgLuminance = storageBuffers[ pushBlock.avgLumIdx ].Load<float>( 0 );
		float adaptedLum = avgLuminance + ( weightedAvgLum - avgLuminance ) * pushBlock.avgLumInfo.dt;
		
		storageBuffers[ pushBlock.avgLumIdx ].Store( 0, adaptedLum );
	}
}