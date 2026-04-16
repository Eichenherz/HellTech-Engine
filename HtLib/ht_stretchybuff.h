#pragma once

#ifndef __HT_VECTOR_H__
#define __HT_VECTOR_H__

#include "ht_core_types.h"
#include "ht_mem_arena.h"

#include "ht_macros.h"

#include <ranges>
#include <algorithm>

template<typename R>
concept CONTIGUOUS_MEMCPY_ABLE = std::ranges::contiguous_range<R> && std::is_trivially_copyable_v<std::ranges::range_value_t<R>>;

template<typename R, typename T>
concept TRIVIAL_RANGE_OF = CONTIGUOUS_MEMCPY_ABLE<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template<typename T, arena_t ARENA_T>
struct ht_vector
{
    using reverse_iter          = std::reverse_iterator<T*>;
    using const_rev_iter        = std::reverse_iterator<const T*>;

    ARENA_T         arena       = {};
    T*              elems       = nullptr;
    u64             elemCount   = 0;
    u64             currentCap  = 0;

                    ht_vector() = default;
                    ht_vector( ARENA_T a );

    T*              begin()         { return elems; }
    T*              end()           { return elems + elemCount; }
    const T*        begin() const   { return elems; }
    const T*        end() const     { return elems + elemCount; }
    reverse_iter    rbegin()        { return reverse_iter( end() ); };
    reverse_iter    rend()          { return reverse_iter( begin() ); }
    const_rev_iter  rbegin() const  { return const_rev_iter( end() ); }
    const_rev_iter  rend()   const  { return const_rev_iter( begin() ); }

    T&              operator[]( u64 i )       { return elems[ i ]; }
    const T&        operator[]( u64 i ) const { return elems[ i ]; }
    T*              data()          { return elems; }
    const T*        data() const    { return elems; }
    u64             size() const    { return elemCount; }
    u64             capacity() const { return currentCap; }

    void            reserve( u64 newCap );

    void            resize( u64 newSize ) { resize( newSize, {} ); }
    void            resize( u64 newSize, const T& val );

    T&              push_back( const T& val );
    T&              push_back( T&& val );

    template<typename... Args>
    T&              emplace_back( Args&&... args );

    template<TRIVIAL_RANGE_OF<T> R>
    void            append_range( R&& rg );

    void            pop_back();

    void            clear();
};

template<typename T>
using virtual_stretchy_buffer = ht_vector<T, virtual_arena>;

template<typename T>
using dynamic_stretchy_buffer = ht_vector<T, dynamic_arena>;

template<typename T, arena_t ARENA_T>
ht_vector<T, ARENA_T>::ht_vector( ARENA_T a ) : arena{ MOV( a ) }
{
    elems;
    currentCap;
}

template<typename T, arena_t ARENA_T>
void ht_vector<T, ARENA_T>::reserve( u64 newCap )
{
    if( newCap <= currentCap )
    {
        return;
    }

    if( nullptr == elems )
    {
        elems = ( T* ) arena.Alloc( newCap * sizeof( T ), alignof( T ) );
    }
    else
    {
        arena.Alloc( ( newCap - currentCap ) * sizeof( T ), alignof( T ) );
    }
    currentCap = newCap;
}

template<typename T, arena_t ARENA_T>
void ht_vector<T, ARENA_T>::resize( u64 newSize, const T& val )
{
    if( newSize > currentCap )
    {
        reserve( newSize );
    }

    if constexpr( std::is_trivially_copyable_v<T> )
    {
        if( newSize > elemCount )
        {
            std::fill( elems + elemCount, elems + newSize, val );
        }
    }
    else
    {
        for( u64 i = elemCount; i < newSize; ++i )
        {
            std::construct_at( &elems[ i ], val );
        }
    }
    
    for( u64 i = newSize; i < elemCount; ++i )
    {
        std::destroy_at( &elems[ i ] );
    }

    elemCount = newSize;
}

template<typename T, arena_t ARENA_T>
T& ht_vector<T, ARENA_T>::push_back( const T& val )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], val );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T, arena_t ARENA_T>
T& ht_vector<T, ARENA_T>::push_back( T&& val )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], std::move( val ) );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T, arena_t ARENA_T>
template<typename... Args>
T& ht_vector<T, ARENA_T>::emplace_back( Args&&... args )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], FWD( args )... );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T, arena_t ARENA_T>
template<TRIVIAL_RANGE_OF<T> R>
void ht_vector<T, ARENA_T>::append_range( R&& rg )
{
    u64 off     = size();
    u64 rgSz    = std::size( rg );
    resize( off + rgSz );
    std::memcpy( data() + off, std::data( rg ), rgSz * sizeof( T ) );
}

template<typename T, arena_t ARENA_T>
void ht_vector<T, ARENA_T>::pop_back()
{
    --elemCount;
    std::destroy_at( &elems[ elemCount ] );
}

template<typename T, arena_t ARENA_T>
void ht_vector<T, ARENA_T>::clear()
{
    for( u64 i = 0; i < elemCount; ++i )
    {
        std::destroy_at( &elems[ i ] );
    }
    elemCount = 0;
}

#endif // !__HT_VECTOR_H__

