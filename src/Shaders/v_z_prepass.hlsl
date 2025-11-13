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
	vertex vtx = storageBuffers[ pushBlock.vertexBuffIdx ].Load<vertex>( vertexId * sizeof( vertex ) );
	//uint di = draw_cmd_ref( drawCmdAddr ).drawCmd[ gl_DrawIDARB ].drawIdx;
	instance_desc thisInst = storageBuffers[ pushBlock.instanceBuffIdx ].Load<instance_desc>( instId * sizeof( instance_desc ) );
	
	global_data camData = storageBuffers[ pushBlock.globalBuffIdx ].Load<global_data>( pushBlock.camIdx * sizeof( global_data ) );
	
	float4 worldPos = mul( float4( vtx.px, vtx.py, vtx.pz, 1.0f ), thisInst.localToWorld );
	float4x4 camProjMainView = camData.mainView * camData.proj;
	posOut = mul( worldPos, camProjMainView );
}