#pragma once

#ifndef __HT_FIXED_VECTOR_H__
#define __HT_FIXED_VECTOR_H__

#include "core_types.h"
#include "ht_error.h"
#include <array>


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

                            fixed_vector( const fixed_vector& )            = default;
                            fixed_vector& operator=( const fixed_vector& ) = default;
                            fixed_vector( fixed_vector&& )                 = default;
                            fixed_vector& operator=( fixed_vector&& )      = default;

    u64                     size()        const { return elemCount; }
    constexpr u64           capacity()    const { return N; }

    void                    push_back( const T& v ) { HT_ASSERT( elemCount < N ); elems[ elemCount++ ] = v; }

    template<typename... Args>
    T&                      emplace_back( Args&&... args );

    void                    pop_back()              { HT_ASSERT( elemCount > 0 ); --elemCount; }
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
    for ( const T& v : il ) elems[ elemCount++ ] = v;
}

template<TRIVIAL_T T, u64 N>
template<typename Iter>
fixed_vector<T, N>::fixed_vector( Iter first, Iter last )
{
    u64 sz = last - first;
    HT_ASSERT( sz <= N );
    for ( ; first != last; ++first )
    {
        elems[ elemCount++ ] = *first;
    }
}

template<TRIVIAL_T T, u64 N>
template<typename... Args>
T& fixed_vector<T, N>::emplace_back( Args&&... args )
{
    HT_ASSERT( elemCount < N );
    return elems[ elemCount++ ] = T{ std::forward<Args>( args )... };
}

template<TRIVIAL_T T, u64 N>
void fixed_vector<T, N>::resize( u64 n, const T& val )
{
    HT_ASSERT( n <= N );
    for ( u64 i = elemCount; i < n; ++i ) elems[ i ] = val;
    elemCount = n;
}

#endif // !__HT_FIXED_VECTOR_H__
