#ifndef __cplusplus

vec3 RotateQuat( vec3 v, vec4 q )
{
	vec3 t = 2.0 * cross( q.xyz, v );
	return v + q.w * t + cross( q.xyz, t );
}



//vec3 Oct16Decode()
#endif 
