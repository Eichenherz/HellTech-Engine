#version 460

#extension GL_GOOGLE_include_directive: require

#define BINDLESS
#include "..\r_data_structs.h"

#extension GL_EXT_debug_printf : enable


layout( push_constant, scalar ) uniform block {
	uint64_t	instDescAddr;
	uint64_t	meshDescAddr;
	uint64_t    visInstAddr;
	uint64_t	drawCmdsAddr;
	uint64_t	compactedArgsAddr;
	uint64_t	occludedCacheAddr;
	uint64_t    atomicWorkgrCounterAddr;
	uint64_t    visInstaceCounterAddr;
	uint64_t    dispatchCmdAddr;
	uint	    hizBuffIdx;
	uint	    hizSamplerIdx;
	uint		instCount;
	uint		viewDataIdx;
	uint		latePass;
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) buffer bitset_ref{ 
	uint b[]; 
};

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mesh_desc_ref{ 
	mesh_desc meshes[]; 
};
layout( buffer_reference, std430, buffer_reference_align = 16 ) readonly buffer inst_desc_ref{
	instance_desc instDescs[];
};

layout( buffer_reference, buffer_reference_align = 4 ) coherent buffer coherent_counter_ref{
	uint coherentCounter;
};

layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer dispatch_indirect_ref{
	dispatch_command dispatchCmd;
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer draw_cmd_ref{
	draw_command drawCmds[];
};
layout( buffer_reference, buffer_reference_align = 4 ) writeonly buffer compacted_args_ref{
	compacted_draw_args compactedDrawArgs[];
};

shared uint workgrAtomicCounterShared;

layout( local_size_x_id = 0 ) in;

struct frustum_culling_result
{
	bool visible;
	bool intersectsNearZ;
};

// TODO: culling inspired by Nabla
// https://github.com/Devsh-Graphics-Programming/Nabla/blob/master/include/nbl/builtin/glsl/utils/culling.glsl
frustum_culling_result FrustumCulling( vec3 boxMin, vec3 boxMax, mat4 mvp )
{	
	mat4 trsMvp = transpose( mvp );

	vec4 xPlanePos = trsMvp[ 3 ] + trsMvp[ 0 ];
	vec4 yPlanePos = trsMvp[ 3 ] + trsMvp[ 1 ];
	vec4 xPlaneNeg = trsMvp[ 3 ] - trsMvp[ 0 ];
	vec4 yPlaneNeg = trsMvp[ 3 ] - trsMvp[ 1 ];
		
	bool visible = true;
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( trsMvp[ 3 ].xyz, vec3( 0.0f ) ) ), trsMvp[ 3 ].xyz ) > -trsMvp[ 3 ].w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlanePos.xyz, vec3( 0.0f ) ) ), xPlanePos.xyz ) > -xPlanePos.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlanePos.xyz, vec3( 0.0f ) ) ), yPlanePos.xyz ) > -yPlanePos.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( xPlaneNeg.xyz, vec3( 0.0f ) ) ), xPlaneNeg.xyz ) > -xPlaneNeg.w );
	visible = visible && ( dot( mix( boxMax, boxMin, lessThan( yPlaneNeg.xyz, vec3( 0.0f ) ) ), yPlaneNeg.xyz ) > -yPlaneNeg.w );

	float minW = dot( mix( boxMax, boxMin, greaterThanEqual( trsMvp[ 3 ].xyz, vec3( 0.0f ) ) ), trsMvp[ 3 ].xyz ) + trsMvp[ 3 ].w;
	bool intersectsNearZ = minW <= 0.0f;

	frustum_culling_result res;
	res.visible = visible;
	res.intersectsNearZ = intersectsNearZ;
	return res;
}

struct proj_bounds
{
	vec2 minXY;
	vec2 maxXY;
	float maxZ;
};

