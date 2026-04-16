#pragma once

#ifndef __HT_STRETCHYBUFF_H__
#define __HT_STRETCHYBUFF_H__

#include "ht_core_types.h"
#include "ht_mem_arena.h"

#include "ht_macros.h"

#include <ranges>
#include <algorithm>

template<typename R>
concept CONTIGUOUS_MEMCPY_ABLE = std::ranges::contiguous_range<R> && std::is_trivially_copyable_v<std::ranges::range_value_t<R>>;

template<typename R, typename T>
concept TRIVIAL_RANGE_OF = CONTIGUOUS_MEMCPY_ABLE<R> && std::same_as<std::ranges::range_value_t<R>, T>;

// TODO: add an explicit push_grow func which takes an arena/allocator thingy
template<typename T>
struct ht_stretchybuff
{
    using reverse_iter          = std::reverse_iterator<T*>;
    using const_rev_iter        = std::reverse_iterator<const T*>;

    T*              elems       = nullptr;
    u64             elemCount   = 0;
    u64             currentCap  = 0;


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
inline ht_stretchybuff<T> HtNewStretchyBuffFromMem( void* mem, u64 cap )
{
    return { .elems = (T*) mem, .currentCap = cap / sizeof( T ) };
}

template<typename T, arena_t Arena>
inline ht_stretchybuff<T> HtANewStretchyBuffFromArena( Arena& arena, u64 count )
{
    T* mem = new ( arena.Alloc( sizeof( T ) * count, alignof( T ) ) ) T[ count ];
    return { .elems = mem, .currentCap = count };
}

template<typename T>
void ht_stretchybuff<T>::resize( u64 newSize, const T& val )
{
    HT_ASSERT( newSize <= currentCap );

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

template<typename T>
T& ht_stretchybuff<T>::push_back( const T& val )
{
    HT_ASSERT( elemCount <= currentCap );
    std::construct_at( &elems[ elemCount ], val );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T>
T& ht_stretchybuff<T>::push_back( T&& val )
{
    HT_ASSERT( elemCount <= currentCap );
    std::construct_at( &elems[ elemCount ], std::move( val ) );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T>
template<typename... Args>
T& ht_stretchybuff<T>::emplace_back( Args&&... args )
{
    HT_ASSERT( elemCount <= currentCap );
    std::construct_at( &elems[ elemCount ], FWD( args )... );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T>
template<TRIVIAL_RANGE_OF<T> R>
void ht_stretchybuff<T>::append_range( R&& rg )
{
    u64 off     = size();
    u64 rgSz    = std::size( rg );
    resize( off + rgSz );
    std::memcpy( data() + ( off * sizeof( T ) ), std::data( rg ), rgSz * sizeof( T ) );
}

template<typename T>
void ht_stretchybuff<T>::pop_back()
{
    --elemCount;
    std::destroy_at( &elems[ elemCount ] );
}

template<typename T>
void ht_stretchybuff<T>::clear()
{
    for( u64 i = 0; i < elemCount; ++i )
    {
        std::destroy_at( &elems[ i ] );
    }
    elemCount = 0;
}

#endif // !__HT_STRETCHYBUFF_H__

