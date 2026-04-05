#ifndef __HT_UTILS_H__
#define __HT_UTILS_H__

#include <bit>
#include <span>
#include <algorithm>

#include "ht_core_types.h"
#include "ht_error.h"

constexpr u64 GB = 1ull << 30;
constexpr u64 MB = 1ull << 20;
constexpr u64 KB = 1ull << 10;

#define BYTE_COUNT( buffer ) std::size( buffer ) * sizeof( buffer[ 0 ] )

inline bool IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
inline u64 FwdAlign( u64 addr, u64 alignment )
{
	HT_ASSERT( IsPowOf2( alignment ) );
	return ( addr + ( alignment - 1 ) ) & ~( alignment - 1 );
}
// TODO: math_uitl file
inline u64 FloorPowOf2( u64 size )
{
	// NOTE: use Hacker's Delight for bit-tickery
	constexpr u64 ONE_LEFT_MOST = u64( 1ULL << ( sizeof( u64 ) * 8 - 1 ) );
	return ( size ) ? ONE_LEFT_MOST >> __lzcnt64( size ) : 0;
}
inline u32 GetImgMipCount( u32 width, u32 height, u32 mipLevels )
{
	HT_ASSERT( width && height );
	// NOTE: floor( log2 () ) == bit_width -1 
	return std::min( ( u32 ) std::bit_width( std::max( width, height ) ) - 1, mipLevels );
}
#endif // !__HT_UTILS_H__
