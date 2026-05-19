#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
culling_init_params pushBlock;


[shader( "compute" )]
[numthreads( 1, 1, 1 )]
void CsMainCullingInit()
{
    if( !bool( pushBlock.isLatePass ) )
    {
        BufferStore<u32>( pushBlock.occludedInstCounterIdx, 0, 0 );
        BufferStore<u32>( pushBlock.occludedMeshletsCounterIdx, 0, 0 );
    }

    BufferStore<u32>( pushBlock.visibleInstCounterIdx, 0, 0 );
    BufferStore<u32>( pushBlock.visibleMeshletsCounterIdx, 0, 0 );
    BufferStore<u32>( pushBlock.drawCounterIdx, 0, 0 );

    u32 workItemsCount = !bool( pushBlock.isLatePass ) ?
        pushBlock.instCount : BufferLoad<u32>( pushBlock.occludedInstCounterIdx, 0 );
    dispatch_command dispatchCmd = { ( workItemsCount + pushBlock.cullShaderWorkGrX - 1 ) / pushBlock.cullShaderWorkGrX, 1, 1 };
    BufferStore<dispatch_command>( pushBlock.dispatchCmdBuffIdx, dispatchCmd, 0 );
}