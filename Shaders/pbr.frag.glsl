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


// TODO: pre-convert to Linear space
vec4 SrgbToLinear( vec4 srgb )
{
	bvec4 cutoff = lessThanEqual( srgb, vec4( 0.04045 ) );
	vec4 higher = pow( ( srgb + vec4( 0.055 ) ) / vec4( 1.055 ), vec4( 2.4 ) );
	vec4 lower = srgb * vec4( 1.0 / 12.92 );

	return mix( higher, lower, cutoff );
}
// NOTE: inspired by Unreal 2013
float DistRadiusAtten( float distSq, float lightRad )
{
	float invRadius = 1.0 / lightRad;
	float invRadiusSq = invRadius * invRadius;

	float factor = distSq * invRadiusSq;
	float smoothFactor = clamp( 1.0 - factor * factor, 0.0, 1.0 );

	return ( smoothFactor * smoothFactor ) / ( distSq + 1.0 );
}
float SpotAngleAtten( float spotLightCos, float cosInner, float cosOuter )
{
	float spotRange = 1.0 / max( cosInner - cosOuter, 1e-4 );
	float spotOffset = -cosOuter / spotRange;

	float attenuation = clamp( spotLightCos * spotRange + spotOffset, 0, 1 );
	return attenuation * attenuation;
}
float DistributionGGX( float nDotH, float roughnessParam )
{
	float alphaSq = roughnessParam * roughnessParam;
	float denom = ( nDotH * alphaSq - nDotH ) * nDotH + 1.0;
	return invPi * alphaSq / ( denom * denom );
}
// NOTE: from Frostbite PBR
float VisibilitySmtihGGXCorr( float NdotL, float NdotV, float roughnessParam )
{
	float alphaSq = roughnessParam * roughnessParam;
	// NOTE: "NdotL *" and "NdotV *" inversed on purpose
	// TODO: lambdaV can be pre-computed for all the lights, it should be moved out of this function
	float lambdaV = NdotL * sqrt( ( -NdotV * alphaSq + NdotV ) * NdotV + alphaSq );
	float lambdaL = NdotV * sqrt( ( -NdotL * alphaSq + NdotL ) * NdotL + alphaSq );
	return  0.5 / ( lambdaV + lambdaL );
}
vec3 FresnelSchlickUnreal( vec3 refl0, float refl90, float VdotH )
{
	float gaussianApprox = ( -5.55473 * VdotH - 6.98316 ) * VdotH;
	return refl0 + ( refl90 - refl0 ) * pow( 2, gaussianApprox );
}
vec3 FresnelSchlick( vec3 refl0, float refl90, float VdotH )
{
	return refl0 + ( vec3( refl90 ) - refl0 ) * pow( 1.0 - VdotH, 5 );
}

vec3 ComputeBrdfReflectance( 
	vec3	lightDir,
	vec3	diffColor, 
	vec3	baseReflectivity,
	vec3	viewDir,  
	vec3	normal, 
	float	linearRoughness 
){
	vec3 halfVec = normalize( viewDir + lightDir );
	float NdotV = abs( dot( normal, viewDir ) ) + 1e-5;
	float NdotL = clamp( dot( normal, lightDir ), 0, 1 );
	float NdotH = clamp( dot( normal, halfVec ), 0, 1 );
	float LdotH = clamp( dot( lightDir, halfVec ), 0, 1 );
	
	float roughness = linearRoughness * linearRoughness;
	//float alphaSq = roughness * roughness;
	float distr = DistributionGGX( NdotH, roughness );
	float vis = VisibilitySmtihGGXCorr( NdotV, NdotL, roughness );
	vec3 specularRefl = FresnelSchlick( baseReflectivity, 1, LdotH );
	vec3 specularBrdf = ( distr * vis ) * specularRefl;

	vec3 diffuseRefl = 1.0 - specularRefl;
	// NOTE: lambertian diffuse
	vec3 diffuseBrdf = diffuseRefl * diffColor * invPi;
	
	return ( specularBrdf + diffuseBrdf ) * NdotL;
}


