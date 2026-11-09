#pragma once

#ifndef __HELLTECH_VBUFFER_H__
#define __HELLTECH_VBUFFER_H__

#include "../HtLib/ht_renderer_types.h"

struct vbuffer_vs_out
{
    float4              pos             : SV_Position;
    nointerpolation u32 globalMltIdx    : TEXCOORD0;
    nointerpolation u32 globalInstIdx   : TEXCOORD1;
    nointerpolation u32 lodIdx          : TEXCOORD2;
};

struct vbuffer_ps_out
{

};

struct vbuffer_pixel
{
    u32 mltId   : 25;
    u32 triId   : 7; // local to the cluster
    u32 instId  : 26;
    u32 lodId   : 6;
};

u32x2 VBufferPackPixel( vbuffer_pixel pixel )
{
    return u32x2( pixel.triId << 25 | pixel.mltId, pixel.lodId << 26 | pixel.instId );
}

vbuffer_pixel VBufferUnpackPixel( u32x2 packed )
{
    vbuffer_pixel pixel = {
        packed.x & ( ( 1u << 25 ) - 1u ),
        packed.x >> 25,
        packed.y & ( ( 1u << 26 ) - 1u ),
        packed.y >> 26
    };
    return pixel;
}

bool VBufferIsValidPixel( u32x2 vbuffPixel ) { return all( ~0u != vbuffPixel ); }

#endif //!__HELLTECH_VBUFFER_H__