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

static const u32    MAX_WORKGROUP_COUNT = HT_WORKGROUP_COUNT.x * HT_WORKGROUP_COUNT.y;

[[vk::push_constant]]
downsampler_params  pushBlock;

using lds_ping_pong_t = bool;

static const lds_ping_pong_t PING = false;
static const lds_ping_pong_t PONG = true;

// NOTE: 16x16 bc first reduction is done within "wave mem".
// NOTE: we will use 2 arrays and ping pong between them to avoid mem stomping;
// yes, there are other methods but this is the simplest
groupshared float   ldsPing[ 16 ][ 16 ];
groupshared float   ldsPong[ 8 ][ 8 ];

groupshared u32     ldsWorkgrIdx;

// NOTE: the storage selection should be group uniform
float LDSLoadAtQuadID( u32x2 quadID, lds_ping_pong_t pingPong )
{
    return ( PING == pingPong ) ? ldsPing[ quadID.x ][ quadID.y ] : ldsPong[ quadID.x ][ quadID.y ];
}
void LDSStoreAtQuadID( u32x2 quadID, float val, lds_ping_pong_t pingPong )
{
    if( PING == pingPong )
    {
        ldsPing[ quadID.x ][ quadID.y ] = val;
        return;
    }
    ldsPong[ quadID.x ][ quadID.y ] = val;
}
void StoreMipTexel( u32x2 dstTexCoord, float val, u32 mipIdx )
{
    gRWTexture2D_float[ pushBlock.dstMipsIdx[ mipIdx ] ][ dstTexCoord ] = val;
}

// NOTE: all reaductions are 2x2 --> 1
// Min bc we use Reverse Z
float QuadMin( float4 val )
{
    return min( min( val.x, val.y ), min( val.z, val.w ) );
}

// NOTE: we use a reduction sampler so it taps a quad
// NOTE: the sampler is also clamped to edge which produces correct results here
// NOTE: our sampler doesn't use unnormalized coords
float SampleDownsampleMinQuadMaybeNonPOT(
    in Texture2D<float> srcTex,
    in SamplerState     reductionSampler,
    u32x2               unnormalizedDstUV,
    u32x2               srcRes,
    u32x2               dstRes,
    bool                isMip0FromNonPot // NOTE: should be uniform per dispatch
) {
    float minDepth = 1.0f; // max for reverse-Z
    if( isMip0FromNonPot )
    {
        // NOTE: bc it's non pot we have to reduce more
        u32x2 srcFloor = ( unnormalizedDstUV * srcRes ) / dstRes;
        u32x2 srcCeil = ( ( unnormalizedDstUV + 1 ) * srcRes + dstRes - 1 ) / dstRes;

        for( u32 sy = srcFloor.y; sy < srcCeil.y; sy += 2 )
        {
           for( u32 sx = srcFloor.x; sx < srcCeil.x; sx += 2 )
           {
                float2 uv = ( float2( sx, sy ) + 1.0f ) / float2( srcRes ); // + 1 bc we sample at corners
                minDepth = min( minDepth, srcTex.SampleLevel( reductionSampler, uv, 0 ) );
           }
        }
    }
    else
    {
        float2 uv = ( float2( unnormalizedDstUV ) + 0.5f ) / float2( srcRes );
        minDepth = srcTex.SampleLevel( reductionSampler, uv, 0 );
    }

    return minDepth;
}

float4 DownsampleMip0( u32x2 quadID, u32x2 wgID, u32x2 srcTexRes, u32x2 mip0Res, bool isMip0FromNonPot )
{
    SamplerState smp = samplers[ pushBlock.reductionSamplerIdx ];
    Texture2D<float> srcTex = gTexture2D_float[ pushBlock.srcSrvIdx ];

    u32x2 dstTileIdx = wgID * MIP0_TILE_SIZE;

    // NOTE: bc our input is most liekly NON POT but our depth pyramid is, we go from mip0 UV into src UV
    u32x2 dstUV0 = dstTileIdx + quadID + u32x2( 0,  0  );
    u32x2 dstUV1 = dstTileIdx + quadID + u32x2( 16, 0  );
    u32x2 dstUV2 = dstTileIdx + quadID + u32x2( 0,  16 );
    u32x2 dstUV3 = dstTileIdx + quadID + u32x2( 16, 16 );

    float q0 = SampleDownsampleMinQuadMaybeNonPOT( srcTex, smp, dstUV0, srcTexRes, mip0Res, isMip0FromNonPot );
    float q1 = SampleDownsampleMinQuadMaybeNonPOT( srcTex, smp, dstUV1, srcTexRes, mip0Res, isMip0FromNonPot );
    float q2 = SampleDownsampleMinQuadMaybeNonPOT( srcTex, smp, dstUV2, srcTexRes, mip0Res, isMip0FromNonPot );
    float q3 = SampleDownsampleMinQuadMaybeNonPOT( srcTex, smp, dstUV3, srcTexRes, mip0Res, isMip0FromNonPot );

   StoreMipTexel( dstUV0, q0, 0 );
   StoreMipTexel( dstUV1, q1, 0 );
   StoreMipTexel( dstUV2, q2, 0 );
   StoreMipTexel( dstUV3, q3, 0 );

    return float4( q0, q1, q2, q3 );
}

