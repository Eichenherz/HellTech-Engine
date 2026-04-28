#ifndef __HT_UTILS_H__
#define __HT_UTILS_H__

#include <new>

#include "ht_core_types.h"
#include "ht_error.h"

constexpr u64 GB = 1ull << 30;
constexpr u64 MB = 1ull << 20;
constexpr u64 KB = 1ull << 10;

template<typename T>
constexpr bool IsStructZero( const T& inStruct )
{
    constexpr u8 ZERO_STRUCT_MEM[ sizeof( T ) ] = {};
    i32 memCmpRes = std::memcmp( &inStruct, ZERO_STRUCT_MEM, sizeof( T ) );
    return 0 == memCmpRes;
}

template<typename T>
constexpr void ZeroStruct( T& inStruct )
{
    std::memset( &inStruct, 0, sizeof( inStruct ) );
}

inline bool IsPowOf2( u64 addr )
{
    return !( addr & ( addr - 1 ) );
}
inline u64 FwdAlign( u64 addr, u64 alignment )
{
    HT_ASSERT( IsPowOf2( alignment ) );
    return ( addr + ( alignment - 1 ) ) & ~( alignment - 1 );
}

constexpr u64 CACHE_LINE_SZ = std::hardware_destructive_interference_size;

#define CACHE_ALIGN alignas( CACHE_LINE_SZ )

#endif // !__HT_UTILS_H__
