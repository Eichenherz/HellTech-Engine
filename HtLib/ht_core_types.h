#pragma once

#ifndef HT_CORE_TYPES_H
#define HT_CORE_TYPES_H

#ifdef __cplusplus

#include <stdint.h>
#include <concepts>
#include <compare>

using u8    = uint8_t;
using u16   = uint16_t;
using u32   = uint32_t;
using u64   = uint64_t;

using i8    = int8_t;
using i16   = int16_t;
using i32   = int32_t;
using i64   = int64_t;

#ifdef __clang__ // NOTE: bc it gives us logical, bitwise and arithmetic operators for "free"
using u128  = unsigned __int128;
using i128  = __int128;
#else
struct alignas( 16 ) u128
{
    u64 lo;
    u64 hi;

    bool operator==( const u128& ) const = default;
};
#endif

template<typename T>
struct alignas( 16 ) tagged_ptr
{
    T*  ptr = nullptr;
    u64 tag = 0;
};

template<typename T>
concept INTEGER_T = std::integral<T>;

template<typename T>
concept UINT_T = std::unsigned_integral<T>;

template<INTEGER_T T>
bool IsIndexValid( T idx ) { return ~T( 0 ) != idx; }

template<typename T>
concept TRIVIAL_T = std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>;

template<typename T>
consteval T BitCount() { return sizeof( T ) * 8; }

#else

typedef uint16_t    u16;
typedef uint        u32;
typedef uint64_t    u64;

typedef int16_t     i16;
typedef int         i32;
typedef int64_t     i64;

#endif

#include <ht_macros.h>

template<typename T, u64 IDX_BIT_WIDTH, u64 GEN_BIT_WIDTH>
struct generic_hndl32_t
{
    CONSTEXPR u64 MAX_IDX = 1ull << IDX_BIT_WIDTH;
    CONSTEXPR u64 MAX_GEN = ( 1ull << GEN_BIT_WIDTH ) - 1;

    u32 idx     : IDX_BIT_WIDTH;
    u32 gen     : GEN_BIT_WIDTH;
    u32 inUse   : 1;

#ifdef __cplusplus
    auto operator<=>( const generic_hndl32_t& ) const = default;
#endif

};

#define RHNDL32( T ) generic_hndl32_t<T, 20, 11>

STATIC_ASSERT( sizeof( u32 ) == sizeof( RHNDL32( u32 ) ), "" );

#endif // !HT_CORE_TYPES_H
