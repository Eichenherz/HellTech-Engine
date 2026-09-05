#pragma once

#ifndef __HT_VECTOR_H__
#define __HT_VECTOR_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_macros.h>
#include <ht_mem_arena.h>

#include <algorithm>
#include <compare>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

template<typename A, typename T>
concept vector_allocator_t = TRIVIAL_T<A> && requires( const A a, std::span<T> run, u64 reqSzInElems )
{
    { a.Grow( run, reqSzInElems ) } -> std::same_as<std::span<T>>;
};

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
struct ht_arena_allocator
{
    ARENA_T*        pArena = nullptr;

    ht_arena_allocator() = default;
    ht_arena_allocator( ARENA_T* pSrcArena )    : pArena{ pSrcArena } { HT_ASSERT( nullptr != pArena ); }
    ht_arena_allocator( ARENA_T& srcArena )     : pArena{ &srcArena } {}

    std::span<T>    Grow( this const ht_arena_allocator& self, std::span<T> run, u64 reqSzInElems );
};

template<TRIVIAL_T T, arena_t ARENA_T>
std::span<T> ht_arena_allocator<T, ARENA_T>::Grow( this const ht_arena_allocator& self, std::span<T> run, u64 reqSzInElems )
{
    HT_ASSERT( ( nullptr != self.pArena ) && ( reqSzInElems > std::size( run ) ) );

    u64 reqSzInBytes = reqSzInElems * sizeof( T );

    if( 0 == std::size( run ) )
    {
        return { ( T* ) self.pArena->Alloc( reqSzInBytes, alignof( T ) ), reqSzInElems };
    }

    u64 runSzInBytes        = std::size( run ) * sizeof( T );
    u64 stretchedSzInBytes  = self.pArena->TryStretchAlloc(
        { ( u8* ) std::data( run ), runSzInBytes }, reqSzInBytes - runSzInBytes );
    HT_ASSERT( ~0ull != stretchedSzInBytes );

    return { std::data( run ), stretchedSzInBytes / sizeof( T ) };
}

template<TRIVIAL_T T>
struct ht_borrowed_allocator
{
    std::span<T> Grow( this const ht_borrowed_allocator&, std::span<T> run, u64 reqSzInElems )
    {
        HT_ASSERT( reqSzInElems <= std::size( run ) );
        return run;
    }
};

template<typename T, typename SELF>
using ht_const_like_ptr = std::conditional_t<std::is_const_v<std::remove_reference_t<SELF>>, const T*, T*>;

