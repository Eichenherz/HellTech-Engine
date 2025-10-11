#define MAX_DESCRIPTOR_COUNT 0xFFFF

// NOTE: taken from vulkanised_2023_setting_up_a_bindless_rendering_pipeline 
#define ITERATE_TEXTURE_TYPES(GENERATOR, ...) \
	GENERATOR( int, ##__VA_ARGS__ ) \
	GENERATOR( uint, ##__VA_ARGS__ ) \
	GENERATOR( float, ##__VA_ARGS__ ) \
	GENERATOR( int2, ##__VA_ARGS__ ) \
	GENERATOR( uint2, ##__VA_ARGS__ ) \
	GENERATOR( float2, ##__VA_ARGS__ ) \
	GENERATOR( int3, ##__VA_ARGS__ ) \
	GENERATOR( uint3, ##__VA_ARGS__ ) \
	GENERATOR( float3, ##__VA_ARGS__ ) \
	GENERATOR( int4, ##__VA_ARGS__ ) \
	GENERATOR( uint4, ##__VA_ARGS__ ) \
	GENERATOR( float4, ##__VA_ARGS__ ) 

#define TEXURE_TYPE_SLOT_GENERATOR( native_type, texture_type, slot ) \
	[[vk::binding( slot )]] texture_type<native_type> g##texture_type##_##native_type[ MAX_DESCRIPTOR_COUNT ];

#define DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( texture_type, slot ) \
   ITERATE_TEXTURE_TYPES(TEXURE_TYPE_SLOT_GENERATOR, texture_type, slot)

[[vk::binding( 0 )]] SamplerState samplers[MAX_DESCRIPTOR_COUNT];
[[vk::binding( 1 )]] ByteAddressBuffer storageBuffers[MAX_DESCRIPTOR_COUNT];
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( RWTexture2D, 2 ) 
DEFINE_TEXTURE_TYPES_AND_FORMAT_SLOTS( Texture2D, 3 ) 