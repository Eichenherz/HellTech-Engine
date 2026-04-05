#pragma once

#ifndef __HT_CORE_TYPES_H__
#define __HT_CORE_TYPES_H__

#include <stdint.h>
#include <concepts>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

#define INVALID_IDX -1

template<typename T>
concept UINT_T = std::integral<T> && std::is_unsigned_v<T>;

template<UINT_T T>
inline bool IsIndexValid( T idx ) 
{ 
    constexpr T INVALID = T( INVALID_IDX );
    return INVALID != idx;
}

template<typename T>
concept TRIVIAL_T = std::is_trivially_destructible_v<T> && std::is_trivially_copyable_v<T>;

#endif // !__HT_CORE_TYPES_H__
