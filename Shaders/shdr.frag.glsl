#version 460

#extension GL_GOOGLE_include_directive : require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mtl_ref{
	material_data materials[];
};

//layout( constant_id = 0 ) const bool TEXTURED_OUTPUT = false;


layout( location = 0 ) in vec3 normal;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in flat vertex_stage_data vtxVars;

layout( location = 0 ) out vec4 oCol;

void main()
{
	//if( TEXTURED_OUTPUT )
	//{
		material_data mtl = mtl_ref( g.addr + g.materialsOffset ).materials[ vtxVars.mtlIdx ];

		vec4 baseCol = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.diffuseIdx ) ], samplers[ nonuniformEXT( 0 ) ] ), uv.xy );
		vec3 bumpCol = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.bumpIdx ) ], samplers[ nonuniformEXT( 0 ) ] ), uv.xy ).rgb;
		bumpCol = bumpCol * 2.0 - vec3( 1 );
		bumpCol = normalize( vtxVars.tbn * bumpCol );

		vec3 lightDir = normalize( cam.camPos  - vtxVars.fragWorldPos );
		float lightSpotCosine = dot( lightDir, normalize( cam.camViewDir ) );
		float lightSpotInnerThreshold = cos( 0.2181662 ); // 12.5 degs
		float lightSpotOuterThreshold = cos( 0.3054326 ); // 17.5 degs
		float intensity = ( lightSpotCosine - lightSpotOuterThreshold ) / ( lightSpotInnerThreshold - lightSpotOuterThreshold );
		intensity = clamp( intensity, 0, 1.0 );


		float ambientFactor = 0.2;
		float lambertFactor = 0;
		float diffuseFactor = 0;
		if( lightSpotCosine > lightSpotOuterThreshold )
		{
			lambertFactor = abs( dot( lightDir, bumpCol ) );
			//diffuseFactor = abs( dot( lightDir, normal ) );
		}
		oCol = vec4( baseCol.rgb * ( ambientFactor + ( diffuseFactor + lambertFactor ) * intensity ), 1 );
		
	//}
	//else
	//{
		//oCol = vec4( normal * 0.5 + vec3( 0.5 ), 1.0 );
	//}
}