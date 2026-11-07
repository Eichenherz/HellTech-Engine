#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

#include "vbuffer.h"

[[vk::push_constant]]
vbuffer_params pushBlock;

[shader("pixel")]
vbuffer_ps_out VBufferPsMain( in vbuffer_vs_out vsOut, in u32 primId : SV_PrimitiveID )
{
    vbuffer_pixel pixel = { vsOut.globalMltIdx, primId, vsOut.globalInstIdx, vsOut.lodIdx };
    return vbuffer_ps_out( ( u32x2 ) pixel );
}