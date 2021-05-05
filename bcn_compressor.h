#pragma once
#include "core_types.h"

void CompressToBc1_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex );
void CompressNormalMapToBc5_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex );