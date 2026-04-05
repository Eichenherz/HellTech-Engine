[[vk::binding( 1 )]] Texture2D fontAtals;
[[vk::binding( 1 )]] SamplerState fontSampler;

[shader("pixel")]
float4 ImGuiPsMain(
	[[vk::location( 0 )]] 
	in float4 col : COLOR0,
	[[vk::location( 1 )]] 
	in float2 uv : TEXCOORD0
) : SV_Target
{
    return col * fontAtals.Sample( fontSampler, uv );
}