layout( location = 0 ) in vec3 normal;
layout( location = 1 ) in vec3 tangent;
layout( location = 2 ) in vec3 worldPos;
layout( location = 3 ) in vec2 uv;
layout( location = 4 ) in flat uint mtlIdx;

layout( location = 0 ) out vec4 oCol;

layout( early_fragment_tests ) in;
void main()
{
	material_data mtl = mtl_ref( bdas.mtrlsAddr ).materials[ mtlIdx ];

	vec4 baseCol = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.baseColIdx ) ], samplers[ 0 ] ), uv );
	vec3 orm = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.occRoughMetalIdx ) ], samplers[ 0 ] ), uv ).rgb;
	vec3 normalFromMap = texture( sampler2D( sampledImages[ nonuniformEXT( mtl.normalMapIdx ) ], samplers[ 0 ] ), uv ).rgb;

	normalFromMap = normalFromMap * 2.0 - 1.0;
	normalFromMap.b = sqrt( clamp( 1 - dot( normalFromMap.rg, normalFromMap.rg ), 0, 1 ) );

	baseCol = SrgbToLinear( baseCol );
	baseCol *= vec4( mtl.baseColFactor, 1 );

	vec3 t = normalize( tangent );
	vec3 n = normalize( normal );
	// NOTE: orthonormalize t wrt n
	t = normalize( t - dot( t, n ) * n );
	vec3 b = cross( n, t );
	mat3 tbn = mat3( t, b, n );

	vec3 bumpN = normalize( tbn * normalFromMap );

	float surfRoughness = orm.r * mtl.roughnessFactor;
	float surfMetalness = orm.g * mtl.metallicFactor;

	vec3 viewDir = normalize( cam.worldPos - worldPos );

	vec3 baseReflectivity = mix( vec3( 0.04 ), baseCol.rgb, surfMetalness );
	vec3 diffCol = baseCol.rgb * ( 1.0 - surfMetalness );

	vec3 reflectance = {};

	float ambientFactor = 0.0025;
	oCol = vec4( baseCol.rgb * ambientFactor, 1 );

	[[unroll]] for( uint i = 0; i < 4; ++i )
	{
		light_data l = light_ref( bdas.lightsDescAddr ).lights[ i ];
		vec3 posToLight = l.pos - worldPos;
		vec3 radiance = l.col * DistRadiusAtten( dot( posToLight, posToLight ), l.radius );

		reflectance += ComputeBrdfReflectance( 
			normalize( posToLight ), diffCol, baseReflectivity, viewDir, bumpN, surfRoughness ) * radiance;
	}
	oCol += vec4( reflectance, 1 );
	

	//{
	//	light_data flashlight = { cam.camPos, vec3( 500.0 ), 50.0 };
	//	const vec2 spotCosInOut = cos( vec2( radians( 12.5 ), radians( 17.5 ) ) );
	//	float spotCos = dot( viewDir, normalize( cam.camViewDir ) );
	//
	//	vec3 posToLight = flashlight.pos - worldPos;
	//	vec3 radiance = flashlight.col *
	//		DistRadiusAtten( dot( posToLight, posToLight ), flashlight.radius ) *
	//		SpotAngleAtten( spotCos, spotCosInOut.x, spotCosInOut.y );
	//	vec3 flashReflectance = vec3( 0 );
	//
	//	flashReflectance = ComputeBrdfReflectance( normalize( posToLight ),
	//											   diffCol,
	//											   baseReflectivity,
	//											   viewDir,
	//											   bumpN,
	//											   surfRoughness );
	//
	//	oCol += vec4( flashReflectance * radiance * uint( spotCos > spotCosInOut.y ), 1 );
	//}

	//oCol = vec4( ( bumpN * 0.5 + 0.5 ) * 0.0015, 1 );
	//oCol = vec4( ( n * 0.5 + 0.5 ) * 0.0015, 1 );
}