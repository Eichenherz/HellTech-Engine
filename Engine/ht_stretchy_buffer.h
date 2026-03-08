#pragma once

#ifndef __HT_STERTCHY_BUFFEER_H__
#define __HT_STERTCHY_BUFFEER_H__

#include "core_types.h"

#include "System/sys_mem_arena.h"

#include <algorithm>

template<typename T, arena_t ARENA_T>
struct ht_stretchy_buffer
{
    using reverse_iter = std::reverse_iterator<T*>;
    using const_rev_iter = std::reverse_iterator<const T*>;

    ARENA_T         arena;
    T*              elems = nullptr;
    u64             elemCount = 0;
    u64             currentCap = 0;

                    ht_stretchy_buffer() = default;
                    ht_stretchy_buffer( u64 reservedEntries ) : arena{ reservedEntries * sizeof( T ) } {}

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

    void            pop_back();

    void            clear();
};

template<typename T>
using stable_stretchy_buffer = ht_stretchy_buffer<T, virtual_arena>;

template<typename T, arena_t ARENA_T>
void ht_stretchy_buffer<T, ARENA_T>::reserve( u64 newCap )
{
    if( newCap <= currentCap )
    {
        return;
    }

    if( elems == nullptr )
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
void ht_stretchy_buffer<T, ARENA_T>::resize( u64 newSize, const T& val )
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
T& ht_stretchy_buffer<T, ARENA_T>::push_back( const T& val )
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
T& ht_stretchy_buffer<T, ARENA_T>::push_back( T&& val )
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
T& ht_stretchy_buffer<T, ARENA_T>::emplace_back( Args&&... args )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], std::forward<Args>( args )... );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T, arena_t ARENA_T>
void ht_stretchy_buffer<T, ARENA_T>::pop_back()
{
    --elemCount;
    std::destroy_at( &elems[ elemCount ] );
}

template<typename T, arena_t ARENA_T>
void ht_stretchy_buffer<T, ARENA_T>::clear()
{
    for( u64 i = 0; i < elemCount; ++i )
    {
        std::destroy_at( &elems[ i ] );
    }
    elemCount = 0;
}

#endif // !__HT_STERTCHY_BUFFEER_H__

