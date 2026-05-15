// NOTE: heavily inspired by AMD's SPD
#include "ht_renderer_types.h"
#include "ht_hlsl_lang.h"

// https://martinfullerblog.wordpress.com/2023/02/01/compute-shader-thread-index-to-2d-coordinate/
/*
    MIT License
    Copyright (c) Martin Fuller
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:
    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE
*/
/*
    Generate correct mapping for HLSL derivatives and QuadReadAccross

    Sightly more expensive square version produces:
     0  1  4  5 16 17 20 21
     2  3  6  7 18 19 22 23
     8  9 12 13 24 25 28 29
    10 11 14 15 26 27 30 31
    32 33 36 37 48 49 52 53
    34 35 38 39 50 51 54 55
    40 41 44 45 56 57 60 61
    42 43 46 47 58 59 62 63

    // 3 periods of tiling, non optimised for clarity:
    x = ( index & 1      ) + ((index & 4) >> 1) + ((index & 16) >> 2);
    y = ((index & 2) >> 1) + ((index & 8) >> 2) + ((index & 32) >> 3);
*/
u32x2 ThreadIndexToQuadCoordSquare( u32 threadIndex )
{
    // duplicate index in top 16 bits, but pre-shifted right by 1
    threadIndex |= threadIndex << 15;
    // two bitwise ANDS for the price of one
    u32x2 coord;
    coord.x  = threadIndex & 0x10001;
    coord.x |= ( threadIndex >> 1 ) & 0x20002;
    coord.x |= ( threadIndex >> 2 ) & 0x40004;
    coord.y = coord.x >> 16;
    coord.x &= 0x7;

    return coord;
}

// NOTE: now, we got 256 threads so we need to extend this algo
// COMPACT VIEW — bit routing from tid to ( x, y )
//
// tid bits:     b7  b6  b5  b4  b3  b2  b1  b0
//               |   |   |   |   |   |   |   |
//               v   v   v   v   v   v   v   v
//               y3  x3  y2  x2  y1  x1  y0  x0
//
// tid = ...y3 x3 y2 x2 y1 x1 y0 x0    (Morton interleaved)
//
// x = b6 b4 b2 b0   (every even-positioned bit of i, packed down)
// y = b7 b5 b3 b1   (every odd-positioned bit of i, packed down)
//
//   bit pair  scale     selects
//   ────────  ────────  ─────────────────────────
//   b1, b0    2×2       position within a quad
//   b3, b2    4×4       which quad in a 4×4 block
//   b5, b4    8×8       which 4×4 in an 8×8 block
//   b7, b6    16×16     which 8×8 in the 16×16 WG

u32x2 ThreadIndexToQuadCoord16x16( u32 tid )
{
    u32 x = ( tid & 1 ) + ( ( tid & 4 ) >> 1 ) + ( ( tid & 16 ) >> 2 ) + ( ( tid & 64 ) >> 3) ;
    u32 y = ( ( tid & 2 ) >> 1 ) + ( ( tid & 8 ) >> 2 ) + ( ( tid & 32 ) >> 3 ) + ( ( tid & 128 ) >> 4 );
    return u32x2( x, y) ;
}

static const u32x2  TILE_SIZE = u32x2( 64, 64 );
static const u32    MAX_WORKGROUP_COUNT = HT_WORKGROUP_COUNT.x * HT_WORKGROUP_COUNT.y;

[[vk::push_constant]]
downsampler_params  pushBlock;

// NOTE: 16x16 bc first reduction is done within "wave mem".
groupshared float   ldsTemp[ 16 ][ 16 ];

groupshared u32     ldsWorkgrIdx;

float LDSLoadAtQuadID( u32x2 quadID )
{
    return ldsTemp[ quadID.x ][ quadID.y ];
}
void LDSStoreAtQuadID( u32x2 quadID, float val )
{
    ldsTemp[ quadID.x ][ quadID.y ] = val;
}
void StoreMipTexel( u32x2 dstTexCoord, float val, u32 mipIdx )
{
    gRWTexture2D_float[ pushBlock.dstMipsIdx[ mipIdx ] ][ dstTexCoord ] = val;
}

