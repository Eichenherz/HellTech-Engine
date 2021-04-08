#version 460

#extension GL_GOOGLE_include_directive : require

#define GLOBAL_RESOURCES

#include "..\r_data_structs.h"

layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer mtl_ref{
	material_data materials[];
};
layout( buffer_reference, scalar, buffer_reference_align = 4 ) readonly buffer light_ref{
	light_data lights[];
};

layout( push_constant ) uniform block{
	uint64_t lightsBuffAddr;
};

//layout( constant_id = 0 ) const bool TEXTURED_OUTPUT = false;

layout( location = 0 ) in vec3 normal;
layout( location = 1 ) in vec2 uv;
layout( location = 2 ) in vec3 worldPos;
layout( location = 3 ) in flat vertex_stage_data vtxVars;

layout( location = 0 ) out vec4 oCol;



const float oneOverPi = 0.31830988618;
const float pi = 3.14159265359;



vec4 SrgbToLinear( vec4 srgb )
{
	bvec4 cutoff = lessThanEqual( srgb, vec4( 0.04045 ) );
	vec4 higher = pow( ( srgb + vec4( 0.055 ) ) / vec4( 1.055 ), vec4( 2.4 ) );
	vec4 lower = srgb * vec4( 1.0 / 12.92 );

	return mix( higher, lower, cutoff );
}
// NOTE: inspired by Unreal 2013
float InvSquareLightFalloff( float dist, float lightRad )
{
	float distOverRad = dist / lightRad;
	float denom = clamp( 1.0 - pow( distOverRad, 4 ), 0.0, 1.0 );

	return denom * denom / ( dist * dist + 1.0 );
}
float DistributeGGX( float normalHalfvecAngle, float roughness )
{
	// NOTE: mickey mouse reparam: alpha = roughness * roughness
	float mickeyMouseAlphaSq = ( roughness * roughness ) * ( roughness * roughness );
	float denom = normalHalfvecAngle * normalHalfvecAngle * ( mickeyMouseAlphaSq - 1.0 ) + 1.0;
	return oneOverPi * mickeyMouseAlphaSq / ( denom * denom );
}
// NOTE: don't apply for IBL
float GeometrySchlickGGX( float nDotView, float nDotLight, float roughness )
{
	// NOTE: mickey mouse reparam: alpha = ( roughness + 1 ) / 2
	float mickeyMouseK = ( roughness + 1 ) * ( roughness + 1 ) * 0.125;
	float gV = nDotView / ( nDotView * ( 1 - mickeyMouseK ) + mickeyMouseK );
	float gL = nDotLight / ( nDotLight * ( 1 - mickeyMouseK ) + mickeyMouseK );

	return gV * gL;
}
vec3 FresnelSchlickUnreal( vec3 baseReflect, float viewHalfvecAngle )
{
	float gaussianApprox = ( -5.55473 * viewHalfvecAngle - 6.98316 ) * viewHalfvecAngle;
	return baseReflect + ( vec3( 1.0 ) - baseReflect ) * pow( 2, gaussianApprox );
}
// TODO: light rad param
vec3 ComputeBrdfReflectance( 
	light_data	lightData,
	vec3		baseCol, 
	vec3		baseReflectivity,
	vec3		viewDir, 
	vec3		worldPos, 
	vec3		normal, 
	float		metalness, 
	float		roughness )
{
	vec3 lightDir = normalize( lightData.pos - worldPos );
	float distToLight = length( lightData.pos - worldPos );
	vec3 radiance = lightData.col * InvSquareLightFalloff( distToLight, lightData.radius );
	//vec3 radiance = lightData.col / ( distToLight * distToLight );

	vec3 halfVec = normalize( viewDir + lightDir );
	float nDotView = max( dot( normal, viewDir ), 0 );
	float nDotLight = max( dot( normal, lightDir ), 0 );

	float specularD = DistributeGGX( max( dot( normal, halfVec ), 0 ), roughness );
	float specularG = GeometrySchlickGGX( nDotView, nDotLight, roughness );
	vec3 specularTerm = FresnelSchlickUnreal( baseReflectivity, max( dot( viewDir, halfVec ), 0 ) );
	vec3 diffuseTerm = ( vec3( 1.0 ) - specularTerm ) * ( 1.0 - metalness );

	vec3 cookTorranceBrdf = specularD * specularG * specularTerm / max( 4 * nDotView * nDotLight, 0.001 );
	//vec3 cookTorranceBrdf = 0.25 * specularD * specularG * specularTerm / max( nDotView * nDotLight, 0.001 );
	vec3 lambertBrdf = diffuseTerm * baseCol * oneOverPi;

	return ( cookTorranceBrdf + lambertBrdf ) * radiance * max( dot( normal, lightDir ), 0 );
}

