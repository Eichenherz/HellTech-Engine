
struct root_const
{
	float2 scale;
	float2 translate;
	uint uiVtxBuffId;
	uint fontAtlasId;
	uint fontSamplerId;
};

// TODO: put in incldue 
struct imgui_vertex
{
	float x, y;
	float u, v;
	uint rgba8Unorm;
};

[[vk::push_constant]]
ConstantBuffer<root_const> rootConst : register( b0 );

struct vs_out
{
[[vk::location( 0 )]] float4 col : COLOR0;
[[vk::location( 1 )]] float2 uv : TEXCOORD0;
};

[shader( "vertex" )]
vs_out VsMain(
	in uint vtxID : SV_VertexID,
	out float4 pos : SV_Position
){
	StructuredBuffer<imgui_vertex> uiVtxBuff = ResourceDescriptorHeap[ rootConst.uiVtxBuffId ];
	imgui_vertex uiVtxAttrs = uiVtxBuff[ vtxID ];
	
	pos = float4( float2( uiVtxAttrs.x, uiVtxAttrs.y ) * rootConst.scale + rootConst.translate, 0.0f, 1.0f );
	
	vs_out vsOut;
	vsOut.col = unpack_u8u32( uiVtxAttrs.rgba8Unorm ).xyzw;
	vsOut.uv = float2( uiVtxAttrs.u, uiVtxAttrs.v );
	
	return vsOut;
}

[shader( "pixel" )]
float4 PsMain( in vs_out vsOut ) : SV_Target
{
	Texture2D fontAtlas = ResourceDescriptorHeap[ rootConst.fontAtlasId ];
	SamplerState fontSampler = SamplerDescriptorHeap[ rootConst.fontSamplerId ];
	return vsOut.col * fontAtlas.Sample( fontSampler, vsOut.uv );
}