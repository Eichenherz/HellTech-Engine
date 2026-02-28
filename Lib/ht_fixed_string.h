#pragma once 

#ifndef __HT_FIXED_STRING_H__
#define __HT_FIXED_STRING_H__

#include "core_types.h"
#include "ht_error.h"
#include "ht_fixed_vector.h"

#include <format>
#include <string_view>

template<u64 N>
struct fixed_string : fixed_vector<char, N>
{
    using base_t = fixed_vector<char, N>;
    using base_t::elems;
    using base_t::elemCount;

                    fixed_string() = default;

                    fixed_string( const char* s );
                    fixed_string( std::string_view sv );

                    template<typename... Args>
                    fixed_string( std::format_string<Args...> fmt, Args&&... args );

                    fixed_string( const fixed_string& )            = default;
                    fixed_string& operator=( const fixed_string& ) = default;
                    fixed_string( fixed_string&& )                 = default;
                    fixed_string& operator=( fixed_string&& )      = default;

                    fixed_string& operator=( const char* s )       { return *this = fixed_string( s ); }
                    fixed_string& operator=( std::string_view sv ) { return *this = fixed_string( sv ); }

    constexpr u64   capacity()  const { return N - 1; }

    void            push_back( char v );
    template<typename... Args>
    char&           emplace_back( Args&&... args );
    void            pop_back();
    void            resize( u64 n, char val = '\0' );


    operator std::string_view() const { return { base_t::data(), base_t::size() }; }
};

template<u64 N>
fixed_string<N>::fixed_string( const char* s )
{
    u64 len = std::strlen( s );
    HT_ASSERT( len < N );
    std::memcpy( base_t::data(), s, len );
    elemCount      = len;
    elems[ len ]   = '\0';
}

template<u64 N>
fixed_string<N>::fixed_string( std::string_view sv )
{
    const u64 strSz = std::size( sv );
    HT_ASSERT( strSz < N );
    std::memcpy( base_t::data(), std::data( sv ), strSz );
    elemCount          = strSz;
    elems[ strSz ] = '\0';
}

template<u64 N>
template<typename... Args>
fixed_string<N>::fixed_string( std::format_string<Args...> fmt, Args&&... args )
{
    static_assert( N > 1, "fixed_string buffer too small" );
    auto res    = std::format_to_n( std::data( elems ), N - 1, fmt, std::forward<Args>( args )... );
    HT_ASSERT( res.size < N );
    elemCount         = res.size;
    elems[ res.size ] = '\0';
}

template<u64 N>
void fixed_string<N>::push_back( char v )
{
    HT_ASSERT( elemCount < N - 1 );
    elems[ elemCount++ ] = v;
    elems[ elemCount ]   = '\0';
}

template<u64 N>
template<typename... Args>
char& fixed_string<N>::emplace_back( Args&&... args )
{
    HT_ASSERT( elemCount < N - 1 );
    char& c            = elems[ elemCount++ ] = char{ std::forward<Args>( args )... };
    elems[ elemCount ] = '\0';
    return c;
}

template<u64 N>
void fixed_string<N>::pop_back()
{
    HT_ASSERT( elemCount > 0 );
    elems[ --elemCount ] = '\0';
}

template<u64 N>
void fixed_string<N>::resize( u64 n, char val )
{
    HT_ASSERT( n < N );
    for ( u64 i = elemCount; i < n; ++i ) elems[ i ] = val;
    elemCount    = n;
    elems[ n ]   = '\0';
}


template<u64 N>
inline bool operator==( const fixed_string<N>& a, const fixed_string<N>& b )
{
    return ( std::size( a ) == std::size( b ) ) && 
        ( std::memcmp( std::data( a.elems ), std::data( b.elems ), std::size( a ) ) == 0 );
}

template<u64 N>
struct std::hash<fixed_string<N>>
{
    u64 operator()( const fixed_string<N>& s ) const
    {
        constexpr u64 kOffset = 14695981039346656037ull;
        constexpr u64 kPrime  = 1099511628211ull;
        u64 h = kOffset;
        for( char c : s )
        {
            h ^= ( u8 ) c;
            h *= kPrime;
        }
        return h;
    }
};

template<u64 N>
struct std::formatter<fixed_string<N>> : std::formatter<std::string_view>
{
    auto format( const fixed_string<N>& s, std::format_context& ctx ) const
    {
        return std::formatter<std::string_view>::format( std::string_view( s ), ctx );
    }
};

using vfs_path = fixed_string<128>;

#endif // !__HT_FIXED_STRING_H__
