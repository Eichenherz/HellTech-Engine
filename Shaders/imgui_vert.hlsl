#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
imgui_pass_args pushBlock;


[shader( "vertex" )]
void ImGuiVsMain(
	in u32      vtxID   : SV_VertexID,
	out float4	pos     : SV_Position,
	[[vk::location( 0 )]] 
	out float4	col     : COLOR0,
	[[vk::location( 1 )]] 
	out float2	uv      : TEXCOORD0
) {
	imgui_vertex uiVtxAttrs = device_ptr<imgui_vertex>( pushBlock.vtxAddr )[ vtxID ];
	
	pos = float4( float2( uiVtxAttrs.x, uiVtxAttrs.y ) * pushBlock.scale + pushBlock.translate, 0.0f, 1.0f );
	col = float4( unpack_u8u32( uiVtxAttrs.rgba8Unorm ).rgba ) / 255.0f;
	uv  = float2( uiVtxAttrs.u, uiVtxAttrs.v );
}