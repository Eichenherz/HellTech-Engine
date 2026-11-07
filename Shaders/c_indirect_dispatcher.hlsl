#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
indirect_dispatcher_params pushBlock;

[shader( "compute" )]
[numthreads( 1, 1, 1 )]
void IndirectDispatcherCsMain()
{
    u32 workItemsCount = device_ptr<u32>( pushBlock.counterBufferAddr )[ 0 ];
    dispatch_command dispatchCmd = {
        ( workItemsCount + pushBlock.cullShaderWorkGrX - 1 ) / pushBlock.cullShaderWorkGrX,
        1,
        1
    };
    device_ptr<dispatch_command>( pushBlock.dispatchCmdBuffAddr ).Store( 0, dispatchCmd );
}