// NOTE: we're using reverse Z 
// NOTE: from https://interplayoflight.wordpress.com/2017/11/15/experiments-in-gpu-based-occlusion-culling/
proj_bounds ProjectAabbToScreenspace( vec3 boxMin, vec3 boxMax, mat4 mvp )
{
	vec3 boxSize = boxMax - boxMin;
	vec3 boxCorners[] = { 
		boxMin,
		boxMin + vec3( boxSize.x, 0, 0 ),
		boxMin + vec3( 0, boxSize.y, 0 ),
		boxMin + vec3( 0, 0, boxSize.z ),
		boxMin + vec3( boxSize.xy, 0 ),
		boxMin + vec3( 0, boxSize.yz ),
		boxMin + vec3( boxSize.x, 0, boxSize.z ),
		boxMin + boxSize 
	};
			
	vec2 minXY = vec2( 1.0f );
	vec2 maxXY = vec2( 0.0f );
	float maxZ = 0.0f;
	[[ unroll ]]
	for( uint i = 0; i < 8; ++i )
	{
		vec4 clipPos = mvp * vec4( boxCorners[ i ], 1.0f );
		clipPos.xyz = clipPos.xyz / clipPos.w;
		clipPos.xy = clamp( clipPos.xy, -1.0f, 1.0f );
		clipPos.xy = clipPos.xy * vec2( 0.5f, -0.5f ) + vec2( 0.5f, 0.5f );
 			
		minXY = min( clipPos.xy, minXY );
		maxXY = max( clipPos.xy, maxXY );
		maxZ = max( maxZ, clipPos.z );
	}

	proj_bounds res;
	res.minXY = minXY;
	res.maxXY = maxXY;
	res.maxZ = maxZ;
	return res;
}

// NOTE: also check https://github.com/zeux/niagara/blob/master/src/shaders/drawcull.comp.glsl for more details
bool ProjectedAabbVsHzb( proj_bounds projBounds, uint hzbIdx, uint minQuadSamplerIdx )
{
	vec2 hzbSize = textureSize( sampler2D( sampledImages[ hzbIdx ], samplers[ minQuadSamplerIdx ] ), 0 ).xy;
	vec2 projAabbSize = abs( projBounds.maxXY - projBounds.minXY ) * hzbSize;

	float mipLevel = ceil( log2( max( projAabbSize.x, projAabbSize.y ) ) );
	float hzbMaxMip = textureQueryLevels( sampler2D( sampledImages[ hzbIdx ], samplers[ minQuadSamplerIdx ] ) ) - 1.0f;
    mipLevel = clamp( mipLevel, 0, hzbMaxMip );

    float prevLevel = max( mipLevel - 1, 0 );
    vec2 scale = vec2( exp2( -prevLevel ) );
    vec2 scaledMinCorner = floor( projBounds.minXY * scale );
    vec2 scaledMaxCorner = ceil( projBounds.maxXY * scale);
    vec2 dims = scaledMaxCorner - scaledMinCorner;
 
    // NOTE: Use the prev level ( more detailed ) if we only touch <= 2 texels in both dimensions
    if( all( lessThanEqual( dims, vec2( 2 ) ) ) )
	{
		mipLevel = prevLevel;
	}

	vec2 uvMid = ( projBounds.maxXY + projBounds.minXY ) * 0.5f;
	float sampledDepth = textureLod( sampler2D( sampledImages[ hzbIdx ], samplers[ minQuadSamplerIdx ] ), uvMid, mipLevel ).x;	
	return ( sampledDepth <= projBounds.maxZ );	
}

