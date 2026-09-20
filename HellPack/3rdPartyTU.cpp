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