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
    u32x2 pixel : SV_Target0;
};

static const u32 MLT_MASK = ( 1u << 24 ) - 1u;

struct vbuffer_pixel
{
    u32 mltId : 24;
    u32 triId : 6; // local to the cluster
    u32 instId : 24;
    u32 lodId : 6;

    operator u32x2() { return u32x2( triId << 24 | mltId, lodId << 24 | instId ); }
};

vbuffer_pixel VBufferUnpackPixel( u32x2 packed )
{
    vbuffer_pixel pixel = { packed.x & MLT_MASK, packed.x >> 24, packed.y & MLT_MASK, packed.y >> 24 };
    return pixel;
}

bool VBufferIsValidPixel( u32x2 vbuffPixel ) { return all( ~0u != vbuffPixel ); }

#endif //!__HELLTECH_VBUFFER_H__