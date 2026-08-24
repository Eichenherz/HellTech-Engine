#pragma once

#ifndef __HT_FIXED_VECTOR_H__
#define __HT_FIXED_VECTOR_H__

#include "ht_core_types.h"
#include "ht_error.h"
#include <array>
#include <ranges>
#include <span>


template<TRIVIAL_T T, u64 N>
struct fixed_vector
{
    using value_type             = T;
    using size_type              = u64;
    using difference_type        = i64;
    using reference              = T&;
    using const_reference        = const T&;
    using pointer                = T*;
    using const_pointer          = const T*;
    using iterator               = typename std::array<T,N>::iterator;
    using const_iterator         = typename std::array<T,N>::const_iterator;
    using reverse_iterator       = typename std::array<T,N>::reverse_iterator;
    using const_reverse_iterator = typename std::array<T,N>::const_reverse_iterator;

    std::array<T, N>        elems;
    u64                     elemCount = 0;

                            fixed_vector() = default;

                            fixed_vector( std::initializer_list<T> il );

                            template<typename Iter>
                            fixed_vector( Iter first, Iter last );

                            // NOTE: templated on the span's element type so a span<T> converts in a single
                            // user-defined conversion, copy-init from one would need 2 with span<const T>
                            template<typename U, std::size_t E>
                                requires std::same_as<std::remove_const_t<U>, T>
                            fixed_vector( std::span<U, E> s );

                            // NOTE: non-explicit so views assign through a braced init: fv = { std::from_range, r };
                            template<std::ranges::sized_range R>
                                requires std::convertible_to<std::ranges::range_reference_t<R>, T>
                            fixed_vector( std::from_range_t, R&& r );

                            fixed_vector( const fixed_vector& )            = default;
                            fixed_vector& operator=( const fixed_vector& ) = default;
                            fixed_vector( fixed_vector&& )                 = default;
                            fixed_vector& operator=( fixed_vector&& )      = default;

    u64                     size()        const { return elemCount; }
    constexpr u64           capacity()    const { return N; }

    reference               push_back( const T& v );

    template<typename... Args>
    reference               emplace_back( Args&&... args );

    value_type              pop_back()              { HT_ASSERT( elemCount > 0 ); return elems[ --elemCount ]; }
    void                    clear()                 { elemCount = 0; }

    void                    resize( u64 n, const T& val = T{} );

    reference               operator[]( u64 i )       { HT_ASSERT( i < elemCount ); return elems[ i ]; }
    const_reference         operator[]( u64 i ) const { HT_ASSERT( i < elemCount ); return elems[ i ]; }

    T*                      data()       { return std::data( elems ); }
    const T*                data() const { return std::data( elems ); }

    iterator                begin()        { return std::begin( elems ); }
    const_iterator          begin()  const { return std::cbegin( elems ); }
    const_iterator          cbegin() const { return std::cbegin( elems ); }

    iterator                end()          { return std::begin( elems ) + elemCount; }
    const_iterator          end()    const { return std::cbegin( elems ) + elemCount; }
    const_iterator          cend()   const { return std::cbegin( elems ) + elemCount; }

    reverse_iterator        rbegin()        { return reverse_iterator( end() ); }
    const_reverse_iterator  rbegin()  const { return const_reverse_iterator( end() ); }
    const_reverse_iterator  crbegin() const { return const_reverse_iterator( cend() ); }

    reverse_iterator        rend()          { return reverse_iterator( begin() ); }
    const_reverse_iterator  rend()    const { return const_reverse_iterator( begin() ); }
    const_reverse_iterator  crend()   const { return const_reverse_iterator( cbegin() ); }
};

template<TRIVIAL_T T, u64 N>
fixed_vector<T, N>::fixed_vector( std::initializer_list<T> il )
{
    HT_ASSERT( std::size( il ) <= N );
    for ( const T& v : il )
    {
        elems[ elemCount ] = v;
        elemCount++;
    }

}

template<TRIVIAL_T T, u64 N>
template<typename Iter>
fixed_vector<T, N>::fixed_vector( Iter first, Iter last )
{
    u64 sz = last - first;
    HT_ASSERT( sz <= N );
    for ( ; first != last; ++first )
    {
        elems[ elemCount ] = *first;
        elemCount++;
    }
}

template<TRIVIAL_T T, u64 N>
template<typename U, std::size_t E>
    requires std::same_as<std::remove_const_t<U>, T>
fixed_vector<T, N>::fixed_vector( std::span<U, E> s )
{
    HT_ASSERT( std::size( s ) <= N );
    for ( const T& v : s )
    {
        elems[ elemCount ] = v;
        elemCount++;
    }
}

template<TRIVIAL_T T, u64 N>
template<std::ranges::sized_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
fixed_vector<T, N>::fixed_vector( std::from_range_t, R&& r )
{
    HT_ASSERT( std::ranges::size( r ) <= N );
    for ( auto&& v : r )
    {
        elems[ elemCount ] = v;
        elemCount++;
    }
}

template<TRIVIAL_T T, u64 N>
fixed_vector<T, N>::reference fixed_vector<T, N>::push_back( const T& v )
{
    HT_ASSERT( elemCount < N );
    u64 oldCount = elemCount++;
    return elems[ oldCount ] = v;

}

template<TRIVIAL_T T, u64 N>
template<typename... Args>
fixed_vector<T, N>::reference fixed_vector<T, N>::emplace_back( Args&&... args )
{
    HT_ASSERT( elemCount < N );
    u64 oldCount = elemCount++;
    return elems[ oldCount ] = T{ FWD( args )... };
}

template<TRIVIAL_T T, u64 N>
void fixed_vector<T, N>::resize( u64 n, const T& val )
{
    HT_ASSERT( n <= N );
    for ( u64 i = elemCount; i < n; ++i )
    {
        elems[ i ] = val;
    }
    elemCount = n;
}

#endif // !__HT_FIXED_VECTOR_H__