void main()
{
	//if( TEXTURED_OUTPUT )
	//{
		material_data mtl = mtl_ref( g.addr + g.materialsOffset ).materials[ vtxVars.mtlIdx ];

		vec4 baseCol = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.baseColIdx ) ], samplers[ nonuniformEXT( 0 ) ] ), 
								uv.xy );
		// TODO: when using baseCol.alpha sRGB->linear ?
		baseCol = SrgbToLinear( baseCol );
		vec3 metalRough = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.metalRoughIdx ) ], samplers[ nonuniformEXT( 0 ) ] ),
								   uv.xy ).rgb;
		vec3 normalFromMap = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.normalMapIdx ) ], samplers[ nonuniformEXT( 0 ) ] ), 
									  uv.xy ).rgb;
		normalFromMap = normalFromMap * 2.0 - vec3( 1 );
		// TODO: pass tbn transformed data to the frag, not tbn itself ?
		normalFromMap = normalize( vtxVars.tbn * normalFromMap );

		float surfMetalness = metalRough.b * mtl.metallicFactor;
		float surfRoughness = metalRough.g * mtl.roughnessFactor;

		vec3 viewDir = normalize( cam.camPos - worldPos );
		vec3 baseReflectivity = mix( vec3( 0.04 ), baseCol.rgb, surfMetalness );
		vec3 reflectance = vec3( 0 );

		[[unroll]] for( uint i = 0; i < 4; ++i )
		{
			reflectance += ComputeBrdfReflectance( light_ref( lightsBuffAddr ).lights[ i ],
												   baseCol.rgb,
												   baseReflectivity,
												   viewDir, 
												   worldPos,
												   normal,
												   surfMetalness,
												   surfRoughness );
		}

		float ambientFactor = 0.0025;
		oCol = vec4( baseCol.rgb * ambientFactor, 1 );
		oCol += vec4( reflectance, 1 );
		//vec3 directionalLightDir = vec3( 1, 0, 0 );
		//float diffDirectionalLight = max( dot( directionalLightDir, normalFromMap ), 0.0 );
		//float directionalIntensity = 0.5;

		light_data flashlight = { viewDir, vec3( 1000.0 ), 40 };
		vec3 flashReflectance = ComputeBrdfReflectance( flashlight,
														baseCol.rgb,
														baseReflectivity,
														viewDir,
														worldPos,
														normal,//normalFromMap,
														surfMetalness,
														surfRoughness );

		const float spotLightInnerThreshold = cos( 0.2181662 ); // 12.5 degs
		const float spotLightOuterThreshold = cos( 0.3054326 ); // 17.5 degs
		float spotLightCosine = dot( viewDir, normalize( cam.camViewDir ) );
		float intensity = ( spotLightCosine - spotLightOuterThreshold ) / ( spotLightInnerThreshold - spotLightOuterThreshold );
		intensity = clamp( intensity, 0, 1.0 );

		if( spotLightCosine > spotLightOuterThreshold )
		{
			//oCol += vec4( flashReflectance, 1 );
		}
		

		
	//}
	//else
	//{
		//oCol = vec4( normal * 0.5 + vec3( 0.5 ), 1.0 );
	//}
}