#pragma once

#ifndef __HT_STABLE_STERTCHY_BUFFEER_H__
#define __HT_STABLE_STERTCHY_BUFFEER_H__

#include "core_types.h"

#include "System/sys_mem_arena.h"

template<typename T>
struct stable_stretchy_buffer
{
    using reverse_iter = std::reverse_iterator<T*>;
    using const_rev_iter = std::reverse_iterator<const T*>;

    virtual_arena   arena;
    T*              elems = nullptr;
    u64             elemCount = 0;
    u64             currentCap = 0;

                    stable_stretchy_buffer() = default;
                    stable_stretchy_buffer( u64 reserveBytes ) : arena{ reserveBytes } {}

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
    u64             capacity() const { return capacity; }

    void            reserve( u64 newCap );

    void            resize( u64 newSize );

    T&              push_back( const T& val );
    T&              push_back( T&& val );

    template<typename... Args>
    T&              emplace_back( Args&&... args );

    void            pop_back();

    void            clear();
};

template<typename T>
void stable_stretchy_buffer<T>::reserve( u64 newCap )
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
        arena.Alloc( ( newCap - currentCap ) * sizeof( T ), 1 );
    }
    currentCap = newCap;
}

template<typename T>
void stable_stretchy_buffer<T>::resize( u64 newSize )
{
    if( newSize > currentCap )
    {
        reserve( newSize );
    }

    for( u64 i = elemCount; i < newSize; ++i )
    {
        std::construct_at( &elems[ i ] );
    }

    for( u64 i = newSize; i < elemCount; ++i )
    {
        std::destroy_at( &elems[ i ] );
    }

    elemCount = newSize;
}

template<typename T>
T& stable_stretchy_buffer<T>::push_back( const T& val )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], val );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T>
T& stable_stretchy_buffer<T>::push_back( T&& val )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], std::move( val ) );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T>
template<typename... Args>
T& stable_stretchy_buffer<T>::emplace_back( Args&&... args )
{
    if( elemCount == currentCap )
    {
        reserve( currentCap ? currentCap * 2 : 8 );
    }

    std::construct_at( &elems[ elemCount ], std::forward<Args>( args )... );
    ++elemCount;
    return elems[ elemCount - 1 ];
}

template<typename T>
void stable_stretchy_buffer<T>::pop_back()
{
    --elemCount;
    std::destroy_at( &elems[ elemCount ] );
}

template<typename T>
void stable_stretchy_buffer<T>::clear()
{
    for( u64 i = 0; i < elemCount; ++i )
    {
        std::destroy_at( &elems[ i ] );
    }
    elemCount = 0;
}

#endif // !__HT_STABLE_STERTCHY_BUFFEER_H__

