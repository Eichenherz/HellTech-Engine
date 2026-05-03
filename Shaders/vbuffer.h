#pragma once

#ifndef __HELLTECH_VBUFFER_H__
#define __HELLTECH_VBUFFER_H__

#include "ht_renderer_types.h"

struct vbuffer_vs_out
{
    float4              pos             : SV_Position;
    nointerpolation u32 globalMltIdx    : TEXCOORD0;
    nointerpolation u32 globalInstIdx   : TEXCOORD1;
};

struct vbuffer_ps_out
{
    u32x2 pixel : SV_Target0;
};

static const u32 MLT_MASK = ( 1u << 24 ) - 1u;

struct vbuffer_pixel
{
    u32 mltId;
    u32 triId; // local to the cluster
    u32 instId;
};


u32x2 VBufferPackPixel( u32 globalMltIdx, u32 primitiveId, u32 globalInstId )
{
    // TODO: assert ( somewhere ) that we won't have more than 24 bits for the mlt
    return u32x2( ( primitiveId << 24 ) | globalMltIdx, globalInstId );
}

vbuffer_pixel VBufferUnpackPixel( u32x2 packed )
{
    vbuffer_pixel pixel = { packed.x & MLT_MASK, packed.x >> 24, packed.y };
    return pixel;
}


bool VBufferIsValidPixel( u32x2 vbuffPixel )
{
    return ( ~u32( 0 ) != vbuffPixel.x ) && ( ~u32( 0 ) != vbuffPixel.y );
}

#endif //!__HELLTECH_VBUFFER_H__