// NOTE: all reaductions are 2x2 --> 1
// Min bc we use Reverse Z
float DownsampleReduce2x2( float4 val )
{
    return min( min( val.x, val.y ), min( val.z, val.w ) );
}

float4 DownsampleMip0( u32x2 quadID, u32x2 wgID )
{
    SamplerState smp = samplers[ pushBlock.samplerIdx ];
    Texture2D<float> srcTex = gTexture2D_float[ pushBlock.srcSrvIdx ];

    u32x2 srcTileIdx = wgID * TILE_SIZE;
    // NOTE: we use a reduction sampler so it taps a quad
    float q0 = srcTex.SampleLevel( smp, srcTileIdx + u32x2( 2, 2 ) * quadID + u32x2( 0,  0  ), 0 );
    float q1 = srcTex.SampleLevel( smp, srcTileIdx + u32x2( 2, 2 ) * quadID + u32x2( 32, 0  ), 0 );
    float q2 = srcTex.SampleLevel( smp, srcTileIdx + u32x2( 2, 2 ) * quadID + u32x2( 0,  32 ), 0 );
    float q3 = srcTex.SampleLevel( smp, srcTileIdx + u32x2( 2, 2 ) * quadID + u32x2( 32, 32 ), 0 );

    u32x2 dstTileIdx = wgID * ( TILE_SIZE / 2 );

   StoreMipTexel( dstTileIdx + quadID + u32x2( 0,  0  ), q0, 0 );
   StoreMipTexel( dstTileIdx + quadID + u32x2( 16, 0  ), q1, 0 );
   StoreMipTexel( dstTileIdx + quadID + u32x2( 0,  16 ), q2, 0 );
   StoreMipTexel( dstTileIdx + quadID + u32x2( 16, 16 ), q3, 0 );

    return float4( q0, q1, q2, q3 );
}

void DownsampleMip1( u32x2 quadID, u32x2 wgID, float4 q )
{
    float q0 = DownsampleReduce2x2( HTQuadBroadcast( q.x ) );
    float q1 = DownsampleReduce2x2( HTQuadBroadcast( q.y ) );
    float q2 = DownsampleReduce2x2( HTQuadBroadcast( q.z ) );
    float q3 = DownsampleReduce2x2( HTQuadBroadcast( q.w ) );

    if( HTIsQuadLeader( quadID ) )
    {
        u32x2 dstTileIdx = wgID * ( TILE_SIZE / 4 );
        u32x2 texIdx0 = quadID / u32x2( 2, 2 ) + u32x2( 0, 0 );
        u32x2 texIdx1 = quadID / u32x2( 2, 2 ) + u32x2( 8, 0 );
        u32x2 texIdx2 = quadID / u32x2( 2, 2 ) + u32x2( 0, 8 );
        u32x2 texIdx3 = quadID / u32x2( 2, 2 ) + u32x2( 8, 8 );

        StoreMipTexel( dstTileIdx + texIdx0, q0, 0 );
        StoreMipTexel( dstTileIdx + texIdx1, q1, 0 );
        StoreMipTexel( dstTileIdx + texIdx2, q2, 0 );
        StoreMipTexel( dstTileIdx + texIdx3, q3, 0 );

        LDSStoreAtQuadID( texIdx0, q0 );
        LDSStoreAtQuadID( texIdx1, q1 );
        LDSStoreAtQuadID( texIdx2, q2 );
        LDSStoreAtQuadID( texIdx3, q3 );
    }
}

void DownsampleQuad( u32x2 quadID, u32x2 dstTileIdx, u32 mipIdx )
{
    float q0 = DownsampleReduce2x2( HTQuadBroadcast( LDSLoadAtQuadID( quadID ) ) );

    if( HTIsQuadLeader( quadID ) )
    {
        u32x2 dstQuadID = quadID / u32x2( 2, 2 );
        StoreMipTexel( dstTileIdx + dstQuadID, q0, mipIdx );
        LDSStoreAtQuadID( dstQuadID, q0 );
    }
}

void DownsampleTo16x16( u32x2 quadID, u32x2 wgID, u32 mipIdx )
{
    DownsampleQuad( quadID, wgID * ( TILE_SIZE / 8 ), mipIdx );
}

