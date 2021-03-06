#version 460

#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_control_flow_attributes : require

#extension GL_KHR_shader_subgroup_basic: require
#extension GL_KHR_shader_subgroup_arithmetic: require
#extension GL_KHR_shader_subgroup_ballot: require

layout( push_constant ) uniform block{
	layout( offset = 80 ) uint64_t	lightTilesAddr;
	uint		tileSize;
	uint		tileRowLen;
	uint		tileWordCount;
	uint		tileMaxLightsLog2;
};


layout( buffer_reference, buffer_reference_align = 4 ) buffer tile_ref{ 
	uint lightTiles[]; 
};


layout( location = 0 ) in flat uint lightId;

//const uint TILE_SIZE = 8;
//const uint TILE_WORDS_STRIDE = 1; //smth
//const uint TILE_GRID_MAX_LIGHTS_LOG2 = 1; //smth
//const uint TILE_ROW_LEN = 1; //smth

//layout( early_fragment_tests ) in;
void main()
{
	atomicOr(  tile_ref( lightTilesAddr ).lightTiles[ 0 ], -1 );
	uvec2 tileId = uvec2( gl_FragCoord.xy ) % tileSize;
	uint tileIndex = tileId.y * tileRowLen + tileId.x;
	uint lightBit = 1u << ( lightId % 32 );
	uint lightWord = lightId / 32;
	
	uint wordIndex = tileIndex * tileWordCount + lightWord;
	
	uint key = wordIndex << tileMaxLightsLog2;
	
	uvec4 waveMask;
	for(;;)
	{
		uint firstInvocationVal = subgroupBroadcastFirst( key );
		waveMask = subgroupBallot( firstInvocationVal == key );
		if( firstInvocationVal == key ) break;
	}
	
	uint hash = subgroupBallotInclusiveBitCount( waveMask );
	[[ branch ]]
	if( hash == 0 )
	{
		atomicOr(  tile_ref( lightTilesAddr ).lightTiles[ wordIndex ], lightBit );
	}
}