template<typename R, typename T>
concept borrowable_run_t =
    std::ranges::contiguous_range<R>
    && std::ranges::sized_range<R>
    && std::ranges::borrowed_range<R>
    && std::same_as<std::remove_reference_t<std::ranges::range_reference_t<R>>, T>;

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
struct ht_vector
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
    using allocator_type            = ALLOC_T;

    std::span<T>        run         = {};
    u64                 elemCount   = 0;
    EMBED_TYPE ALLOC_T  alloc       = {};

    ht_vector() = default;
    explicit ht_vector( ALLOC_T srcAlloc )                          : alloc{ srcAlloc } {}
    template<u64 E>
    ht_vector( std::span<T, E> srcRun, ALLOC_T srcAlloc = {} )      : run{ srcRun }, alloc{ srcAlloc } {}
    explicit ht_vector( u64 n, ALLOC_T srcAlloc = {} )              : alloc{ srcAlloc } { this->resize( n ); }
    ht_vector( u64 n, const T& v, ALLOC_T srcAlloc = {} )           : alloc{ srcAlloc } { this->resize( n, v ); }
    ht_vector( std::initializer_list<T> il, ALLOC_T srcAlloc = {} ) : alloc{ srcAlloc } { this->append_range( il ); }

    template<std::ranges::input_range R>
    requires ( !( std::same_as<ALLOC_T, ht_borrowed_allocator<T>> && borrowable_run_t<R, T> ) )
    ht_vector( std::from_range_t, R&& r, ALLOC_T srcAlloc = {} ) : alloc{ srcAlloc } { this->append_range( FWD( r ) ); }

    template<borrowable_run_t<T> R>
    requires std::same_as<ALLOC_T, ht_borrowed_allocator<T>>
    ht_vector( std::from_range_t, R&& r ) : run( std::ranges::data( r ), std::ranges::size( r ) ) {}

    ht_vector&  operator=( std::initializer_list<T> il )  { this->assign_range( il ); return *this; }

    auto*       data( this auto&& self )                    { return ( ht_const_like_ptr<T, decltype( self )> ) std::data( self.run ); }

    auto        begin( this auto&& self )                   { return std::data( self ); }
    auto        end( this auto&& self )                     { return std::data( self ) + self.elemCount; }
    auto        cbegin( this const ht_vector& self )        { return std::data( self ); }
    auto        cend( this const ht_vector& self )          { return std::data( self ) + self.elemCount; }
    auto        rbegin( this auto&& self ) { return std::reverse_iterator{ std::end( self ) }; }
    auto        rend( this auto&& self ) { return std::reverse_iterator{ std::begin( self ) }; }
    auto        crbegin( this const ht_vector& self ) { return std::reverse_iterator{ std::end( self ) }; }
    auto        crend( this const ht_vector& self ) { return std::reverse_iterator{ std::begin( self ) }; }

    auto&       operator[]( this auto&& self, u64 i ) { HT_ASSERT( i < self.elemCount ); return std::data( self )[ i ]; }
    auto&       front( this auto&& self )             { return self[ 0 ]; }
    auto&       back( this auto&& self )              { return self[ self.elemCount - 1 ]; }

    u64         size( this const ht_vector& self )     { return self.elemCount; }
    bool        empty( this const ht_vector& self )    { return 0 == self.elemCount; }
    u64         capacity( this const ht_vector& self ) { return std::size( self.run ); }
    void        reserve( this ht_vector& self, u64 n ) { if( n > std::size( self.run ) ) self.run = self.alloc.Grow( self.run, n ); }
    void        shrink_to_fit( this ht_vector& )       {}

    void        clear( this ht_vector& self )                           { self.elemCount = 0; }
    void        assign( this ht_vector& self, u64 n, const T& v )       { self.elemCount = 0; self.resize( n, v ); }
    void        assign( this ht_vector& self, std::initializer_list<T> il ) { self.assign_range( il ); }
    void        assign_range( this ht_vector& self, std::ranges::input_range auto&& r ) { self.elemCount = 0; self.append_range( FWD( r ) ); }

    void        push_back( this ht_vector& self, const T& v ) { self.reserve( self.elemCount + 1 ); self.run[ self.elemCount++ ] = v; }
    T&          emplace_back( this ht_vector& self, auto&&... args ) { self.reserve( self.elemCount + 1 ); return self.run[ self.elemCount++ ] = T{ FWD( args )... }; }
    void        append_range( this ht_vector& self, std::ranges::input_range auto&& r );
    void        pop_back( this ht_vector& self ) { HT_ASSERT( 0 != self.elemCount ); --self.elemCount; }

    T*          insert( this ht_vector& self, const T* pos, const T& v ) { return self.insert( pos, 1, v ); }
    T*          insert( this ht_vector& self, const T* pos, u64 n, const T& v );
    T*          insert( this ht_vector& self, const T* pos, std::initializer_list<T> il ) { return self.insert_range( pos, il ); }
    T*          insert_range( this ht_vector& self, const T* pos, std::ranges::input_range auto&& r );
    T*          emplace( this ht_vector& self, const T* pos, auto&&... args ) { return self.insert( pos, T{ FWD( args )... } ); }
    T*          erase( this ht_vector& self, const T* pos ) { return self.erase( pos, pos + 1 ); }
    T*          erase( this ht_vector& self, const T* first, const T* last );

    void        resize( this ht_vector& self, u64 n )  { self.resize( n, T{} ); }
    void        resize( this ht_vector& self, u64 n, const T& v );
    void        swap( this ht_vector& self, ht_vector& other ) { std::swap( self, other ); }

    bool        operator==( const ht_vector& other ) const { return std::ranges::equal( *this, other ); }
    auto        operator<=>( const ht_vector& other ) const requires std::three_way_comparable<T>;
};

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
void ht_vector<T, ALLOC_T>::append_range( this ht_vector& self, std::ranges::input_range auto&& r )
{
    if constexpr( std::ranges::sized_range<decltype( r )> )
    {
        u64 n = std::ranges::size( r );
        self.reserve( self.elemCount + n );
        std::ranges::copy( r, std::data( self.run ) + self.elemCount );
        self.elemCount += n;
    }
    else
    {
        for( auto&& e : r ) self.push_back( e );
    }
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
T* ht_vector<T, ALLOC_T>::insert( this ht_vector& self, const T* pos, u64 n, const T& v )
{
    u64 idx = pos - std::data( self.run );
    HT_ASSERT( idx <= self.elemCount );

    T val = v;
    self.reserve( self.elemCount + n );

    T* slot = std::data( self.run ) + idx;
    std::memmove( slot + n, slot, ( self.elemCount - idx ) * sizeof( T ) );
    std::fill_n( slot, n, val );
    self.elemCount += n;

    return slot;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
T* ht_vector<T, ALLOC_T>::insert_range( this ht_vector& self, const T* pos, std::ranges::input_range auto&& r )
{
    u64 idx         = pos - std::data( self.run );
    u64 oldCount    = self.elemCount;
    HT_ASSERT( idx <= oldCount );

    self.append_range( FWD( r ) );

    T* slot = std::data( self.run ) + idx;
    std::rotate( slot, std::data( self.run ) + oldCount, std::data( self.run ) + self.elemCount );

    return slot;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
T* ht_vector<T, ALLOC_T>::erase( this ht_vector& self, const T* first, const T* last )
{
    u64 idx = first - std::data( self.run );
    u64 n   = last - first;
    HT_ASSERT( ( first <= last ) && ( ( idx + n ) <= self.elemCount ) );

    T* slot = std::data( self.run ) + idx;
    std::memmove( slot, slot + n, ( self.elemCount - idx - n ) * sizeof( T ) );
    self.elemCount -= n;

    return slot;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
void ht_vector<T, ALLOC_T>::resize( this ht_vector& self, u64 n, const T& v )
{
    self.reserve( n );
    if( n > self.elemCount ) std::fill_n( std::data( self.run ) + self.elemCount, n - self.elemCount, v );
    self.elemCount = n;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
auto ht_vector<T, ALLOC_T>::operator<=>( const ht_vector& other ) const requires std::three_way_comparable<T>
{
    return std::lexicographical_compare_three_way(
        std::begin( *this ), std::end( *this ), std::begin( other ), std::end( other ) );
}

template<typename T, typename ALLOC_T>
inline constexpr bool std::ranges::enable_borrowed_range<ht_vector<T, ALLOC_T>> = true;

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
using arena_vector      = ht_vector<T, ht_arena_allocator<T, ARENA_T>>;

template<TRIVIAL_T T>
using borrowed_vector   = ht_vector<T, ht_borrowed_allocator<T>>;

static_assert( TRIVIAL_T<arena_vector<u8>> && TRIVIAL_T<borrowed_vector<u8>> );

#endif // !__HT_VECTOR_H__
