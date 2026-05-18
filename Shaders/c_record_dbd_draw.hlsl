#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
record_dbg_draw_params pushBlock;


[numthreads( 1, 1, 1 )]
[shader("compute")]
void RecordDbgDrawCsMain()
{
    u32 instCount = device_ptr<u32>( pushBlock.gpuInstCountAddr )[ 0 ];
    draw_instanced_indexed_indirect drawIndirect = {
        pushBlock.indexCount,
        instCount,
        pushBlock.firstIndex,
        pushBlock.vertexOffset,
        0
    };
    device_ptr<draw_instanced_indexed_indirect> pDrawCmds = { pushBlock.dbgDrawCmdsAddr };
    pDrawCmds.Store( 0, drawIndirect );

    device_ptr<u32> pDbgCounter = { pushBlock.dbgDrawCountAddr };
    pDbgCounter.Store( 0, 1u );
}