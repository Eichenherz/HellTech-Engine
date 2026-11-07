#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

[[vk::push_constant]]
culling_pass_params pushBlock;


[shader( "compute" )]
[numthreads( 1, 1, 1 )]
void CsMainCullingInit()
{
    const bool isLatePass = bool( pushBlock.isLatePass );

    if( !isLatePass )
    {
        device_ptr<u32>( pushBlock.occludedInstCounterAddr ).Store( 0, 0 );
        device_ptr<u32>( pushBlock.occludedMltCounterAddr ).Store( 0, 0 );
        device_ptr<u32>( pushBlock.mltToTestCounterAddr ).Store( 0, 0 );
    }

    device_ptr<u32>( pushBlock.visibleInstCounterAddr ).Store( 0, 0 );
    device_ptr<u32>( pushBlock.drawCounterAddr ).Store( 0, 0 );

    u32 workItemsCount = !isLatePass ? pushBlock.instCount : device_ptr<u32>( pushBlock.occludedInstCounterAddr )[ 0 ];
    dispatch_command dispatchCmd = {
        ( workItemsCount + pushBlock.instCullWorkGrX - 1 ) / pushBlock.instCullWorkGrX,
        1, 1
    };
    device_ptr<dispatch_command>( pushBlock.dispatchCmdsAddr ).Store( 0, dispatchCmd );
}