void DownsampleMip1( u32x2 quadID, u32x2 wgID, float4 q )
{
    float q0 = QuadMin( HTQuadBroadcast( q.x ) );
    float q1 = QuadMin( HTQuadBroadcast( q.y ) );
    float q2 = QuadMin( HTQuadBroadcast( q.z ) );
    float q3 = QuadMin( HTQuadBroadcast( q.w ) );

    if( HTIsQuadLeader( quadID ) )
    {
        u32x2 dstTileIdx = wgID * ( MIP0_TILE_SIZE / 2 );
        u32x2 texIdx0 = quadID / u32x2( 2, 2 ) + u32x2( 0, 0 );
        u32x2 texIdx1 = quadID / u32x2( 2, 2 ) + u32x2( 8, 0 );
        u32x2 texIdx2 = quadID / u32x2( 2, 2 ) + u32x2( 0, 8 );
        u32x2 texIdx3 = quadID / u32x2( 2, 2 ) + u32x2( 8, 8 );

        StoreMipTexel( dstTileIdx + texIdx0, q0, 1 );
        StoreMipTexel( dstTileIdx + texIdx1, q1, 1 );
        StoreMipTexel( dstTileIdx + texIdx2, q2, 1 );
        StoreMipTexel( dstTileIdx + texIdx3, q3, 1 );

        LDSStoreAtQuadID( texIdx0, q0, PING );
        LDSStoreAtQuadID( texIdx1, q1, PING );
        LDSStoreAtQuadID( texIdx2, q2, PING );
        LDSStoreAtQuadID( texIdx3, q3, PING );
    }
}

void DownsampleQuad( u32x2 quadID, u32x2 dstTileIdx, u32 dstMipIdx, lds_ping_pong_t isPingPong )
{
    float q0 = QuadMin( HTQuadBroadcast( LDSLoadAtQuadID( quadID, isPingPong ) ) );

    if( HTIsQuadLeader( quadID ) )
    {
        u32x2 dstQuadID = quadID / u32x2( 2, 2 );
        StoreMipTexel( dstTileIdx + dstQuadID, q0, dstMipIdx );
        LDSStoreAtQuadID( dstQuadID, q0, !isPingPong );
    }
}

void DownsampleNextThree( u32x2 quadID, u32x2 wgID, u32 srcMipIdx, lds_ping_pong_t isPingPong, u32 mipCount  )
{
    // 16x16 tiles
    DownsampleQuad( quadID, wgID * ( MIP0_TILE_SIZE / 4 ), srcMipIdx + 1, isPingPong );

    if( mipCount <= ( srcMipIdx + 2 ) ) return;

    GroupMemoryBarrierWithGroupSync();

    // 8x8 tiles
    if( all( quadID < 8 ) )
    {
        DownsampleQuad( quadID, wgID * ( MIP0_TILE_SIZE / 8 ), srcMipIdx + 2, !isPingPong );
    }

    if( mipCount <= ( srcMipIdx + 3 ) ) return;

    GroupMemoryBarrierWithGroupSync();

    // 4x4 tiles
    if( all( quadID < 4 ) )
    {
        DownsampleQuad( quadID, wgID * ( MIP0_TILE_SIZE / 16 ), srcMipIdx + 3, !( !isPingPong ) );
    }
}

void DownsampleTo2x2Coherent( u32x2 quadID, u32x2 wgID, u32 mipIdx, lds_ping_pong_t isPingPong )
{
    if( all( quadID < 2 ) )
    {
        float q0 = QuadMin( HTQuadBroadcast( LDSLoadAtQuadID( quadID, isPingPong ) ) );

        if( HTIsQuadLeader( quadID ) )
        {
            u32x2 dstTileIdx = wgID * ( MIP0_TILE_SIZE / 32 ); // 1 essentially
            HTStoreCoherentImageFloat( pushBlock.dstMipsIdx[ mipIdx ], dstTileIdx, q0 );
        }
    }
}

void DownsampleTo2x2( u32x2 quadID, u32x2 wgID, u32 mipIdx, lds_ping_pong_t isPingPong )
{
    if( all( quadID < 2 ) )
    {
        float q0 = QuadMin( HTQuadBroadcast( LDSLoadAtQuadID( quadID, isPingPong ) ) );

        if( HTIsQuadLeader( quadID ) )
        {
            u32x2 dstTileIdx = wgID * ( MIP0_TILE_SIZE / 32 ); // 1 essentially
            StoreMipTexel( dstTileIdx, q0, mipIdx );
        }
    }
}

