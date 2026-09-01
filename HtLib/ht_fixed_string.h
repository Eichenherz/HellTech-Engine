#pragma once

#ifndef __HT_FIXED_STRING_H__
#define __HT_FIXED_STRING_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_arena_vector.h>

#include <format>
#include <string_view>

template<typename T, u64 N>
struct fixed_string_base : fixed_vector<T, N>
{
    using base_t = fixed_vector<T, N>;
    using view_t = std::basic_string_view<T>;
    using base_t::data;
    using base_t::elemCount;

                    fixed_string_base() = default;

                    fixed_string_base( const T* s );
                    fixed_string_base( view_t sv );

                    template<typename... Args>
                    fixed_string_base( std::basic_format_string<T, std::type_identity_t<Args>...> fmt, Args&&... args );

                    fixed_string_base( const fixed_string_base& )            = default;
                    fixed_string_base& operator=( const fixed_string_base& ) = default;
                    fixed_string_base( fixed_string_base&& )                 = default;
                    fixed_string_base& operator=( fixed_string_base&& )      = default;

                    fixed_string_base& operator=( const T* s ) { return *this = fixed_string_base( s ); }
                    fixed_string_base& operator=( view_t sv )  { return *this = fixed_string_base( sv ); }

    constexpr u64   capacity()  const { return N - 1; }

    void            push_back( T v );
    template<typename... Args>
    T&              emplace_back( Args&&... args );
    void            pop_back();
    void            resize( u64 n, T val = T{} );


    operator view_t()  const { return { base_t::data(), base_t::size() }; }
    explicit operator const T*() const { return base_t::data(); }
};

template<typename T, u64 N>
fixed_string_base<T, N>::fixed_string_base( const T* s )
{
    u64 len = std::size( view_t( s ) );
    HT_ASSERT( len < N );
    std::memcpy( base_t::data(), s, len * sizeof( T ) );
    elemCount      = len;
    data()[ len ]  = T{};
}

template<typename T, u64 N>
fixed_string_base<T, N>::fixed_string_base( view_t sv )
{
    const u64 strSz = std::size( sv );
    HT_ASSERT( strSz < N );
    std::memcpy( base_t::data(), std::data( sv ), strSz * sizeof( T ) );
    elemCount       = strSz;
    data()[ strSz ] = T{};
}

template<typename T, u64 N>
template<typename... Args>
fixed_string_base<T, N>::fixed_string_base(
    std::basic_format_string<T, std::type_identity_t<Args>...> fmt, Args&&... args )
{
    static_assert( N > 1, "fixed_string buffer too small" );
    auto res    = std::format_to_n( data(), N - 1, fmt, std::forward<Args>( args )... );
    HT_ASSERT( res.size < N );
    elemCount   = res.size;
    data()[ res.size ] = T{};
}

template<typename T, u64 N>
void fixed_string_base<T, N>::push_back( T v )
{
    HT_ASSERT( elemCount < N - 1 );
    data()[ elemCount++ ] = v;
    data()[ elemCount ]   = T{};
}

template<typename T, u64 N>
template<typename... Args>
T& fixed_string_base<T, N>::emplace_back( Args&&... args )
{
    HT_ASSERT( elemCount < N - 1 );
    T& c                = data()[ elemCount++ ] = T{ std::forward<Args>( args )... };
    data()[ elemCount ] = T{};
    return c;
}

template<typename T, u64 N>
void fixed_string_base<T, N>::pop_back()
{
    HT_ASSERT( elemCount > 0 );
    data()[ --elemCount ] = T{};
}

template<typename T, u64 N>
void fixed_string_base<T, N>::resize( u64 n, T val )
{
    HT_ASSERT( n < N );
    for ( u64 i = elemCount; i < n; ++i ) data()[ i ] = val;
    elemCount     = n;
    data()[ n ]   = T{};
}

template<typename T, u64 N>
inline bool operator==( const fixed_string_base<T, N>& a, const fixed_string_base<T, N>& b )
{
    return ( std::size( a ) == std::size( b ) ) &&
        ( std::memcmp( std::data( a ), std::data( b ), std::size( a ) * sizeof( T ) ) == 0 );
}

template<typename T, u64 N>
struct std::hash<fixed_string_base<T, N>>
{
    u64 operator()( const fixed_string_base<T, N>& s ) const
    {
        constexpr u64 kOffset = 14695981039346656037ull;
        constexpr u64 kPrime  = 1099511628211ull;

        u64 h = kOffset;
        std::span<const u8> bytes = { ( const u8* ) std::data( s ), std::size( s ) * sizeof( T ) };
        for( u8 c : bytes )
        {
            h ^= ( u64 ) c;
            h *= kPrime;
        }
        return h;
    }
};

template<typename T, u64 N>
struct std::formatter<fixed_string_base<T, N>, T> : std::formatter<std::basic_string_view<T>, T>
{
    template<typename CTX_T>
    auto format( const fixed_string_base<T, N>& s, CTX_T& ctx ) const
    {
        return std::formatter<std::basic_string_view<T>, T>::format( std::basic_string_view<T>( s ), ctx );
    }
};

template<u64 N>
using fixed_string = fixed_string_base<char, N>;
template<u64 N>
using fixed_wstring = fixed_string_base<wchar_t, N>;

using vfs_path = fixed_string<128>;

#endif // !__HT_FIXED_STRING_H__