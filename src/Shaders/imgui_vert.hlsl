
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

float4 UnpackU8U32( uint packedRGBA )
{
	return float4(
        (packedRGBA & 0xFF) / 255.0f,
        ((packedRGBA >> 8) & 0xFF) / 255.0f,
        ((packedRGBA >> 16) & 0xFF) / 255.0f,
        ((packedRGBA >> 24) & 0xFF) / 255.0f
    );
}

[ shader( "vertex" ) ]
void ImGuiVsMain( 
	in uint		vtxID : SV_VertexID,
	out float4	pos : SV_Position,
	[[vk::location( 0 )]] 
	out float4	col : COLOR0,
	[[vk::location( 1 )]] 
	out float2	uv : TEXCOORD0
) {
	imgui_vertex uiVtxAttrs = uiVtxBuff[ vtxID ];
	
	pos = float4( float2( uiVtxAttrs.x, uiVtxAttrs.y ) * pushBlock.scale + pushBlock.translate, 0.0f, 1.0f );
	col = UnpackU8U32( uiVtxAttrs.rgba8Unorm ).rgba;
	uv = float2( uiVtxAttrs.u, uiVtxAttrs.v );
}