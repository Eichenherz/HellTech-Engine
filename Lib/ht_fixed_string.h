#pragma once 

#ifndef __HT_FIXED_STRING_H__
#define __HT_FIXED_STRING_H__

#include "core_types.h"
#include "ht_error.h"

#include <format>
#include <string_view>
#include <cstring>
#include <iterator>

template<u64 N>
struct fixed_string
{
    using value_type             = char;
    using size_type              = u64;
    using difference_type        = i64;
    using reference              = char&;
    using const_reference        = const char&;
    using pointer                = char*;
    using const_pointer          = const char*;
    using iterator               = char*;
    using const_iterator         = const char*;
    using reverse_iterator       = std::reverse_iterator<char*>;
    using const_reverse_iterator = std::reverse_iterator<const char*>;

    static constexpr u64 npos    = ~0ull;

    char chars[ N ]              = {};
    u64  charCount               = 0;

  
                            fixed_string() = default;

                            fixed_string( const char* s );

                            fixed_string( std::string_view sv );

                            template<typename... Args>
                            fixed_string( std::format_string<Args...> fmt, Args&&... args );

                            fixed_string( const fixed_string& )            = default;
                            fixed_string& operator=( const fixed_string& ) = default;
                            fixed_string( fixed_string&& )                 = default;
                            fixed_string& operator=( fixed_string&& )      = default;

    fixed_string&           operator=( std::string_view sv ) { return *this = fixed_string( sv ); }
    fixed_string&           operator=( const char* s )       { return *this = fixed_string( s ); }

    u64                     size()     const { return charCount; }
    constexpr u64           capacity() const { return N - 1; }

    void                    resize( u64 n, char ch = '\0' )
    {
        HT_ASSERT( n < N );
        if( n > charCount )
        {
            std::memset( chars + charCount, ch, n - charCount );
        }

        charCount = n;
        chars[ charCount ] = '\0';
    }

    reference               operator[]( u64 i ) { HT_ASSERT( i < N ); return chars[ i ]; }
    const_reference         operator[]( u64 i ) const { HT_ASSERT( i < N ); return chars[ i ]; }

    char*                   data()       { return chars; }
    const char*             data() const { return chars; }

    iterator                begin()        { return chars; }
    const_iterator          begin()  const { return chars; }
    const_iterator          cbegin() const { return chars; }

    iterator                end()          { return chars + charCount; }
    const_iterator          end()    const { return chars + charCount; }
    const_iterator          cend()   const { return chars + charCount; }

    reverse_iterator        rbegin()        { return reverse_iterator( end() ); }
    const_reverse_iterator  rbegin()  const { return const_reverse_iterator( end() ); }
    const_reverse_iterator  crbegin() const { return const_reverse_iterator( end() ); }

    reverse_iterator        rend()          { return reverse_iterator( begin() ); }
    const_reverse_iterator  rend()    const { return const_reverse_iterator( begin() ); }
    const_reverse_iterator  crend()   const { return const_reverse_iterator( begin() ); }

    operator std::string_view() const { return { chars, charCount }; }
};

template<u64 N>
fixed_string<N>::fixed_string( const char* s )
{
    u64 len = std::strlen( s );
    HT_ASSERT( len < N );
    charCount = len;
    std::memcpy( chars, s, charCount );
    chars[ charCount ] = '\0';
}

template<u64 N>
fixed_string<N>::fixed_string( std::string_view sv )
{
    HT_ASSERT( std::size( sv ) < N );
    charCount = std::size( sv );
    std::memcpy( chars, std::data( sv ), charCount );
    chars[ charCount ] = '\0';
}

template<u64 N>
template<typename... Args>
fixed_string<N>::fixed_string( std::format_string<Args...> fmt, Args&&... args )
{
    static_assert( N > 1, "fixed_string buffer too small to hold any content" );
    auto res = std::format_to_n( chars, N - 1, fmt, std::forward<Args>( args )... );
    HT_ASSERT( res.size < N );
    charCount = res.size;
    chars[ charCount ] = '\0';
}

template<u64 N>
inline bool operator==( const fixed_string<N>& a, const fixed_string<N>& b )
{
    return ( std::size( a ) == std::size( b ) ) && ( std::memcmp( a.chars, b.chars, a.charCount ) == 0 );
}

template<u64 N>
struct std::hash<fixed_string<N>>
{
    u64 operator()( const fixed_string<N>& s ) const
    {
        constexpr u64 kOffset = 14695981039346656037ull;
        constexpr u64 kPrime  = 1099511628211ull;
        u64 h = kOffset;
        for ( u64 i = 0; i < s.charCount; ++i )
        {
            h ^= ( u8 ) s.chars[i];
            h *= kPrime;
        }
        return h;
    }
};

using vfs_path = fixed_string<128>;

#endif // !__HT_FIXED_STRING_H__
