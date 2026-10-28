// NOTE: second unity TU; these collide on static symbol names with the files in 3rdPartyTU.cpp
#include <rgbcx.cpp>

#include <meshletcodec.cpp>
#include <partition.cpp>
#include <simplifier.cpp>
#include <spatialorder.cpp>
#include <vcacheoptimizer.cpp>

// NOTE: zstd unity build, mirrors fb_zstd/build/single_file_libs/zstd-in.c ( no dictBuilder, no ASM )
#define DEBUGLEVEL 0
#define MEM_MODULE
#undef  XXH_NAMESPACE
#define XXH_NAMESPACE ZSTD_
#undef  XXH_PRIVATE_API
#define XXH_PRIVATE_API
#undef  XXH_INLINE_ALL
#define XXH_INLINE_ALL
#define ZSTD_LEGACY_SUPPORT 0
#define ZSTD_MULTITHREAD
#define ZSTD_TRACE 0
#define ZSTD_DISABLE_ASM 1

#define ZSTD_DEPS_NEED_MALLOC
#define ZSTD_DEPS_NEED_MATH64
#include <common/zstd_deps.h>

#include <common/debug.c>
#include <common/entropy_common.c>
#include <common/error_private.c>
#include <common/fse_decompress.c>
#include <common/threading.c>
#include <common/pool.c>
#include <common/zstd_common.c>

#include <compress/fse_compress.c>
#include <compress/hist.c>
#include <compress/huf_compress.c>
#include <compress/zstd_compress_literals.c>
#include <compress/zstd_compress_sequences.c>
#include <compress/zstd_compress_superblock.c>
#include <compress/zstd_preSplit.c>
#include <compress/zstd_compress.c>
#include <compress/zstd_double_fast.c>
#include <compress/zstd_fast.c>
#include <compress/zstd_lazy.c>
#include <compress/zstd_ldm.c>
#include <compress/zstd_opt.c>
#include <compress/zstdmt_compress.c>

#include <decompress/huf_decompress.c>
#include <decompress/zstd_ddict.c>
#include <decompress/zstd_decompress.c>
#include <decompress/zstd_decompress_block.c>