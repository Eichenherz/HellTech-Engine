
[[vk::push_constant]]
struct {
	float2 scale;
	float2 translate;
} pushBlock;

struct imgui_vertex
{
	float x, y;
	float u, v;
	uint rgba8Unorm;
};

[[vk::binding( 0 )]] StructuredBuffer<imgui_vertex> uiVtxBuff;
[[vk::binding( 1 )]] Texture2D fontAtals;
[[vk::binding( 2 )]] SamplerState fontSampler;


///struct vs_out
///{
///[[vk::location( 0 )]] float4 col : COLOR0;
///[[vk::location( 1 )]] float2 uv : TEXCOORD0;
///};

[ shader( "vertex" ) ]
void VsMain( 
	in uint vtxID : SV_VertexID,
	out float4 pos : SV_Position,
	[[vk::location( 0 )]] 
	out float4 col : COLOR0,
	[[vk::location( 1 )]] 
	out float2 uv : TEXCOORD0
){
	imgui_vertex uiVtxAttrs = uiVtxBuff[ vtxID ];
	
	pos = float4( float2( uiVtxAttrs.x, uiVtxAttrs.y ) * pushBlock.scale + pushBlock.translate, 0.0f, 1.0f );
	col = unpack_u8u32(  uiVtxAttrs.rgba8Unorm ).xyzw;
	uv = float2( uiVtxAttrs.u, uiVtxAttrs.v );
}

[ shader( "pixel" ) ]
float4 PsMain( 
	[[vk::location( 0 )]] 
	in float4 col : COLOR0,
	[[vk::location( 1 )]] 
	in float2 uv : TEXCOORD0
) : SV_Target
{
	return col * fontAtals.Sample( fontSampler, uv );
}