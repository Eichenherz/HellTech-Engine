#ifndef __HELLTECH_HT_DBG_COMMON_H__
#define __HELLTECH_HT_DBG_COMMON_H__

#include "ht_hlsl_lang.h"

struct meshlet_vs_out
{
    precise float4  pos     : SV_Position;
    float3          n       : NORMAL;
    float3          t       : TANGENT;
    float3          wrldPos : TEXCOORD1;
    NOINTERP float  tanSgn  : TEXCOORD2;
};

#endif //__HELLTECH_HT_DBG_COMMON_H__