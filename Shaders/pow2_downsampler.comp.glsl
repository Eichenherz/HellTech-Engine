#version 460

//#extension GL_GOOGLE_include_directive : require
//layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_BUFFER ) readonly buffer device_addrs{ uint64_t deviceAddrs[]; };
//layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_SAMPLED_IMAGE ) uniform texture2D sampledImages[];
//layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_SAMPLER ) uniform sampler samplers[];
//layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_IMAGE, r32f ) writeonly uniform coherent image2D depthViews[];
//layout( set = VK_GLOBAL_DESC_SET, binding = VK_GLOBAL_SLOT_STORAGE_IMAGE, rgba8 ) writeonly uniform coherent image2D swapchainViews[];



layout( push_constant ) uniform block
{
	vec2 imgSize;
};

layout( binding = 0 ) uniform writeonly image2D outImage;
layout( binding = 1 ) uniform sampler2D inImage;

layout( local_size_x = 32, local_size_y = 32, local_size_z = 1 ) in;
void main()
{
	uvec2 pos = gl_GlobalInvocationID.xy;

	//vec2 imgSize = imageSize( outImage, 
	// NOTE: this computes the minimum depth of a 2x2 texel quad
	float depth = texture( inImage, ( vec2( pos ) + vec2( 0.5f ) ) / imgSize ).x;

	imageStore( outImage, ivec2( pos ), vec4( depth ) );
}
