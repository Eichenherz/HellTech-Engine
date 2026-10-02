// TODO: rename this to MaxOptimizedTU.cpp
// NOTE: we need this in order to compile 3rdParty libs OPTIMIZED
#define STB_IMAGE_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <bc7enc.cpp>

// NOTE: meshopt group A; the files below share no static symbol names ( see 3rdPartyTU2.cpp for the rest )
#include <allocator.cpp>
#include <clusterizer.cpp>
#include <indexanalyzer.cpp>
#include <indexcodec.cpp>
#include <indexgenerator.cpp>
#include <meshletutils.cpp>
#include <opacitymap.cpp>
#include <overdrawoptimizer.cpp>
#include <quantization.cpp>
#include <rasterizer.cpp>
#include <stripifier.cpp>
#include <tangentspace.cpp>
#include <vertexcodec.cpp>
#include <vertexfilter.cpp>
#include <vfetchoptimizer.cpp>

#include <range_utils.h>
// TODO: if we sort on floats/doubles we need to move this to a TU with /fp:precise ( /fp:fast breaks the lib's inf sentinels + NaN handling )
#include <x86simdsort-static-incl.h>

namespace ht
{
    void kv_qsort( KV_QSORT_ELEM_T auto* keys, KV_QSORT_ELEM_T auto* vals, u64 count )
    {
        x86simdsortStatic::keyvalue_qsort( keys, vals, count );
    }

    template void kv_qsort<u32, u32>( u32*, u32*, u64 );
    template void kv_qsort<u64, u32>( u64*, u32*, u64 );
}