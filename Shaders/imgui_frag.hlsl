#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
imgui_pass_args pushBlock;

[shader("pixel")]
float4 ImGuiPsMain(
	[[vk::location( 0 )]] 
	in float4 col : COLOR0,
	[[vk::location( 1 )]] 
	in float2 uv : TEXCOORD0
) : SV_Target
{
    Texture2D<float4>   fontAtlas   = gTexture2D_float4[ pushBlock.texAtlasIdx ];
    SamplerState        fontSampler = samplers[ pushBlock.samplerIdx ];
    return col * fontAtlas.Sample( fontSampler, uv );
}