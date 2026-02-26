#ifndef __CORE_TYPES_H__
#define __CORE_TYPES_H__

#include <stdint.h>

#include <array>
#include <format>
#include <type_traits>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

constexpr i32 INVALID_IDX = -1;

template<typename T>
concept UINT_T = std::integral<T> && std::is_unsigned_v<T>;

template<UINT_T T>
inline bool IsIndexValid( T idx ) 
{ 
    constexpr T INVALID_IDX = ~T{ 0 };
    return INVALID_IDX != idx; 
}

template<typename T>
concept Number32BitsMax = ( sizeof( T ) <= 4 );

template<u64 N>
struct fixed_string
{
    std::array<char, N> str;

    fixed_string() = default;

    template<typename... Args>
    fixed_string( std::format_string<Args...> fmt, Args&&... args )
    {
        str = {};
        std::format_to_n( str, std::size( str ) - 1, fmt, std::forward<Args>( args )... );
    }
};

// NOTE: always relative to the main file
using vfs_path = std::array<char, 128>;

template <>
struct std::hash<vfs_path>
{
    std::uint64_t operator()( const vfs_path& p ) const
    {
        // Hash as C-string up to first '\0' (or full buffer if no terminator).
        // FNV-1a 64-bit.
        constexpr std::uint64_t kOffset = 14695981039346656037ull;
        constexpr std::uint64_t kPrime = 1099511628211ull;

        std::uint64_t h = kOffset;

        for( char c : p )
        {
            if( c == '\0' )
                break;

            h ^= ( u8 ) c;
            h *= kPrime;
        }

        return h;
    }
};

#endif // !__CORE_TYPES_H__
