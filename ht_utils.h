#ifndef __HT_UTILS_H__
#define __HT_UTILS_H__

#include "core_types.h"
#include "assert.h"

#define GB (u64)( 1 << 30 )
#define MB (u64)( 1 << 20 )
#define KB (u64)( 1 << 10 )

inline bool IsPowOf2( u64 addr )
{
	return !( addr & ( addr - 1 ) );
}
inline u64 FwdAlign( u64 addr, u64 alignment )
{
	assert( IsPowOf2( alignment ) );
	u64 mod = addr & ( alignment - 1 );
	return mod ? addr + ( alignment - mod ) : addr;
}
// TODO: math_uitl file
inline u64 FloorPowOf2( u64 size )
{
	// NOTE: use Hacker's Delight for bit-tickery
	constexpr u64 ONE_LEFT_MOST = u64( 1ULL << ( sizeof( u64 ) * 8 - 1 ) );
	return ( size ) ? ONE_LEFT_MOST >> __lzcnt64( size ) : 0;
}
inline u64 GetImgMipCountForPow2( u64 width, u64 height, u64 mipLevels )
{
	// NOTE: log2 == position of the highest bit set (or most significant bit set, MSB)
	// NOTE: https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogObvious
	constexpr u64 TYPE_BIT_COUNT = sizeof( u64 ) * 8 - 1;

	u64 maxDim = std::max( width, height );
	assert( IsPowOf2( maxDim ) );
	u64 log2MaxDim = TYPE_BIT_COUNT - __lzcnt64( maxDim );

	return std::min( log2MaxDim, mipLevels );
}
inline u64 GetImgMipCount( u64 width, u64 height, u64 mipLevels )
{
	assert( width && height );
	u64 maxDim = std::max( width, height );

	return std::min( (u64) floor( log2( maxDim ) ), mipLevels );
}
#endif // !__HT_UTILS_H__
