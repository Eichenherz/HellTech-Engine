#pragma once

#ifndef __HT_FIXED_STRING_H__
#define __HT_FIXED_STRING_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_macros.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <format>
#include <string_view>

// NOTE: + 1 for the null terminator
template<typename T, u64 N>
struct fixed_string_base
{
    static_assert( N > 0, "fixed_str can't be 0 in sz !!!!" );

    using value_type = T;
    using view_t     = std::basic_string_view<T>;

    std::array<T, N + 1>    elems;
    u64                     sizeInElems;

                    fixed_string_base() : sizeInElems{ 0 } { elems[ 0 ] = T{}; }

                    fixed_string_base( const T* s ) : fixed_string_base{ view_t( s ) } {}
                    fixed_string_base( view_t sv ) { *this = sv; }

                    template<typename... Args>
                    fixed_string_base( std::basic_format_string<T, std::type_identity_t<Args>...> fmt, Args&&... args );

                    auto& operator=( const T* s ) { return *this = view_t( s ); }
                    auto& operator=( view_t sv );

    constexpr u64   capacity()  const { return N; }

    u64             size( this const fixed_string_base& self )   { return self.sizeInElems; }
    bool            empty( this const fixed_string_base& self )  { return 0 == self.sizeInElems; }

    auto*           data( this auto&& self )    { return std::data( self.elems ); }

    auto            begin( this auto&& self )   { return std::data( self.elems ); }
    auto            end( this auto&& self )     { return std::data( self.elems ) + self.sizeInElems; }

    auto&           operator[]( this auto&& self, u64 i )   { HT_ASSERT( i < self.sizeInElems ); return std::data( self.elems )[ i ]; }
    auto&           back( this auto&& self )                { HT_ASSERT( self.sizeInElems > 0 ); return std::data( self.elems )[ self.sizeInElems - 1 ]; }

    void            clear( this fixed_string_base& self )   { self.sizeInElems = 0; self.elems[ 0 ] = T{}; }

    auto&           push_back( this fixed_string_base& self, T v ) { return self.emplace_back( v ); }
    auto&           emplace_back( this fixed_string_base& self, T v );
    auto            pop_back( this fixed_string_base& self );
    void            resize( this fixed_string_base& self, u64 n, T val = T{} );

    operator view_t()  const { return { std::data( elems ), sizeInElems }; }
    explicit operator const T*() const { return std::data( elems ); }

     bool operator==( const fixed_string_base& other ) const { return view_t( *this ) == view_t( other ); }

};

template<typename T, u64 N>
auto& fixed_string_base<T, N>::operator=( view_t sv )
{
    const u64 strSz = std::size( sv );
    HT_ASSERT( strSz <= N );
    std::memcpy( std::data( elems ), std::data( sv ), strSz * sizeof( T ) );
    sizeInElems         = strSz;
    elems[ sizeInElems ] = T{};
    return *this;
}

template<typename T, u64 N>
template<typename... Args>
fixed_string_base<T, N>::fixed_string_base(
    std::basic_format_string<T, std::type_identity_t<Args>...> fmt, Args&&... args )
{
    auto res = std::format_to_n( std::data( elems ), N, fmt, FWD( args )... );
    // NOTE: res.size is what it WOULD have written, so a truncated one points past the buffer
    HT_ASSERT( ( u64 ) res.size <= N );
    sizeInElems          = ( ( u64 ) res.size < N ) ? ( u64 ) res.size : N;
    elems[ sizeInElems ] = T{};
}

template<typename T, u64 N>
auto& fixed_string_base<T, N>::emplace_back( this fixed_string_base& self, T v )
{
    HT_ASSERT( self.sizeInElems < N );
    T& c = self.elems[ self.sizeInElems++ ] = v;
    self.elems[ self.sizeInElems ] = T{};
    return c;
}

template<typename T, u64 N>
auto fixed_string_base<T, N>::pop_back( this fixed_string_base& self )
{
    HT_ASSERT( self.sizeInElems > 0 );
    T v = self.elems[ --self.sizeInElems ];
    self.elems[ self.sizeInElems ] = T{};
    return v;
}

template<typename T, u64 N>
void fixed_string_base<T, N>::resize( this fixed_string_base& self, u64 n, T val )
{
    HT_ASSERT( n <= N );
    if( n > self.sizeInElems ) std::fill( std::data( self.elems ) + self.sizeInElems, std::data( self.elems ) + n, val );
    self.sizeInElems = n;
    self.elems[ n ]  = T{};
}

template<typename T, u64 N>
struct std::hash<fixed_string_base<T, N>>
{
    u64 operator()( const fixed_string_base<T, N>& s ) const { return std::hash<std::basic_string_view<T>>{}( s ); }
};

template<typename T, u64 N>
struct std::formatter<fixed_string_base<T, N>, T> : std::formatter<std::basic_string_view<T>, T>
{
    using base_t = std::formatter<std::basic_string_view<T>, T>;

    auto format( const fixed_string_base<T, N>& s, auto& ctx ) const { return base_t::format( s, ctx ); }
};

template<u64 N>
using fixed_string = fixed_string_base<char, N>;
template<u64 N>
using fixed_wstring = fixed_string_base<wchar_t, N>;

using vfs_path = fixed_string<128>;

#endif // !__HT_FIXED_STRING_H__