void DownsampleTo8x8( u32x2 quadID, u32x2 wgID, u32 mipIdx )
{
    if( all( quadID < 8 ) )
    {
        DownsampleQuad( quadID, wgID * ( TILE_SIZE / 16 ), mipIdx );
    }
}

void DownsampleTo4x4( u32x2 quadID, u32x2 wgID, u32 mipIdx )
{
    if( all( quadID < 4 ) )
    {
        DownsampleQuad( quadID, wgID * ( TILE_SIZE / 32 ), mipIdx );
    }
}

void DownsampleTo2x2Coherent( u32x2 quadID, u32x2 wgID, u32 mipIdx )
{
    if( all( quadID < 2 ) )
    {
        float q0 = DownsampleReduce2x2( HTQuadBroadcast( LDSLoadAtQuadID( quadID ) ) );

        if( HTIsQuadLeader( quadID ) )
        {
            u32x2 dstTileIdx = wgID * ( TILE_SIZE / 64 ); // 1 essentially
            HTStoreCoherentImageFloat( pushBlock.dstMipsIdx[ mipIdx ], dstTileIdx, q0 );
        }
    }
}

void DownsampleTo2x2( u32x2 quadID, u32x2 wgID, u32 mipIdx )
{
    if( all( quadID < 2 ) )
    {
        float q0 = DownsampleReduce2x2( HTQuadBroadcast( LDSLoadAtQuadID( quadID ) ) );

        if( HTIsQuadLeader( quadID ) )
        {
            u32x2 dstTileIdx = wgID * ( TILE_SIZE / 64 ); // 1 essentially
            StoreMipTexel( dstTileIdx, q0, mipIdx );
        }
    }
}

float4 DownsampleMip6( u32x2 quadID )
{
    u32x2 tex0 = quadID * u32x2( 4, 4 ) + u32x2( 0, 0 );
    u32x2 tex1 = quadID * u32x2( 4, 4 ) + u32x2( 2, 0 );
    u32x2 tex2 = quadID * u32x2( 4, 4 ) + u32x2( 0, 2 );
    u32x2 tex3 = quadID * u32x2( 4, 4 ) + u32x2( 2, 2 );

    float q0 = HTLoadCoherentImageFloat( pushBlock.dstMipsIdx[ 5 ], tex0 );
    float q1 = HTLoadCoherentImageFloat( pushBlock.dstMipsIdx[ 5 ], tex1 );
    float q2 = HTLoadCoherentImageFloat( pushBlock.dstMipsIdx[ 5 ], tex2 );
    float q3 = HTLoadCoherentImageFloat( pushBlock.dstMipsIdx[ 5 ], tex3 );

    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 0, 0 ), q0, 6 );
    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 1, 0 ), q1, 6 );
    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 0, 1 ), q2, 6 );
    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 1, 1 ), q3, 6 );

    return float4( q0, q1, q2, q3 );
}

void DownsampleMip7( u32x2 quadID, float4 q )
{
    float q0 = DownsampleReduce2x2( q );
    StoreMipTexel( quadID, q0, 7 );
    LDSStoreAtQuadID( quadID, q0 );
}

