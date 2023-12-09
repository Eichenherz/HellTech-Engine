#version 460

#extension GL_GOOGLE_include_directive : require

#define BINDLESS
#include "..\r_data_structs.h"

layout( push_constant ) uniform block
{
	vec2 imgSize;
	uint samplerIdx;

	uint inSampledImgIdx;
	uint outImgIdx;
};

layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;
void main()
{
	uvec2 pos = gl_GlobalInvocationID.xy;

	// NOTE: this computes the minimum depth of a 2x2 texel quad
	float depth = texture( sampler2D( sampledImages[ nonuniformEXT( inSampledImgIdx ) ], samplers[samplerIdx] ), 
		( vec2( pos ) + vec2( 0.5f ) ) / imgSize ).x;

	imageStore( storageImages[ nonuniformEXT( outImgIdx ) ], ivec2( pos ), vec4( depth ) );
}
