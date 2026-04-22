#pragma once

#ifndef __HELLTECH_VBUFFER_H__
#define __HELLTECH_VBUFFER_H__

#include "ht_renderer_types.h"

struct vbuffer_vs_out
{
    float4              pos     : SV_Position;
    nointerpolation u32 instIdx : TEXCOORD0;
    nointerpolation u32 triOff  : TEXCOORD1;
};

struct vbuffer_ps_out
{
    u32x2 pixel : SV_Target0;
};

bool VBufferIsValidPixel( u32x2 vbuffPixel )
{
    return ( ~u32( 0 ) != vbuffPixel.x ) && ( ~u32( 0 ) != vbuffPixel.y );
}
#endif //!__HELLTECH_VBUFFER_H__