/*
OVERVIEW:

MIP 0 SRC 64x64 (dst wgID*64)      |    MIP 1 DST 32x32 (dst wgID*32)       |    MIP 2 DST 16x16 (dst wgID*16)
    0        32       64           |        0   16   32                     |        0    8   16
  0 +--------+--------+            |      0 +----+----+                     |      0 +----+----+
    | q0     | q1     |            |        | q0 | q1 |                     |        | q0 | q1 |
    | 32x32  | +32 X  |            |        |16x | +16|                     |        |    | +8 |
    | 16x16  |        |            |        |str1|  X |                     |        |    |  X |
    | str 2  |        |            |     16 +----+----+                     |      8 +----+----+              -------
 32 +--------+--------+            |        | q2 | q3 |                     |        | q2 | q3 |                    |
    | q2     | q3     |            |        |+16 |+16 |                     |        | +8 | +8 |                    |
    | +32 Y  | +32 X,Y|            |        |  Y | X,Y|                     |        | Y  | X,Y|                    |
    |        |        |            |     32 +----+----+                     |     16 +----+----+                    |
    |        |        |            |   ReduceQuad -> filter (qid&1)==0 ->   |   ReduceQuad -> 16 leaders ->         |
 64 +--------+--------+            |     64 leaders -> 4 stores each        |    1 store each from here on          |
sample2x2 -> 4 dst writes/thread                                                                                    |
                                                                                                                    |
            ---------------------------------------------------------------------------------------------------------
            |
            V

MIP 3 DST 8x8 (dst wgID*8)         |    MIP 4 DST 4x4 (dst wgID*4)         |    MIP 5 DST 2x2 (dst wgID*2)
    0    4    8                    |        0    2    4                    |        0    1    2
  0 +----+                         |      0 +----+                         |      0 +----+
    | q  |   ( only top-left;      |        | q  |                         |        | q  |                   -------
    |4x4 |   no sub-tile fanout)   |        |2x2 |                         |        |1x1 |                         |
    |str1|                         |      2 +----+                         |      1 +----+                         |
  4 +----+                         |      active threads: tid < 4          |      active threads: tid < 2          |
  active threads: tid < 8          |      stores at: wgID*4 + tid/2        |      stores at: wgID*2 + tid/2        |
  stores at: wgID*8 + tid/2        |      (range 0..1)                     |      (range 0..0)                     |
  (range 0..3)                                                                                                     |
                                                                                                                   |
                                                                                                                   |
                ----------------------------------------------------------------------------------------------------
                |
                V

  MIP 6 DST 1x1 (dst wgID)  <-- globallycoherent boundary
    active: tid == (0,0) only
    stores 1 texel per WG at wgID
    (every WG writes here -> last WG reads this back for mip 7+)
*/
[[vk::ext_capability( spv::ComputeDerivativeGroupLinearKHR )]]
[[vk::ext_extension( "SPV_KHR_compute_shader_derivatives" )]]
[shader( "compute" )]
[numthreads( 256, 1, 1 )]
void DownsamplerCsMain( u32 localLinearID : SV_GroupIndex, u32x3 workgroupID : SV_GroupID )
{
    u32x2 quadID = ThreadIndexToQuadCoord16x16( localLinearID );

    float4 q = DownsampleMip0( quadID, workgroupID.xy );
    if( pushBlock.mipCount <= 1 ) return;

    DownsampleMip1( quadID, workgroupID.xy, q );
    if( pushBlock.mipCount <= 2 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo16x16( quadID, workgroupID.xy, 2 );
    if( pushBlock.mipCount <= 3 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo8x8( quadID, workgroupID.xy, 3 );
    if( pushBlock.mipCount <= 4 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo4x4( quadID, workgroupID.xy, 4 );
    if( pushBlock.mipCount <= 5 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo2x2Coherent( quadID, workgroupID.xy, 5 );
    if( pushBlock.mipCount <= 6 ) return;


    // NOTE: here we will let the LAST wg carry on downsampling the remaning mips
    if( 0 == localLinearID )
    {
        ldsWorkgrIdx = BufferAtomicAdd( pushBlock.atomicWgCounterIdx, 1 );
    }
    GroupMemoryBarrierWithGroupSync();

    if( (  MAX_WORKGROUP_COUNT - 1 ) != ldsWorkgrIdx ) return;

    float4 q6 = DownsampleMip6( quadID );
    // MIP 6 dst 1x1

    // From here on the stages mostly reapeate
    if( pushBlock.mipCount <= 7 ) return;
     // no barrier needed, working on values only from the same thread

    DownsampleMip7( quadID, q6 );
    if( pushBlock.mipCount <= 8 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo16x16( quadID, u32x2( 0, 0 ), 8 );
    if( pushBlock.mipCount <= 9 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo8x8( quadID, u32x2( 0, 0 ), 9 );
    if( pushBlock.mipCount <= 10 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo4x4( quadID, u32x2( 0, 0 ), 10 );
    if( pushBlock.mipCount <= 11 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo2x2( quadID, u32x2( 0, 0 ), 11 );
}