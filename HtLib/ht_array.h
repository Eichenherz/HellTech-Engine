#pragma once

#ifndef __HT_ARRAY_H__
#define __HT_ARRAY_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_macros.h>
#include <ht_mem_arena.h>

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

template<typename T, typename SELF>
using ht_const_like_ptr = std::conditional_t<std::is_const_v<std::remove_reference_t<SELF>>, const T*, T*>;

template<TRIVIAL_T T, storage_t<T> STORAGE_T>
struct ht_array : STORAGE_T
{
    using value_type                = T;
    using size_type                 = u64;
    using difference_type           = i64;
    using reference                 = T&;
    using const_reference           = const T&;
    using pointer                   = T*;
    using const_pointer             = const T*;
    using iterator                  = T*;
    using const_iterator            = const T*;
    using reverse_iterator          = std::reverse_iterator<T*>;
    using const_reverse_iterator    = std::reverse_iterator<const T*>;

    u64                 elemCount   = 0;

    ht_array() {}
    ht_array( STORAGE_T srcStorage ) requires ( !STORAGE_T::OWNS_ELEMENTS )
                                                            : STORAGE_T{ srcStorage } {}
    template<u64 E> requires ( !STORAGE_T::OWNS_ELEMENTS && !STORAGE_T::CAN_GROW )
    ht_array( std::span<T, E> srcMem )                     : STORAGE_T{ srcMem } {}
    template<typename U, u64 E>
    requires ( STORAGE_T::OWNS_ELEMENTS && std::same_as<std::remove_const_t<U>, T> )
    ht_array( std::span<U, E> src )                        { this->append_range( src ); }
    ht_array( std::initializer_list<T> il )                { this->append_range( il ); }
    template<arena_t SRC_ARENA_T> requires ( STORAGE_T::CAN_GROW )
    ht_array( SRC_ARENA_T* pSrcArena )                     : STORAGE_T{ {}, pSrcArena } { HT_ASSERT( nullptr != pSrcArena ); }
    template<arena_t SRC_ARENA_T> requires ( STORAGE_T::CAN_GROW )
    ht_array( SRC_ARENA_T& srcArena )                      : STORAGE_T{ {}, &srcArena } {}
    ht_array( std::from_range_t, std::ranges::input_range auto&& r ) { this->append_range( FWD( r ) ); }

    auto*       data( this auto&& self ) { return ( ht_const_like_ptr<T, decltype( self )> ) std::data( self.mem ); }

    auto        begin( this auto&& self )   { return std::data( self ); }
    auto        end( this auto&& self )     { return std::data( self ) + self.elemCount; }
    auto        rbegin( this auto&& self )  { return std::reverse_iterator{ std::end( self ) }; }
    auto        rend( this auto&& self )    { return std::reverse_iterator{ std::begin( self ) }; }

    auto&       operator[]( this auto&& self, u64 i ) { HT_ASSERT( i < self.elemCount ); return std::data( self )[ i ]; }
    // TODO: remove
    auto&       back( this auto&& self )    { return self[ self.elemCount - 1 ]; }

    u64             size( this const ht_array& self )       { return self.elemCount; }
    constexpr u64   capacity( this const ht_array& self )   { return std::size( self.mem ); }
    void            reserve( this ht_array& self, u64 n )   { if( n > std::size( self.mem ) ) self.Grow( n ); }

    void        clear( this ht_array& self ) { self.elemCount = 0; }

    T&          push_back( this ht_array& self, const T& v ) { return self.emplace_back( v ); }
    T&          emplace_back( this ht_array& self, auto&&... args )
    {
        self.reserve( self.elemCount + 1 ); return self.mem[ self.elemCount++ ] = T{ FWD( args )... };
    }
    void        append_range( this ht_array& self, std::ranges::input_range auto&& r );
    void        pop_back( this ht_array& self ) { HT_ASSERT( 0 != self.elemCount ); --self.elemCount; }

    T*          erase( this ht_array& self, const T* pos ) { return self.erase( pos, pos + 1 ); }
    T*          erase( this ht_array& self, const T* first, const T* last );

    void        resize( this ht_array& self, u64 n ) { self.resize( n, T{} ); }
    void        resize( this ht_array& self, u64 n, const T& v );
};

template<TRIVIAL_T T, storage_t<T> STORAGE_T>
void ht_array<T, STORAGE_T>::append_range( this ht_array& self, std::ranges::input_range auto&& r )
{
    if constexpr( std::ranges::sized_range<decltype( r )> )
    {
        u64 n = std::ranges::size( r );
        self.reserve( self.elemCount + n );
        std::ranges::copy( r, std::data( self.mem ) + self.elemCount );
        self.elemCount += n;
    }
    else
    {
        for( auto&& e : r ) self.push_back( e );
    }
}

template<TRIVIAL_T T, storage_t<T> STORAGE_T>
T* ht_array<T, STORAGE_T>::erase( this ht_array& self, const T* first, const T* last )
{
    u64 idx = first - std::data( self.mem );
    u64 n   = last - first;
    HT_ASSERT( ( first <= last ) && ( ( idx + n ) <= self.elemCount ) );

    T* slot = std::data( self.mem ) + idx;
    std::memmove( slot, slot + n, ( self.elemCount - idx - n ) * sizeof( T ) );
    self.elemCount -= n;

    return slot;
}

template<TRIVIAL_T T, storage_t<T> STORAGE_T>
void ht_array<T, STORAGE_T>::resize( this ht_array& self, u64 n, const T& v )
{
    self.reserve( n );
    if( n > self.elemCount ) std::fill_n( std::data( self.mem ) + self.elemCount, n - self.elemCount, v );
    self.elemCount = n;
}

template<typename T, typename STORAGE_T>
inline constexpr bool std::ranges::enable_borrowed_range<ht_array<T, STORAGE_T>> = !STORAGE_T::OWNS_ELEMENTS;

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
using arena_array      = ht_array<T, arena_storage<T, ARENA_T>>;

template<TRIVIAL_T T>
using borrowed_array   = ht_array<T, borrowed_storage<T>>;

template<TRIVIAL_T T, u64 N>
using inline_array     = ht_array<T, inline_storage<T, N>>;

static_assert( TRIVIAL_T<arena_array<u8>> && TRIVIAL_T<borrowed_array<u8>> && TRIVIAL_T<inline_array<u8, 4>> );

#endif // !__HT_ARRAY_H__
