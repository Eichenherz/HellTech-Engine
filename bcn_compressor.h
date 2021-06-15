#pragma once
#include "core_types.h"

inline constexpr u64 blockSizeX = 4;
inline constexpr u64 blockSizeY = 4;
inline constexpr u64 bc1BytesPerBlock = 8;
inline constexpr u64 bc5BytesPerBlock = 16;
// TODO: type safe ?
inline u64 GetBCTexByteCount( u64 width, u64 height, u64 bytesPerBlock )
{
	u64 xBlocksCount = width / blockSizeX;
	u64 yBlocksCount = height / blockSizeY;
	u64 blockCount = xBlocksCount * yBlocksCount;

	return blockCount * bytesPerBlock;
}

void CompressToBc1_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex );
void CompressNormalMapToBc5_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex );
void CompressMetalRoughMapToBc5_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex );

using PfnBcnCompress_SIMD = void ( * )( const u8* texSrc, u64 width, u64 height, u8* outCmpTex );