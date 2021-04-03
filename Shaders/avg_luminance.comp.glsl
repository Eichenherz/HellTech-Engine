#version 460


#extension GL_EXT_control_flow_attributes : require

#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require
#extension GL_KHR_shader_subgroup_shuffle : require

#extension GL_GOOGLE_include_directive : require

#include "..\r_data_structs.h"

layout( push_constant ) uniform block
{
	avg_luminance_info avgLumInfo;
};

layout( binding = 0, rgba32f ) readonly uniform coherent image2D hdrColTrg;
layout( binding = 1 ) buffer avg_luminance
{
	float avgLuminance;
};
layout( binding = 2 ) buffer global_vars
{
	uint finalLumSum;
	uint finalTooDarkPixelsCount;
};
layout( binding = 3 ) buffer global_sync_counter
{
	uint globSyncCounter;
};

layout( constant_id = 0 ) const uint SubgroupSize = 32;


shared uint partialSumLDS[ SubgroupSize ];
shared uint tooDarkPixelsCountLDS[ SubgroupSize ];

#define EPSILON 0.005
// NOTE: taken from: https://bruop.github.io/exposure/
#define RGB_TO_LUM vec3( 0.2125, 0.7154, 0.0721 )

layout( local_size_x = 16, local_size_y = 16, local_size_z = 1 ) in;
void main()
{
	uvec2 hdrColTrgSize = imageSize( hdrColTrg ).xy;
	if( gl_GlobalInvocationID.x > hdrColTrgSize.x || gl_GlobalInvocationID.y > hdrColTrgSize.y ) return;

	if( gl_GlobalInvocationID.x == 0 && gl_GlobalInvocationID.y == 0 )
	{
		finalLumSum = 0;
		finalTooDarkPixelsCount = 0;
		globSyncCounter = 0;
	}

	uint histoBinIdx = 0;

	vec3 hdrCol = imageLoad( hdrColTrg, ivec2( gl_GlobalInvocationID.xy ) ).xyz;
	float lum = dot( hdrCol, RGB_TO_LUM );
	// NOTE: avoid log2( 0 )
	if( lum > EPSILON )
	{
		float logLum = clamp( ( log2( lum ) - avgLumInfo.minLogLum ) * avgLumInfo.invLogLumRange, 0.0, 1.0 );
		histoBinIdx = uint( logLum * 254.0 + 1.0 );
	}

	uint tooDarkPixelsPartialCount = gl_SubgroupSize - subgroupBallotBitCount( subgroupBallot( bool( histoBinIdx ) ) );
	uint partialLumSum = subgroupAdd( histoBinIdx );

	if( gl_SubgroupInvocationID == 0 )
	{
		partialSumLDS[ gl_SubgroupID ] = partialLumSum;
		tooDarkPixelsCountLDS[ gl_SubgroupID ] = tooDarkPixelsPartialCount;
	}

	barrier();

	if( gl_SubgroupID == 0 )
	{
		partialLumSum =
			gl_SubgroupInvocationID < gl_NumSubgroups ? partialSumLDS[ gl_SubgroupInvocationID ] : 0;
		partialLumSum = subgroupAdd( partialLumSum );

		tooDarkPixelsPartialCount =
			gl_SubgroupInvocationID < gl_NumSubgroups ? tooDarkPixelsCountLDS[ gl_SubgroupInvocationID ] : 0;
		tooDarkPixelsPartialCount = subgroupAdd( tooDarkPixelsPartialCount );
	}

	if( gl_LocalInvocationIndex == 0 )
	{
		atomicAdd( finalLumSum, partialLumSum );
		atomicAdd( finalTooDarkPixelsCount, tooDarkPixelsPartialCount );
		atomicAdd( globSyncCounter, 1 );
	}

	barrier();

	if( globSyncCounter == ( gl_NumWorkGroups.x + gl_NumWorkGroups.y - 1 ) )
	{
		uint numPixels = hdrColTrgSize.x * hdrColTrgSize.y;
		float weightedLogAverage = 
			( float( finalLumSum ) / max( numPixels - float( finalTooDarkPixelsCount ), 1.0 ) ) - 1.0;

		float logLumRange = avgLumInfo.invLogLumRange;
		float weightedAvgLum = exp2( ( ( weightedLogAverage / 254.0 ) * logLumRange ) + avgLumInfo.minLogLum );

		float adaptedLum = avgLuminance + ( weightedAvgLum - avgLuminance ) * avgLumInfo.dt;
		avgLuminance = adaptedLum;
	}
}