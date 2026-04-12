#include "ht_renderer_types.h"

[[vk::push_constant]]
indirect_dispatcher_params pushBlock;


[numthreads(1, 1, 1)]
[shader("compute")]
void IndirectDispatcherCsMain()
{
    u32 workItemsCount = BufferLoad<u32>( pushBlock.counterBufferIdx, 0 );
    dispatch_command dispatchCmd = { ( workItemsCount + cullShaderWorkGrX - 1 ) / pushBlock.cullShaderWorkGrX, 1, 1 };
    BufferStore<dispatch_command>( pushBlock.dispatchCmdBuffIdx, dispatchCmd, 0 );
}