// NOTE: there's no coherent sample in SPIR-V, tap manually
// NOTE: we clamp so our WG work is uniform, Vulkan spec mandates that OOB IMAGE writes are discarded; we're safe
float4 CoherentLoadQuadSafe( u32 imgIdx, u32x2 topLeftUV, u32x2 srcRes )
{
    return float4(
        HTLoadCoherentImageFloat( imgIdx, min( topLeftUV + u32x2( 0, 0 ), srcRes - 1 ) ),
        HTLoadCoherentImageFloat( imgIdx, min( topLeftUV + u32x2( 1, 0 ), srcRes - 1 ) ),
        HTLoadCoherentImageFloat( imgIdx, min( topLeftUV + u32x2( 0, 1 ), srcRes - 1 ) ),
        HTLoadCoherentImageFloat( imgIdx, min( topLeftUV + u32x2( 1, 1 ), srcRes - 1 ) )
    );
}

float4 DownsampleMip6( u32x2 quadID, u32x2 mip5Res )
{
    u32 mip5Idx = pushBlock.dstMipsIdx[ 5 ];

    float q0 = QuadMin( CoherentLoadQuadSafe( mip5Idx, quadID * u32x2( 4, 4 ) + u32x2( 0, 0 ), mip5Res ) );
    float q1 = QuadMin( CoherentLoadQuadSafe( mip5Idx, quadID * u32x2( 4, 4 ) + u32x2( 2, 0 ), mip5Res ) );
    float q2 = QuadMin( CoherentLoadQuadSafe( mip5Idx, quadID * u32x2( 4, 4 ) + u32x2( 0, 2 ), mip5Res ) );
    float q3 = QuadMin( CoherentLoadQuadSafe( mip5Idx, quadID * u32x2( 4, 4 ) + u32x2( 2, 2 ), mip5Res ) );

    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 0, 0 ), q0, 6 );
    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 1, 0 ), q1, 6 );
    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 0, 1 ), q2, 6 );
    StoreMipTexel( quadID * u32x2( 2, 2 ) + u32x2( 1, 1 ), q3, 6 );

    return float4( q0, q1, q2, q3 );
}

void DownsampleMip7( u32x2 quadID, float4 q )
{
    float q0 = QuadMin( q );
    StoreMipTexel( quadID, q0, 7 );
    LDSStoreAtQuadID( quadID, q0, PING );
}

// NOTE: think of this as AMD's SPD BUT, since we're gonna mostly use it on non POT, we basically DROP SPD_MIP0,
// and start at SPD_MIP1 and also use that as our dispatch size and cap
// NOTE:
// 256 threads = 16x16 lanes
//    32x32 mip0   (4 texels/thread, registers)
//    16x16 mip1   quad ops
//      ___________
//     8x8  mip2   |
//     4x4  mip3   | - LDS
//     2x2  mip4   |
//     1x1  mip5   |   <- 6 levels per WG
[shader( "compute" )]
[numthreads( 256, 1, 1 )]
void DownsamplerCsMain( u32 localLinearID : SV_GroupIndex, u32x3 workgroupID : SV_GroupID )
{
    u32x2 quadID = ThreadIndexToQuadCoord16x16( localLinearID );

    float4 q = DownsampleMip0( quadID, workgroupID.xy, pushBlock.srcResolution,
        pushBlock.mip0Resolution, bool( pushBlock.isMip0FromNonPot ) );

    if( pushBlock.mipCount <= 1 ) return;

    DownsampleMip1( quadID, workgroupID.xy, q );
    if( pushBlock.mipCount <= 2 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleNextThree( quadID, workgroupID.xy, 1, PING, pushBlock.mipCount );

    if( pushBlock.mipCount <= 5 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo2x2Coherent( quadID, workgroupID.xy, 5, PONG );
    if( pushBlock.mipCount <= 6 ) return;

    // NOTE: need to flush mip5 if we continue
    DeviceMemoryBarrierWithGroupSync();

    // NOTE: here we will let the LAST wg carry on downsampling the remaning mips
    if( 0 == localLinearID )
    {
        ldsWorkgrIdx = BufferAtomicAdd( pushBlock.atomicWgCounterIdx, 1 );
    }
    GroupMemoryBarrierWithGroupSync();

    if( (  MAX_WORKGROUP_COUNT - 1 ) != ldsWorkgrIdx ) return;

    float4 q6 = DownsampleMip6( quadID, pushBlock.mip0Resolution >> 5 );

    // From here on the stages mostly reapeat
    if( pushBlock.mipCount <= 7 ) return;
     // no barrier needed, working on values only from the same thread

    DownsampleMip7( quadID, q6 );
    if( pushBlock.mipCount <= 8 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleNextThree( quadID, u32x2( 0, 0 ), 7, PING, pushBlock.mipCount );

    if( pushBlock.mipCount <= 11 ) return;

    GroupMemoryBarrierWithGroupSync();

    DownsampleTo2x2( quadID, u32x2( 0, 0 ), 11, PONG );
}