#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
record_dbg_draw_params pushBlock;


[numthreads(1, 1, 1)]
[shader("compute")]
void RecordDbgDrawCsMain()
{
    u32 instCount = device_addr<u32>( pushBlock.gpuInstCountAddr )[ 0 ];
    draw_instanced_indexed_indirect drawIndirect = {
        pushBlock.indexCount,
        instCount,
        pushBlock.firstIndex,
        pushBlock.vertexOffset,
        0
    };
    DeviceAddrStore<draw_instanced_indexed_indirect>( pushBlock.dbgDrawCmdsAddr, 0, drawIndirect );
    DeviceAddrStore<u32>( pushBlock.dbgDrawCountAddr, 0, 1 );
}