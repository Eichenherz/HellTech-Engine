#include "bcn_compressor.h"

#include <assert.h>
#include <utility>
#include <tuple>
// TODO: fix this shit
#define __SSE2__ 
#define __SSE3__
#define __SSSE3__ 
#define __SSE4_2__
#include <immintrin.h>

#if !defined( R_SHUFFLE_D )
#define R_SHUFFLE_D( x, y, z, w )	(( (w) & 3 ) << 6 | ( (z) & 3 ) << 4 | ( (y) & 3 ) << 2 | ( (x) & 3 ))
#endif

// NOTE: uber-heavily-inspired from Doom 3 BFG and J.M.P. van Waveren 's work, mostly 
constexpr u64 INSET_COLOR_SHIFT = 4;	// inset the bounding box with ( range >> shift )
constexpr u64 INSET_ALPHA_SHIFT = 5;	// inset alpha channel

constexpr u64 C565_5_MASK = 0xF8;		// 0xFF minus last three bits
constexpr u64 C565_6_MASK = 0xFC;		// 0xFF minus last two bits


alignas( 16 ) constexpr u16 SIMD_WORD_insetShift[ 8 ] = { 
	1 << ( 16 - INSET_COLOR_SHIFT ), 
	1 << ( 16 - INSET_COLOR_SHIFT ), 
	1 << ( 16 - INSET_COLOR_SHIFT ), 
	1 << ( 16 - INSET_ALPHA_SHIFT ), 
	0, 0, 0, 0 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetNormalBc5Round[ 8 ] = {
	( ( 1 << ( INSET_ALPHA_SHIFT - 1 ) ) - 1 ), ( ( 1 << ( INSET_ALPHA_SHIFT - 1 ) ) - 1 ), 0, 0, 0, 0, 0, 0 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetNormalBc5Mask[ 8 ] = { 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetNormalBc5ShiftUp[ 8 ] = { 1 << INSET_ALPHA_SHIFT, 1 << INSET_ALPHA_SHIFT, 1, 1, 1, 1, 1, 1 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetNormalBc5ShiftDown[ 8 ] = {
	1 << ( 16 - INSET_ALPHA_SHIFT ), 1 << ( 16 - INSET_ALPHA_SHIFT ), 0, 0, 0, 0, 0, 0 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetMetalRoughBc5Round[ 8 ] = {
	( 0, ( 1 << ( INSET_ALPHA_SHIFT - 1 ) ) - 1 ), ( ( 1 << ( INSET_ALPHA_SHIFT - 1 ) ) - 1 ), 0, 0, 0, 0, 0 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetMetalRoughBc5Mask[ 8 ] = { 0x0000, 0xFFFF, 0xFFFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetMetalRoughBc5ShiftUp[ 8 ] = { 
	1, 1 << INSET_ALPHA_SHIFT, 1 << INSET_ALPHA_SHIFT, 1, 1, 1, 1, 1 };
alignas( 16 ) constexpr u16 SIMD_WORD_insetMetalRoughBc5ShiftDown[ 8 ] = {
	0, 1 << ( 16 - INSET_ALPHA_SHIFT ), 1 << ( 16 - INSET_ALPHA_SHIFT ), 0, 0, 0, 0, 0 };


alignas( 16 ) constexpr u8 SIMD_BYTE_colorMask[ 16 ] = {
	C565_5_MASK, C565_6_MASK, C565_5_MASK, 0x00,
	0x00, 0x00, 0x00, 0x00,
	C565_5_MASK, C565_6_MASK, C565_5_MASK, 0x00,
	0x00, 0x00, 0x00, 0x00 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask0[ 4 ] = { 7 << 0, 0, 7 << 0, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask1[ 4 ] = { 7 << 3, 0, 7 << 3, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask2[ 4 ] = { 7 << 6, 0, 7 << 6, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask3[ 4 ] = { 7 << 9, 0, 7 << 9, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask4[ 4 ] = { 7 << 12, 0, 7 << 12, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask5[ 4 ] = { 7 << 15, 0, 7 << 15, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask6[ 4 ] = { 7 << 18, 0, 7 << 18, 0 };
alignas( 16 ) constexpr u32 SIMD_DWORD_alphaBitMask7[ 4 ] = { 7 << 21, 0, 7 << 21, 0 };

alignas( 16 ) constexpr u16 SIMD_WORD_divBy3[ 8 ] = {
	( 1 << 16 ) / 3 + 1, ( 1 << 16 ) / 3 + 1, ( 1 << 16 ) / 3 + 1, ( 1 << 16 ) / 3 + 1, 
	( 1 << 16 ) / 3 + 1, ( 1 << 16 ) / 3 + 1, ( 1 << 16 ) / 3 + 1, ( 1 << 16 ) / 3 + 1 };
alignas( 16 ) constexpr u16 SIMD_WORD_divBy14[ 8 ] = { 
	( 1 << 16 ) / 14 + 1, ( 1 << 16 ) / 14 + 1, ( 1 << 16 ) / 14 + 1, ( 1 << 16 ) / 14 + 1, 
	( 1 << 16 ) / 14 + 1, ( 1 << 16 ) / 14 + 1, ( 1 << 16 ) / 14 + 1, ( 1 << 16 ) / 14 + 1 };

alignas( 16 ) constexpr u8 SIMD_BYTE_1[ 16 ] = { 
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01 };
alignas( 16 ) constexpr u8 SIMD_BYTE_2[ 16 ] = {
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02 };
alignas( 16 ) constexpr u8 SIMD_BYTE_7[ 16 ] = {
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07 };
alignas( 16 ) constexpr u8 SIMD_BYTE_8[ 16 ] = {
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08 };
alignas( 16 ) constexpr u16 SIMD_WORD_1[ 8 ] = { 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001 };
alignas( 16 ) constexpr u16 SIMD_WORD_2[ 8 ] = { 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002, 0x0002 };
alignas( 16 ) constexpr u16 SIMD_WORD_7[ 8 ] = { 0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0007, 0x0007 };
alignas( 16 ) constexpr u16 SIMD_WORD_255[ 8 ] = { 255, 255, 255, 255, 255, 255, 255, 255 };

alignas( 16 ) constexpr u16 SIMD_WORD_scale_7_9_11_13[ 8 ] = { 7, 7, 9, 9, 11, 11, 13, 13 };
alignas( 16 ) constexpr u16 SIMD_WORD_scale_7_5_3_1[ 8 ] = { 7, 7, 5, 5, 3, 3, 1, 1 };
alignas( 16 ) constexpr u32 SIMD_DWORD_byteMask[ 4 ] = { 0x000000FF, 0x000000FF, 0x000000FF, 0x000000FF };
// TODO: try SoA block layout ?
// TODO: BC3 / DXTC5 
// TODO: YCoCg 
// TODO: better loads and stores
inline u16 Rgb888ToRgb565( i32 col )
{
	u16 r = col & 0xff;
	u16 g = ( col >> 8 ) & 0xff;
	u16 b = ( col >> 16 ) & 0xff;

	return ( ( r >> 3 ) << 11 ) | ( ( g >> 2 ) << 5 ) | ( b >> 3 );
}

inline void Exctract4x4Block_SIMD( const u8* src, int width, u8* colorBlock )
{
	__m128i b0 = _mm_lddqu_si128( ( const __m128i* )( src + width * 4 * 0 ) );
	__m128i b1 = _mm_lddqu_si128( ( const __m128i* )( src + width * 4 * 1 ) );
	__m128i b2 = _mm_lddqu_si128( ( const __m128i* )( src + width * 4 * 2 ) );
	__m128i b3 = _mm_lddqu_si128( ( const __m128i* )( src + width * 4 * 3 ) );

	_mm_store_si128( ( __m128i* )( &colorBlock[ 0 ] ), b0 );
	_mm_store_si128( ( __m128i* )( &colorBlock[ 16 ] ), b1 );
	_mm_store_si128( ( __m128i* )( &colorBlock[ 32 ] ), b2 );
	_mm_store_si128( ( __m128i* )( &colorBlock[ 48 ] ), b3 );
}
inline std::pair<i32,i32> ComputeBlockMinMax_SIMD( const u8* colorBlock )
{
	__m128i b0 = _mm_load_si128( ( const __m128i* )( &colorBlock[ 0 ] ) );
	__m128i b1 = _mm_load_si128( ( const __m128i* )( &colorBlock[ 16 ] ) );
	__m128i b2 = _mm_load_si128( ( const __m128i* )( &colorBlock[ 32 ] ) );
	__m128i b3 = _mm_load_si128( ( const __m128i* )( &colorBlock[ 48 ] ) );

	__m128i max1 = _mm_max_epu8( b0, b1 );
	__m128i min1 = _mm_min_epu8( b0, b1 );
	__m128i max2 = _mm_max_epu8( b2, b3 );
	__m128i min2 = _mm_min_epu8( b2, b3 );

	__m128i max3 = _mm_max_epu8( max1, max2 );
	__m128i min3 = _mm_min_epu8( min1, min2 );

	__m128i max4 = _mm_shuffle_epi32( max3, R_SHUFFLE_D( 2, 3, 2, 3 ) );
	__m128i min4 = _mm_shuffle_epi32( min3, R_SHUFFLE_D( 2, 3, 2, 3 ) );

	__m128i max5 = _mm_max_epu8( max3, max4 );
	__m128i min5 = _mm_min_epu8( min3, min4 );

	__m128i max6 = _mm_shufflelo_epi16( max5, R_SHUFFLE_D( 2, 3, 2, 3 ) );
	__m128i min6 = _mm_shufflelo_epi16( min5, R_SHUFFLE_D( 2, 3, 2, 3 ) );

	max6 = _mm_max_epu8( max5, max6 );
	min6 = _mm_min_epu8( min5, min6 );

	u32 maxColor = _mm_cvtsi128_si32( max6 );
	u32 minColor = _mm_cvtsi128_si32( min6 );

	return { maxColor, minColor };
}

inline std::pair<i32,i32> InsertColorMinMaxBc1_SIMD( i32 maxCol, i32 minCol )
{
	__m128i min = _mm_cvtsi32_si128( minCol );
	__m128i max = _mm_cvtsi32_si128( maxCol );

	__m128i xmm0 = _mm_unpacklo_epi8( min, {} );
	__m128i xmm1 = _mm_unpacklo_epi8( max, {} );

	__m128i xmm2 = _mm_sub_epi16( xmm1, xmm0 );

	xmm2 = _mm_mulhi_epi16( xmm2, *( const __m128i* )SIMD_WORD_insetShift );

	xmm0 = _mm_add_epi16( xmm0, xmm2 );
	xmm1 = _mm_sub_epi16( xmm1, xmm2 );

	xmm0 = _mm_packus_epi16( xmm0, xmm0 );
	xmm1 = _mm_packus_epi16( xmm1, xmm1 );

	u32 minColor = _mm_cvtsi128_si32( xmm0 );
	u32 maxColor = _mm_cvtsi128_si32( xmm1 );

	return { maxColor, minColor };
}
inline std::pair<i32, i32> InsetNormalMinMaxBc5_SIMD( i32 maxNormal, i32 minNormal )
{
	__m128i temp0, temp1, temp2;

	temp0 = _mm_cvtsi32_si128( minNormal );
	temp1 = _mm_cvtsi32_si128( maxNormal );

	temp0 = _mm_unpacklo_epi8( temp0, {} );
	temp1 = _mm_unpacklo_epi8( temp1, {} );

	temp2 = _mm_sub_epi16( temp1, temp0 );
	temp2 = _mm_sub_epi16( temp2, *( const __m128i* )SIMD_WORD_insetNormalBc5Round );
	temp2 = _mm_and_si128( temp2, *( const __m128i* )SIMD_WORD_insetNormalBc5Mask );		// xmm2 = inset (0 & 1)

	temp0 = _mm_mullo_epi16( temp0, *( const __m128i* )SIMD_WORD_insetNormalBc5ShiftUp );
	temp1 = _mm_mullo_epi16( temp1, *( const __m128i* )SIMD_WORD_insetNormalBc5ShiftUp );
	temp0 = _mm_add_epi16( temp0, temp2 );
	temp1 = _mm_sub_epi16( temp1, temp2 );
	temp0 = _mm_mulhi_epi16( temp0, *( const __m128i* )SIMD_WORD_insetNormalBc5ShiftDown );
	temp1 = _mm_mulhi_epi16( temp1, *( const __m128i* )SIMD_WORD_insetNormalBc5ShiftDown );

	// mini and maxi must be >= 0 and <= 255
	temp0 = _mm_max_epi16( temp0, {} );
	temp1 = _mm_max_epi16( temp1, {} );
	temp0 = _mm_min_epi16( temp0, *( const __m128i* )SIMD_WORD_255 );
	temp1 = _mm_min_epi16( temp1, *( const __m128i* )SIMD_WORD_255 );

	temp0 = _mm_packus_epi16( temp0, temp0 );
	temp1 = _mm_packus_epi16( temp1, temp1 );

	i32 minN = _mm_cvtsi128_si32( temp0 );
	i32 maxN = _mm_cvtsi128_si32( temp1 );

	return { maxN,minN };
}
inline std::pair<i32, i32> InsetMetalRoughMinMaxBc5_SIMD( i32 blockMax, i32 blockMin )
{
	__m128i temp0, temp1, temp2;

	temp0 = _mm_cvtsi32_si128( blockMin );
	temp1 = _mm_cvtsi32_si128( blockMax );

	temp0 = _mm_unpacklo_epi8( temp0, {} );
	temp1 = _mm_unpacklo_epi8( temp1, {} );

	temp2 = _mm_sub_epi16( temp1, temp0 );
	temp2 = _mm_sub_epi16( temp2, *( const __m128i* )SIMD_WORD_insetMetalRoughBc5Round );
	temp2 = _mm_and_si128( temp2, *( const __m128i* )SIMD_WORD_insetMetalRoughBc5Mask );		// xmm2 = inset (0 & 1)

	temp0 = _mm_mullo_epi16( temp0, *( const __m128i* )SIMD_WORD_insetMetalRoughBc5ShiftUp );
	temp1 = _mm_mullo_epi16( temp1, *( const __m128i* )SIMD_WORD_insetMetalRoughBc5ShiftUp );
	temp0 = _mm_add_epi16( temp0, temp2 );
	temp1 = _mm_sub_epi16( temp1, temp2 );
	temp0 = _mm_mulhi_epi16( temp0, *( const __m128i* )SIMD_WORD_insetMetalRoughBc5ShiftDown );
	temp1 = _mm_mulhi_epi16( temp1, *( const __m128i* )SIMD_WORD_insetMetalRoughBc5ShiftDown );

	// mini and maxi must be >= 0 and <= 255
	temp0 = _mm_max_epi16( temp0, {} );
	temp1 = _mm_max_epi16( temp1, {} );
	temp0 = _mm_min_epi16( temp0, *( const __m128i* )SIMD_WORD_255 );
	temp1 = _mm_min_epi16( temp1, *( const __m128i* )SIMD_WORD_255 );

	temp0 = _mm_packus_epi16( temp0, temp0 );
	temp1 = _mm_packus_epi16( temp1, temp1 );

	i32 minB = _mm_cvtsi128_si32( temp0 );
	i32 maxB = _mm_cvtsi128_si32( temp1 );

	return { maxB,minB };
}

inline u32 CopmuteColorIndicesBc1_SIMD( const u8* colorBlock, i32 maxCol, i32 minCol )
{
	__m128i result = {};
	__m128i color0, color1, color2, color3;
	__m128i temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;
	__m128i minColor = _mm_cvtsi32_si128( minCol );
	__m128i maxColor = _mm_cvtsi32_si128( maxCol );
	__m128i blockA[ 2 ], blockB[ 2 ];
	blockA[ 0 ] = _mm_load_si128( ( const __m128i* )( &colorBlock[ 0 ] ) );
	blockB[ 0 ] = _mm_load_si128( ( const __m128i* )( &colorBlock[ 16 ] ) );
	blockA[ 1 ] = _mm_load_si128( ( const __m128i* )( &colorBlock[ 32 ] ) );
	blockB[ 1 ] = _mm_load_si128( ( const __m128i* )( &colorBlock[ 48 ] ) );


	temp0 = _mm_and_si128( maxColor, *( const __m128i* )SIMD_BYTE_colorMask );
	temp0 = _mm_unpacklo_epi8( temp0, {} );
	temp4 = _mm_shufflelo_epi16( temp0, R_SHUFFLE_D( 0, 3, 2, 3 ) );
	temp5 = _mm_shufflelo_epi16( temp0, R_SHUFFLE_D( 3, 1, 3, 3 ) );
	temp4 = _mm_srli_epi16( temp4, 5 );
	temp5 = _mm_srli_epi16( temp5, 6 );
	temp0 = _mm_or_si128( temp0, temp4 );
	temp0 = _mm_or_si128( temp0, temp5 );


	temp1 = _mm_and_si128( minColor, *( const __m128i* )SIMD_BYTE_colorMask );
	temp1 = _mm_unpacklo_epi8( temp1, {} );
	temp4 = _mm_shufflelo_epi16( temp1, R_SHUFFLE_D( 0, 3, 2, 3 ) );
	temp5 = _mm_shufflelo_epi16( temp1, R_SHUFFLE_D( 3, 1, 3, 3 ) );
	temp4 = _mm_srli_epi16( temp4, 5 );
	temp5 = _mm_srli_epi16( temp5, 6 );
	temp1 = _mm_or_si128( temp1, temp4 );
	temp1 = _mm_or_si128( temp1, temp5 );


	temp2 = _mm_packus_epi16( temp0, {} );
	color0 = _mm_shuffle_epi32( temp2, R_SHUFFLE_D( 0, 1, 0, 1 ) );

	temp6 = _mm_add_epi16( temp0, temp0 );
	temp6 = _mm_add_epi16( temp6, temp1 );
	temp6 = _mm_mulhi_epi16( temp6, *( const __m128i* )SIMD_WORD_divBy3 );		// * ( ( 1 << 16 ) / 3 + 1 ) ) >> 16
	temp6 = _mm_packus_epi16( temp6, {} );
	color2 = _mm_shuffle_epi32( temp6, R_SHUFFLE_D( 0, 1, 0, 1 ) );

	temp3 = _mm_packus_epi16( temp1, {} );
	color1 = _mm_shuffle_epi32( temp3, R_SHUFFLE_D( 0, 1, 0, 1 ) );

	temp1 = _mm_add_epi16( temp1, temp1 );
	temp0 = _mm_add_epi16( temp0, temp1 );
	temp0 = _mm_mulhi_epi16( temp0, *( const __m128i* )SIMD_WORD_divBy3 );		// * ( ( 1 << 16 ) / 3 + 1 ) ) >> 16
	temp0 = _mm_packus_epi16( temp0, {} );
	color3 = _mm_shuffle_epi32( temp0, R_SHUFFLE_D( 0, 1, 0, 1 ) );

	for( int i = 1; i >= 0; i-- )
	{
		// Load block
		temp3 = _mm_shuffle_epi32( blockA[ i ], R_SHUFFLE_D( 0, 2, 1, 3 ) );
		temp5 = _mm_shuffle_ps( blockA[ i ], _mm_setzero_ps(), R_SHUFFLE_D( 2, 3, 0, 1 ) );
		temp5 = _mm_shuffle_epi32( temp5, R_SHUFFLE_D( 0, 2, 1, 3 ) );

		temp0 = _mm_sad_epu8( temp3, color0 );
		temp6 = _mm_sad_epu8( temp5, color0 );
		temp0 = _mm_packs_epi32( temp0, temp6 );

		temp1 = _mm_sad_epu8( temp3, color1 );
		temp6 = _mm_sad_epu8( temp5, color1 );
		temp1 = _mm_packs_epi32( temp1, temp6 );

		temp2 = _mm_sad_epu8( temp3, color2 );
		temp6 = _mm_sad_epu8( temp5, color2 );
		temp2 = _mm_packs_epi32( temp2, temp6 );

		temp3 = _mm_sad_epu8( temp3, color3 );
		temp5 = _mm_sad_epu8( temp5, color3 );
		temp3 = _mm_packs_epi32( temp3, temp5 );

		// Load block
		temp4 = _mm_shuffle_epi32( blockB[ i ], R_SHUFFLE_D( 0, 2, 1, 3 ) );
		temp5 = _mm_shuffle_ps( blockB[ i ], _mm_setzero_ps(), R_SHUFFLE_D( 2, 3, 0, 1 ) );
		temp5 = _mm_shuffle_epi32( temp5, R_SHUFFLE_D( 0, 2, 1, 3 ) );

		temp6 = _mm_sad_epu8( temp4, color0 );
		temp7 = _mm_sad_epu8( temp5, color0 );
		temp6 = _mm_packs_epi32( temp6, temp7 );
		temp0 = _mm_packs_epi32( temp0, temp6 );	// d0

		temp6 = _mm_sad_epu8( temp4, color1 );
		temp7 = _mm_sad_epu8( temp5, color1 );
		temp6 = _mm_packs_epi32( temp6, temp7 );
		temp1 = _mm_packs_epi32( temp1, temp6 );	// d1

		temp6 = _mm_sad_epu8( temp4, color2 );
		temp7 = _mm_sad_epu8( temp5, color2 );
		temp6 = _mm_packs_epi32( temp6, temp7 );
		temp2 = _mm_packs_epi32( temp2, temp6 );	// d2

		temp4 = _mm_sad_epu8( temp4, color3 );
		temp5 = _mm_sad_epu8( temp5, color3 );
		temp4 = _mm_packs_epi32( temp4, temp5 );
		temp3 = _mm_packs_epi32( temp3, temp4 );	// d3

		temp7 = _mm_slli_epi32( result, 16 );

		temp4 = _mm_cmpgt_epi16( temp0, temp2 );	// b2
		temp5 = _mm_cmpgt_epi16( temp1, temp3 );	// b3
		temp0 = _mm_cmpgt_epi16( temp0, temp3 );	// b0
		temp1 = _mm_cmpgt_epi16( temp1, temp2 );	// b1
		temp2 = _mm_cmpgt_epi16( temp2, temp3 );	// b4

		temp4 = _mm_and_si128( temp4, temp1 );		// x0
		temp5 = _mm_and_si128( temp5, temp0 );		// x1
		temp2 = _mm_and_si128( temp2, temp0 );		// x2
		temp4 = _mm_or_si128( temp4, temp5 );
		temp2 = _mm_and_si128( temp2, *( const __m128i* )SIMD_WORD_1 );
		temp4 = _mm_and_si128( temp4, *( const __m128i* )SIMD_WORD_2 );
		temp2 = _mm_or_si128( temp2, temp4 );

		temp5 = _mm_shuffle_epi32( temp2, R_SHUFFLE_D( 2, 3, 0, 1 ) );
		temp2 = _mm_unpacklo_epi16( temp2, {} );
		temp5 = _mm_unpacklo_epi16( temp5, {} );
		temp5 = _mm_slli_epi32( temp5, 8 );
		temp7 = _mm_or_si128( temp7, temp5 );
		result = _mm_or_si128( temp7, temp2 );
	}

	temp4 = _mm_shuffle_epi32( result, R_SHUFFLE_D( 1, 2, 3, 0 ) );
	temp5 = _mm_shuffle_epi32( result, R_SHUFFLE_D( 2, 3, 0, 1 ) );
	temp6 = _mm_shuffle_epi32( result, R_SHUFFLE_D( 3, 0, 1, 2 ) );
	temp4 = _mm_slli_epi32( temp4, 2 );
	temp5 = _mm_slli_epi32( temp5, 4 );
	temp6 = _mm_slli_epi32( temp6, 6 );
	temp7 = _mm_or_si128( result, temp4 );
	temp7 = _mm_or_si128( temp7, temp5 );
	temp7 = _mm_or_si128( temp7, temp6 );

	return _mm_cvtsi128_si32( temp7 );
}
inline u64 ComputeAlphaIndices_SIMD( const u8* block, u32 channelBitOffset, i32 maxAlpha, i32 minAlpha )
{
	__m128i block0 = _mm_load_si128( ( const __m128i* )( &block[ 0 ] ) );
	__m128i block1 = _mm_load_si128( ( const __m128i* )( &block[ 16 ] ) );
	__m128i block2 = _mm_load_si128( ( const __m128i* )( &block[ 32 ] ) );
	__m128i block3 = _mm_load_si128( ( const __m128i* )( &block[ 48 ] ) );
	__m128i temp0, temp1, temp2, temp3, temp4, temp5, temp6, temp7;

	temp7 = _mm_cvtsi32_si128( channelBitOffset );

	temp0 = _mm_srl_epi32( block0, temp7 );
	temp5 = _mm_srl_epi32( block1, temp7 );
	temp6 = _mm_srl_epi32( block2, temp7 );
	temp4 = _mm_srl_epi32( block3, temp7 );

	temp0 = _mm_and_si128( temp0, *( const __m128i* )SIMD_DWORD_byteMask );
	temp5 = _mm_and_si128( temp5, *( const __m128i* )SIMD_DWORD_byteMask );
	temp6 = _mm_and_si128( temp6, *( const __m128i* )SIMD_DWORD_byteMask );
	temp4 = _mm_and_si128( temp4, *( const __m128i* )SIMD_DWORD_byteMask );

	temp0 = _mm_packus_epi16( temp0, temp5 );
	temp6 = _mm_packus_epi16( temp6, temp4 );

	//---------------------

	// ab0 = (  7 * maxAlpha +  7 * minAlpha + ALPHA_RANGE ) / 14
	// ab3 = (  9 * maxAlpha +  5 * minAlpha + ALPHA_RANGE ) / 14
	// ab2 = ( 11 * maxAlpha +  3 * minAlpha + ALPHA_RANGE ) / 14
	// ab1 = ( 13 * maxAlpha +  1 * minAlpha + ALPHA_RANGE ) / 14

	// ab4 = (  7 * maxAlpha +  7 * minAlpha + ALPHA_RANGE ) / 14
	// ab5 = (  5 * maxAlpha +  9 * minAlpha + ALPHA_RANGE ) / 14
	// ab6 = (  3 * maxAlpha + 11 * minAlpha + ALPHA_RANGE ) / 14
	// ab7 = (  1 * maxAlpha + 13 * minAlpha + ALPHA_RANGE ) / 14

	temp5 = _mm_cvtsi32_si128( maxAlpha );
	temp5 = _mm_shufflelo_epi16( temp5, R_SHUFFLE_D( 0, 0, 0, 0 ) );
	temp5 = _mm_shuffle_epi32( temp5, R_SHUFFLE_D( 0, 0, 0, 0 ) );

	temp2 = _mm_cvtsi32_si128( minAlpha );
	temp2 = _mm_shufflelo_epi16( temp2, R_SHUFFLE_D( 0, 0, 0, 0 ) );
	temp2 = _mm_shuffle_epi32( temp2, R_SHUFFLE_D( 0, 0, 0, 0 ) );

	temp7 = _mm_mullo_epi16( temp5, *( const __m128i* )SIMD_WORD_scale_7_5_3_1 );
	temp5 = _mm_mullo_epi16( temp5, *( const __m128i* )SIMD_WORD_scale_7_9_11_13 );
	temp3 = _mm_mullo_epi16( temp2, *( const __m128i* )SIMD_WORD_scale_7_9_11_13 );
	temp2 = _mm_mullo_epi16( temp2, *( const __m128i* )SIMD_WORD_scale_7_5_3_1 );

	temp5 = _mm_add_epi16( temp5, temp2 );
	temp7 = _mm_add_epi16( temp7, temp3 );

	temp5 = _mm_add_epi16( temp5, *( const __m128i* )SIMD_WORD_7 );
	temp7 = _mm_add_epi16( temp7, *( const __m128i* )SIMD_WORD_7 );

	temp5 = _mm_mulhi_epi16( temp5, *( const __m128i* )SIMD_WORD_divBy14 );
	temp7 = _mm_mulhi_epi16( temp7, *( const __m128i* )SIMD_WORD_divBy14 );

	temp1 = _mm_shuffle_epi32( temp5, R_SHUFFLE_D( 3, 3, 3, 3 ) );
	temp2 = _mm_shuffle_epi32( temp5, R_SHUFFLE_D( 2, 2, 2, 2 ) );
	temp3 = _mm_shuffle_epi32( temp5, R_SHUFFLE_D( 1, 1, 1, 1 ) );
	temp1 = _mm_packus_epi16( temp1, temp1 );
	temp2 = _mm_packus_epi16( temp2, temp2 );
	temp3 = _mm_packus_epi16( temp3, temp3 );

	temp0 = _mm_packus_epi16( temp0, temp6 );

	temp4 = _mm_shuffle_epi32( temp7, R_SHUFFLE_D( 0, 0, 0, 0 ) );
	temp5 = _mm_shuffle_epi32( temp7, R_SHUFFLE_D( 1, 1, 1, 1 ) );
	temp6 = _mm_shuffle_epi32( temp7, R_SHUFFLE_D( 2, 2, 2, 2 ) );
	temp7 = _mm_shuffle_epi32( temp7, R_SHUFFLE_D( 3, 3, 3, 3 ) );
	temp4 = _mm_packus_epi16( temp4, temp4 );
	temp5 = _mm_packus_epi16( temp5, temp5 );
	temp6 = _mm_packus_epi16( temp6, temp6 );
	temp7 = _mm_packus_epi16( temp7, temp7 );

	temp1 = _mm_max_epu8( temp1, temp0 );
	temp2 = _mm_max_epu8( temp2, temp0 );
	temp3 = _mm_max_epu8( temp3, temp0 );
	temp1 = _mm_cmpeq_epi8( temp1, temp0 );
	temp2 = _mm_cmpeq_epi8( temp2, temp0 );
	temp3 = _mm_cmpeq_epi8( temp3, temp0 );
	temp4 = _mm_max_epu8( temp4, temp0 );
	temp5 = _mm_max_epu8( temp5, temp0 );
	temp6 = _mm_max_epu8( temp6, temp0 );
	temp7 = _mm_max_epu8( temp7, temp0 );
	temp4 = _mm_cmpeq_epi8( temp4, temp0 );
	temp5 = _mm_cmpeq_epi8( temp5, temp0 );
	temp6 = _mm_cmpeq_epi8( temp6, temp0 );
	temp7 = _mm_cmpeq_epi8( temp7, temp0 );
	temp0 = _mm_adds_epi8( *( const __m128i* )SIMD_BYTE_8, temp1 );
	temp2 = _mm_adds_epi8( temp2, temp3 );
	temp4 = _mm_adds_epi8( temp4, temp5 );
	temp6 = _mm_adds_epi8( temp6, temp7 );
	temp0 = _mm_adds_epi8( temp0, temp2 );
	temp4 = _mm_adds_epi8( temp4, temp6 );
	temp0 = _mm_adds_epi8( temp0, temp4 );
	temp0 = _mm_and_si128( temp0, *( const __m128i* )SIMD_BYTE_7 );
	temp1 = _mm_cmpgt_epi8( *( const __m128i* )SIMD_BYTE_2, temp0 );
	temp1 = _mm_and_si128( temp1, *( const __m128i* )SIMD_BYTE_1 );
	temp0 = _mm_xor_si128( temp0, temp1 );

	temp1 = _mm_srli_epi64( temp0, 8 - 3 );
	temp2 = _mm_srli_epi64( temp0, 16 - 6 );
	temp3 = _mm_srli_epi64( temp0, 24 - 9 );
	temp4 = _mm_srli_epi64( temp0, 32 - 12 );
	temp5 = _mm_srli_epi64( temp0, 40 - 15 );
	temp6 = _mm_srli_epi64( temp0, 48 - 18 );
	temp7 = _mm_srli_epi64( temp0, 56 - 21 );
	temp0 = _mm_and_si128( temp0, *( const __m128i* )SIMD_DWORD_alphaBitMask0 );
	temp1 = _mm_and_si128( temp1, *( const __m128i* )SIMD_DWORD_alphaBitMask1 );
	temp2 = _mm_and_si128( temp2, *( const __m128i* )SIMD_DWORD_alphaBitMask2 );
	temp3 = _mm_and_si128( temp3, *( const __m128i* )SIMD_DWORD_alphaBitMask3 );
	temp4 = _mm_and_si128( temp4, *( const __m128i* )SIMD_DWORD_alphaBitMask4 );
	temp5 = _mm_and_si128( temp5, *( const __m128i* )SIMD_DWORD_alphaBitMask5 );
	temp6 = _mm_and_si128( temp6, *( const __m128i* )SIMD_DWORD_alphaBitMask6 );
	temp7 = _mm_and_si128( temp7, *( const __m128i* )SIMD_DWORD_alphaBitMask7 );
	temp0 = _mm_or_si128( temp0, temp1 );
	temp2 = _mm_or_si128( temp2, temp3 );
	temp4 = _mm_or_si128( temp4, temp5 );
	temp6 = _mm_or_si128( temp6, temp7 );
	temp0 = _mm_or_si128( temp0, temp2 );
	temp4 = _mm_or_si128( temp4, temp6 );
	temp0 = _mm_or_si128( temp0, temp4 );

	temp1 = _mm_shuffle_epi32( temp0, R_SHUFFLE_D( 2, 3, 0, 1 ) );

	u64 first3IdxBytes = _mm_cvtsi128_si32( temp0 );
	u64 second3IdxBytes = _mm_cvtsi128_si32( temp1 );

	return ( second3IdxBytes << 24 ) | first3IdxBytes;
}

void CompressToBc1_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex )
{
	assert( width >= 4 && ( width & 3 ) == 0 );
	assert( height >= 4 && ( height & 3 ) == 0 );

	alignas( 16 ) u8 block[ 64 ] = {};
	//alignas( 16 ) u8 minColor[ 4 ] = {};
	//alignas( 16 ) u8 maxColor[ 4 ] = {};

	//u64 srcPadding = 0;
	//u64 dstPadding = 0;

	for( u64 j = 0; j < height; j += 4, texSrc += width * 4 * 4 )
	{
		for( u64 i = 0; i < width; i += 4 )
		{
			Exctract4x4Block_SIMD( texSrc + i * 4, width, block );
			alignas( 16 ) i32 minCol = 0, maxCol = 0;
			// TODO: better way of returning 2 vals ?
			std::tie( maxCol, minCol ) = ComputeBlockMinMax_SIMD( block );
			std::tie( maxCol, minCol ) = InsertColorMinMaxBc1_SIMD( maxCol, minCol );
			u16 minRgb565 = Rgb888ToRgb565( minCol );
			u16 maxRgb565 = Rgb888ToRgb565( maxCol );
			u32 packedIndices = CopmuteColorIndicesBc1_SIMD( block, maxCol, minCol );

			*(u16*) outCmpTex = maxRgb565;
			outCmpTex += 2;
			*(u16*) outCmpTex = minRgb565;
			outCmpTex += 2;
			*(u32*) outCmpTex = packedIndices;
			outCmpTex += 4;
		}
		//outCmpTex += dstPadding;
		//texSrc += srcPadding;
	}
}
void CompressNormalMapToBc5_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex )
{
	assert( width >= 4 && ( width & 3 ) == 0 );
	assert( height >= 4 && ( height & 3 ) == 0 );

	alignas( 16 ) u8 block[ 64 ] = {};
	
	for( u64 j = 0; j < height; j += 4, texSrc += width * 4 * 4 )
	{
		for( u64 i = 0; i < width; i += 4 )
		{
			Exctract4x4Block_SIMD( texSrc + i * 4, width, block );
			alignas( 16 ) i32 minNormal = 0, maxNormal = 0;
			// TODO: better way of returning 2 vals ?
			std::tie( maxNormal, minNormal ) = ComputeBlockMinMax_SIMD( block );
			std::tie( maxNormal, minNormal ) = InsetNormalMinMaxBc5_SIMD( maxNormal, minNormal );

			u8 normalXMax = maxNormal & 0xff;
			u8 normalXMin = minNormal & 0xff;
			u64 packedXIndices = ComputeAlphaIndices_SIMD( block, 0 * 8, normalXMax, normalXMin );

			u8 normalYMax = ( maxNormal >> 8 ) & 0xff;
			u8 normalYMin = ( minNormal >> 8 ) & 0xff;
			u64 packedYIndices = ComputeAlphaIndices_SIMD( block, 1 * 8, normalYMax, normalYMin );

			*(u8*) outCmpTex = normalXMax;
			++outCmpTex;
			*(u8*) outCmpTex = normalXMin;
			++outCmpTex;
			*(u64*) outCmpTex = packedXIndices;
			outCmpTex += 6;
			*(u8*) outCmpTex = normalYMax;
			++outCmpTex;
			*(u8*) outCmpTex = normalYMin;
			++outCmpTex;
			*(u64*) outCmpTex = packedYIndices;
			outCmpTex += 6;
		}
	}
}
void CompressMetalRoughMapToBc5_SIMD( const u8* texSrc, u64 width, u64 height, u8* outCmpTex )
{
	assert( width >= 4 && ( width & 3 ) == 0 );
	assert( height >= 4 && ( height & 3 ) == 0 );

	alignas( 16 ) u8 block[ 64 ] = {};

	for( u64 j = 0; j < height; j += 4, texSrc += width * 4 * 4 )
	{
		for( u64 i = 0; i < width; i += 4 )
		{
			Exctract4x4Block_SIMD( texSrc + i * 4, width, block );
			alignas( 16 ) i32 blockMin = 0, blockMax = 0;
			// TODO: better way of returning 2 vals ?
			std::tie( blockMax, blockMin ) = ComputeBlockMinMax_SIMD( block );
			std::tie( blockMax, blockMin ) = InsetMetalRoughMinMaxBc5_SIMD( blockMax, blockMin );

			u8 redChMax = blockMax >> 8;
			u8 redChMin = blockMin >> 8;
			u64 redChIndices = ComputeAlphaIndices_SIMD( block, 1 * 8, redChMax, redChMin );

			u8 greenChMax = blockMax >> 16;
			u8 greenChMin = blockMin >> 16;
			u64 greenChIndices = ComputeAlphaIndices_SIMD( block, 2 * 8, greenChMax, greenChMin );

			*(u8*) outCmpTex = redChMax;
			++outCmpTex;
			*(u8*) outCmpTex = redChMin;
			++outCmpTex;
			*(u64*) outCmpTex = redChIndices;
			outCmpTex += 6;
			*(u8*) outCmpTex = greenChMax;
			++outCmpTex;
			*(u8*) outCmpTex = greenChMin;
			++outCmpTex;
			*(u64*) outCmpTex = greenChIndices;
			outCmpTex += 6;
		}
	}
}