layout( local_size_x = 32, local_size_y = 1, local_size_z = 1 ) in;
void main()
{
	if( gl_LocalInvocationIndex.x == 0 )
	{
		workgrAtomicCounterShared = 0u;
	}
	barrier();

	bool visible = false;

	uint globalIdx = gl_GlobalInvocationID.x;
	if( globalIdx < instCount )
	{
		instance_desc currentInst = inst_desc_ref( instDescAddr ).instDescs[ globalIdx ];
		mesh_desc currentMesh = mesh_desc_ref( meshDescAddr ).meshes[ currentInst.meshIdx ];
	
		vec3 center = currentMesh.center;
		vec3 extent = currentMesh.extent;
	
		vec3 boxMin = center - extent;
		vec3 boxMax = center + extent;
	
		view_data view = ssbos[ viewDataIdx ].views[ 0 ];//viewIdx ];
		
		bool testOcclusion = true;
		if( !bool( latePass ) ) 
		{
			mat4 mvp = view.mainViewProj * currentInst.localToWorld;
			frustum_culling_result frustumCullingResult = FrustumCulling( boxMin, boxMax, mvp );
			visible = frustumCullingResult.visible;
			testOcclusion = visible && !frustumCullingResult.intersectsNearZ;
		}
		else // NOTE: Late pass will only test occluded from prev
		{
			testOcclusion = bool( bitset_ref( occludedCacheAddr ).b[ globalIdx ] );
			//visible = testOcclusion;
		}

		// NOTE: no mem barrier for occluder cache bc the accesses happen on different passes
		if( testOcclusion )
		{
			mat4 viewProj = bool( latePass ) ? view.mainViewProj : view.prevViewProj;
			// NOTE: must use prev instance transform too if not static geometry
			mat4 mvp = viewProj * currentInst.localToWorld;

			proj_bounds projBounds = ProjectAabbToScreenspace( boxMin, boxMax, mvp );
			visible = ProjectedAabbVsHzb( projBounds, hizBuffIdx, hizSamplerIdx );
			if( !bool( latePass ) ) 
			{
				bitset_ref( occludedCacheAddr ).b[ globalIdx ] = uint( -int( !visible ) );
			}
		}
	}

	uvec4 ballotVisible = subgroupBallot( visible );
	uint subgrActiveInvocationsCount = subgroupBallotBitCount( ballotVisible );
	// NOTE: also fine bc grSz == waveSz
	if( subgrActiveInvocationsCount > 0 ) 
	{
		// TODO: shared atomics + global atomics BUT only if grSz > waveSz
		uint subgrSlotOffset = 0;
		// NOTE: no need for GR barrier bc grSz == waveSz
		if( subgroupElect() )
		{
			subgrSlotOffset = atomicAdd( coherent_counter_ref( visInstaceCounterAddr ).coherentCounter, subgrActiveInvocationsCount );
		}
		uint subgrActiveIdx = subgroupBallotExclusiveBitCount( ballotVisible );
		uint slotIdx = subgroupBroadcastFirst( subgrSlotOffset  ) + subgrActiveIdx;
		
		if( visible )
		{
			// TODO: must use SoA buffers for better access
			instance_desc currentInst = inst_desc_ref( instDescAddr ).instDescs[ globalIdx ];
			mesh_desc currentMesh = mesh_desc_ref( meshDescAddr ).meshes[ currentInst.meshIdx ];
			mesh_lod lod = currentMesh.lods[ 0 ];

			compacted_args_ref( compactedArgsAddr ).compactedDrawArgs[ slotIdx ].nodeIdx = globalIdx; 
			
			draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].indexCount = lod.indexCount; 
			draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].instanceCount = 1;
			draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].firstIndex = lod.indexOffset;
			draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].vertexOffset = 0;
			draw_cmd_ref( drawCmdsAddr ).drawCmds[ slotIdx ].firstInstance = 0;
		}
	}

	if( gl_LocalInvocationIndex.x == 0 ) 
	{
		workgrAtomicCounterShared = atomicAdd( coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter, 1 );
	}

	barrier();
	memoryBarrier();

	if( workgrAtomicCounterShared != ( gl_NumWorkGroups.x - 1 ) )
	{
		return;
	}

	if( gl_LocalInvocationIndex.x == 0 )
	{
		//// TODO: pass as spec consts or push consts ? 
		//uint mletsExpDispatch = ( coherent_counter_ref( drawCounterAddr ).coherentCounter + 3 ) / 4;
		//dispatch_indirect_ref( dispatchCmdAddr ).dispatchCmd = dispatch_command( mletsExpDispatch, 1, 1 );
		//// NOTE: reset atomicCounter
		//coherent_counter_ref( atomicWorkgrCounterAddr ).coherentCounter = 0;
	}
}