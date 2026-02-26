#ifndef __HP_BCN_COMPRESSION_H__
#define __HP_BCN_COMPRESSION_H__

#include "core_types.h"
#include "hp_error.h"

#include <bc7enc.h>
#include <rgbcx.h>

#include <atomic>
#include <array>
#include <thread>
#include <span>
#include <vector>

// NOTE: only BC2, BC3, BC5, BC6H, BC7
constexpr u64 BLOCK_PIXEL_PITCH = 4;
constexpr u64 BLOCK_SIZE_IN_BYTES = 4 * 4 * BLOCK_PIXEL_PITCH;


enum class bc_format_t : u8
{
    BC7_RGBA = 0,
    BC5_RG   = 1,
};

constexpr u32 BCnFormatToBlockSizeInBytes( bc_format_t fmt )
{
    using enum bc_format_t;
    switch( fmt )
    {
    case BC7_RGBA: return BC7ENC_BLOCK_SIZE;
    case BC5_RG: return 16;
    default: HP_ASSERT( false && "Not implemented yet" );
    }
}

struct bcn_compression_result
{
    std::vector<u8>         data;

    bcn_compression_result() = default;
    bcn_compression_result( u64 blocksX, u64 blocksY, u64 blockSzInBytes )
    {
        data.resize( blocksX * blocksY * blockSzInBytes );
    }
};

using rgba4x4 = std::array<u8, BLOCK_SIZE_IN_BYTES>;

// TODO: check rowPitch !!!!
inline rgba4x4 GatherBlockRGBA8_Clamp( std::span<const u8> rgba8, u32 width, u32 height, u32 blockX, u32 blockY )
{
    rgba4x4 blockRGBA;
    [[unroll]]
    for( u32 by = 0; by < 4; ++by )
    {
        u32 y = blockY * 4u + by;
        if( y >= height ) y = height - 1u;
        [[unroll]]
        for( u32 bx = 0; bx < 4; ++bx )
        {
            u32 x = blockX * 4u + bx;
            if( x >= width ) x = width - 1u;

            u64 src = ( y * width + x ) * BLOCK_PIXEL_PITCH;
            u64 dst = ( by * 4u + bx ) * BLOCK_PIXEL_PITCH;

            blockRGBA[ dst + 0 ] = rgba8[ src + 0 ];
            blockRGBA[ dst + 1 ] = rgba8[ src + 1 ];
            blockRGBA[ dst + 2 ] = rgba8[ src + 2 ];
            blockRGBA[ dst + 3 ] = rgba8[ src + 3 ];
        }
    }
    return blockRGBA;
}

inline bcn_compression_result CompressRBGA8ToBC7( std::span<const u8> rgba8, u16 width, u16 height ) 
{
    constexpr u32 bcnBlockSzInBytes = BCnFormatToBlockSizeInBytes( bc_format_t::BC7_RGBA );

    u32 blocksX = ( width + 3u ) / 4u;
    u32 blocksY = ( height + 3u ) / 4u;
    u32 blockCount = blocksX * blocksY;
    
    bcn_compression_result bcn = { blocksX, blocksY, bcnBlockSzInBytes };

    HP_ASSERT( std::size( bcn.data ) );

    // TODO: don't call these evey job ?
    bc7enc_compress_block_params bc7CmpParams;
    bc7enc_compress_block_params_init( &bc7CmpParams );
    bc7enc_compress_block_init();
    for( u32 bi = 0; bi < blockCount; ++bi )
    {
        u32 by = bi / blocksX;
        u32 bx = bi - by * blocksX;

        rgba4x4 blockRGBA = GatherBlockRGBA8_Clamp( rgba8, width, height, bx, by );
        u8* pDstBlock = std::data( bcn.data ) + bi * bcnBlockSzInBytes;

        bc7enc_compress_block( pDstBlock, std::data( blockRGBA ), &bc7CmpParams );
    }

    return bcn;
}

inline bcn_compression_result CompressRBGA8ToBC5( std::span<const u8> rgba8, u16 width, u16 height )
{
    constexpr u32 bcnBlockSzInBytes = BCnFormatToBlockSizeInBytes( bc_format_t::BC5_RG );

    u32 blocksX = ( width + 3u ) / 4u;
    u32 blocksY = ( height + 3u ) / 4u;
    u32 blockCount = blocksX * blocksY;

    bcn_compression_result bcn = { blocksX, blocksY, bcnBlockSzInBytes };

    HP_ASSERT( std::size( bcn.data ) );

    rgbcx::init( rgbcx::bc1_approx_mode::cBC1Ideal );
    for( u32 bi = 0; bi < blockCount; ++bi )
    {
        u32 by = bi / blocksX;
        u32 bx = bi - by * blocksX;

        rgba4x4 blockRGBA = GatherBlockRGBA8_Clamp( rgba8, width, height, bx, by );
        u8* pDstBlock = std::data( bcn.data ) + bi * bcnBlockSzInBytes;

        rgbcx::encode_bc5( pDstBlock, std::data( blockRGBA ), 0, 1, 4u );
    }

    return bcn;
}

inline bcn_compression_result CompressRGBA8ToBCn( std::span<const u8> rgba8, u32 width, u32 height, bc_format_t format ) 
{
    if( bc_format_t::BC7_RGBA == format )
    {
        return CompressRBGA8ToBC7( rgba8, width, height );
    }
    else if( bc_format_t::BC5_RG == format )
    {
        return CompressRBGA8ToBC5( rgba8, width, height );
    }
    else
    {
        HP_ASSERT( false && "Unsupported type" );
    }
}

#endif // !__HP_BCN_COMPRESSION_H__
