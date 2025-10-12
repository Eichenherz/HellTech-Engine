#include "r_data_structs2.h"

[[vk::push_constant]]
struct {
	uint globalBuffIdx;
	uint camIdx;
	uint vertexBuffIdx;
	uint instanceBuffIdx;
	float2 translate;
} pushBlock;

[shader("vertex")]
void ZPrepassVsMain( in uint vtxID : SV_VertexID, out float4 posOut : SV_Position )
{
	uint vertexId = uint( vtxID >> 16 );
	uint instId = uint( vtxID & uint16_t( -1 ) );
	
	// TODO: pos only
	// TODO: transf only
	vertex vtx = BufferProxy<vertex>( pushBlock.vertexBuffIdx, vertexId );
	//uint di = draw_cmd_ref( drawCmdAddr ).drawCmd[ gl_DrawIDARB ].drawIdx;
	instance_desc thisInst = BufferProxy<instance_desc>( pushBlock.instanceBuffIdx, instId );
	
	global_data camData = BufferProxy<global_data>( pushBlock.globalBuffIdx, pushBlock.camIdx );

	float4 worldPos = mul( float4( vtx.px, vtx.py, vtx.pz, 1.0f ), thisInst.localToWorld );
	float4x4 camProjMainView = camData.mainView * camData.proj;
	posOut = mul( worldPos, camProjMainView );
}