#pragma once

#ifndef __HT_VECTOR_H__
#define __HT_VECTOR_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_macros.h>
#include <ht_mem_arena.h>

#include <algorithm>
#include <array>
#include <compare>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>

template<typename A, typename T>
concept vector_allocator_t = TRIVIAL_T<A> && requires( const A a, std::span<T> elemSpan, u64 reqSzInElems )
{
    { a.Grow( elemSpan, reqSzInElems ) } -> std::same_as<std::span<T>>;
    { A::IS_BORROWED } -> std::convertible_to<bool>;
};

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
struct ht_arena_allocator
{
    static constexpr bool IS_BORROWED = false;

    ARENA_T*        pArena = nullptr;

    ht_arena_allocator() = default;
    ht_arena_allocator( ARENA_T* pSrcArena )    : pArena{ pSrcArena } { HT_ASSERT( nullptr != pArena ); }
    ht_arena_allocator( ARENA_T& srcArena )     : pArena{ &srcArena } {}

    std::span<T>    Grow( this const ht_arena_allocator& self, std::span<T> elemSpan, u64 reqSzInElems );
};

template<TRIVIAL_T T, arena_t ARENA_T>
std::span<T> ht_arena_allocator<T, ARENA_T>::Grow( this const ht_arena_allocator& self, std::span<T> elemSpan, u64 reqSzInElems )
{
    HT_ASSERT( ( nullptr != self.pArena ) && ( reqSzInElems > std::size( elemSpan ) ) );

    u64 reqSzInBytes = reqSzInElems * sizeof( T );

    if( 0 == std::size( elemSpan ) )
    {
        return { ( T* ) self.pArena->Alloc( reqSzInBytes, alignof( T ) ), reqSzInElems };
    }

    u64 runSzInBytes        = std::size( elemSpan ) * sizeof( T );
    u64 stretchedSzInBytes  = self.pArena->TryStretchAlloc(
        { ( u8* ) std::data( elemSpan ), runSzInBytes }, reqSzInBytes - runSzInBytes );
    HT_ASSERT( ~0ull != stretchedSzInBytes );

    return { std::data( elemSpan ), stretchedSzInBytes / sizeof( T ) };
}

template<TRIVIAL_T T>
struct ht_borrowed_allocator
{
    static constexpr bool IS_BORROWED = true;

    std::span<T> Grow( this const ht_borrowed_allocator&, std::span<T> elemSpan, u64 reqSzInElems )
    {
        HT_ASSERT( reqSzInElems <= std::size( elemSpan ) );
        return elemSpan;
    }
};

template<typename T, typename SELF>
using ht_const_like_ptr = std::conditional_t<std::is_const_v<std::remove_reference_t<SELF>>, const T*, T*>;

//template<typename R, typename T>
//concept borrowable_run_t =
//    std::ranges::contiguous_range<R>
//    && std::ranges::sized_range<R>
//    && std::ranges::borrowed_range<R>
//    && std::same_as<std::remove_reference_t<std::ranges::range_reference_t<R>>, T>;

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

    std::span<T>        elemSpan    = {};
    u64                 elemCount   = 0;
    EMBED_TYPE ALLOC_T  alloc       = {};

    ht_vector() = default;
    explicit ht_vector( ALLOC_T srcAlloc ) requires ( !ALLOC_T::IS_BORROWED )
                                                                    : alloc{ srcAlloc } {}
    template<u64 E> requires ALLOC_T::IS_BORROWED
    ht_vector( std::span<T, E> srcRun )                             : elemSpan{ srcRun } {}
    //explicit ht_vector( u64 n, ALLOC_T srcAlloc = {} )              : alloc{ srcAlloc } { this->resize( n ); }
    //ht_vector( u64 n, const T& v, ALLOC_T srcAlloc = {} )           : alloc{ srcAlloc } { this->resize( n, v ); }
    //ht_vector( std::initializer_list<T> il, ALLOC_T srcAlloc = {} ) : alloc{ srcAlloc } { this->append_range( il ); }

    //template<std::ranges::input_range R> requires ( !ALLOC_T::IS_BORROWED )
    //ht_vector( std::from_range_t, R&& r, ALLOC_T srcAlloc = {} ) : alloc{ srcAlloc } { this->append_range( FWD( r ) ); }

    ht_vector&  operator=( std::initializer_list<T> il )  { this->assign_range( il ); return *this; }

    auto*       data( this auto&& self ) { return ( ht_const_like_ptr<T, decltype( self )> ) std::data( self.elemSpan ); }

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
    u64         capacity( this const ht_vector& self ) { return std::size( self.elemSpan ); }
    void        reserve( this ht_vector& self, u64 n ) { if( n > std::size( self.elemSpan ) ) self.elemSpan = self.alloc.Grow( self.elemSpan, n ); }
    void        shrink_to_fit( this ht_vector& )       {}

    void        clear( this ht_vector& self )                           { self.elemCount = 0; }
    void        assign( this ht_vector& self, u64 n, const T& v )       { self.elemCount = 0; self.resize( n, v ); }
    void        assign( this ht_vector& self, std::initializer_list<T> il ) { self.assign_range( il ); }
    void        assign_range( this ht_vector& self, std::ranges::input_range auto&& r ) { self.elemCount = 0; self.append_range( FWD( r ) ); }

    void        push_back( this ht_vector& self, const T& v ) { self.reserve( self.elemCount + 1 ); self.elemSpan[ self.elemCount++ ] = v; }
    T&          emplace_back( this ht_vector& self, auto&&... args ) { self.reserve( self.elemCount + 1 ); return self.elemSpan[ self.elemCount++ ] = T{ FWD( args )... }; }
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
        std::ranges::copy( r, std::data( self.elemSpan ) + self.elemCount );
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
    u64 idx = pos - std::data( self.elemSpan );
    HT_ASSERT( idx <= self.elemCount );

    T val = v;
    self.reserve( self.elemCount + n );

    T* slot = std::data( self.elemSpan ) + idx;
    std::memmove( slot + n, slot, ( self.elemCount - idx ) * sizeof( T ) );
    std::fill_n( slot, n, val );
    self.elemCount += n;

    return slot;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
T* ht_vector<T, ALLOC_T>::insert_range( this ht_vector& self, const T* pos, std::ranges::input_range auto&& r )
{
    u64 idx         = pos - std::data( self.elemSpan );
    u64 oldCount    = self.elemCount;
    HT_ASSERT( idx <= oldCount );

    self.append_range( FWD( r ) );

    T* slot = std::data( self.elemSpan ) + idx;
    std::rotate( slot, std::data( self.elemSpan ) + oldCount, std::data( self.elemSpan ) + self.elemCount );

    return slot;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
T* ht_vector<T, ALLOC_T>::erase( this ht_vector& self, const T* first, const T* last )
{
    u64 idx = first - std::data( self.elemSpan );
    u64 n   = last - first;
    HT_ASSERT( ( first <= last ) && ( ( idx + n ) <= self.elemCount ) );

    T* slot = std::data( self.elemSpan ) + idx;
    std::memmove( slot, slot + n, ( self.elemCount - idx - n ) * sizeof( T ) );
    self.elemCount -= n;

    return slot;
}

template<TRIVIAL_T T, vector_allocator_t<T> ALLOC_T>
void ht_vector<T, ALLOC_T>::resize( this ht_vector& self, u64 n, const T& v )
{
    self.reserve( n );
    if( n > self.elemCount ) std::fill_n( std::data( self.elemSpan ) + self.elemCount, n - self.elemCount, v );
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

// NOTE: if we force our vector ht_vector to take a static storage we'd have to rework some stuff so for now this stays
template<TRIVIAL_T T, u64 N>
struct inline_vector
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

    std::array<T, N>    elems;
    u64                 elemCount;

    inline_vector() : elemCount{ 0 } {}
    template<typename U, u64 E>
    requires std::same_as<std::remove_const_t<U>, T>
    inline_vector( std::span<U, E> src );
    template<u64 M>
    inline_vector( const std::array<T, M>& src );
    inline_vector( std::initializer_list<T> il );
    inline_vector( u64 n, const T& v );
    template<std::ranges::input_range R>
    inline_vector( std::from_range_t, R&& r );

    inline_vector&  operator=( std::initializer_list<T> il ) { this->assign_range( il ); return *this; }

    auto*       data( this auto&& self )                    { return ( ht_const_like_ptr<T, decltype( self )> ) std::data( self.elems ); }

    auto        begin( this auto&& self )                   { return std::data( self ); }
    auto        end( this auto&& self )                     { return std::data( self ) + self.elemCount; }
    auto        cbegin( this const inline_vector& self )    { return std::data( self ); }
    auto        cend( this const inline_vector& self )      { return std::data( self ) + self.elemCount; }
    auto        rbegin( this auto&& self ) { return std::reverse_iterator{ std::end( self ) }; }
    auto        rend( this auto&& self ) { return std::reverse_iterator{ std::begin( self ) }; }
    auto        crbegin( this const inline_vector& self ) { return std::reverse_iterator{ std::end( self ) }; }
    auto        crend( this const inline_vector& self ) { return std::reverse_iterator{ std::begin( self ) }; }

    auto&       operator[]( this auto&& self, u64 i ) { HT_ASSERT( i < self.elemCount ); return std::data( self )[ i ]; }
    auto&       front( this auto&& self )             { return self[ 0 ]; }
    auto&       back( this auto&& self )              { return self[ self.elemCount - 1 ]; }

    u64             size( this const inline_vector& self )  { return self.elemCount; }
    bool            empty( this const inline_vector& self ) { return 0 == self.elemCount; }
    constexpr u64   capacity( this const inline_vector& )   { return N; }
    void            reserve( this inline_vector&, u64 n )   { HT_ASSERT( n <= N ); }
    void            shrink_to_fit( this inline_vector& )    {}

    void        clear( this inline_vector& self )                               { self.elemCount = 0; }
    void        assign( this inline_vector& self, u64 n, const T& v )           { self.elemCount = 0; self.resize( n, v ); }
    void        assign( this inline_vector& self, std::initializer_list<T> il ) { self.assign_range( il ); }
    void        assign_range( this inline_vector& self, std::ranges::input_range auto&& r ) { self.elemCount = 0; self.append_range( FWD( r ) ); }

    void        push_back( this inline_vector& self, const T& v ) { HT_ASSERT( self.elemCount < N ); self.elems[ self.elemCount++ ] = v; }
    T&          emplace_back( this inline_vector& self, auto&&... args ) { HT_ASSERT( self.elemCount < N ); return self.elems[ self.elemCount++ ] = T{ FWD( args )... }; }
    void        append_range( this inline_vector& self, std::ranges::input_range auto&& r );
    void        pop_back( this inline_vector& self ) { HT_ASSERT( 0 != self.elemCount ); --self.elemCount; }

    void        resize( this inline_vector& self, u64 n ) { self.resize( n, T{} ); }
    void        resize( this inline_vector& self, u64 n, const T& v );

    bool        operator==( const inline_vector& other ) const { return std::ranges::equal( *this, other ); }
    auto        operator<=>( const inline_vector& other ) const requires std::three_way_comparable<T>;
};

template<TRIVIAL_T T, u64 N>
template<typename U, u64 E>
requires std::same_as<std::remove_const_t<U>, T>
inline_vector<T, N>::inline_vector( std::span<U, E> src ) : elemCount{ std::size( src ) }
{
    HT_ASSERT( std::size( src ) <= N );
    std::ranges::copy( src, std::data( elems ) );
}

template<TRIVIAL_T T, u64 N>
template<u64 M>
inline_vector<T, N>::inline_vector( const std::array<T, M>& src ) : elemCount{ M }
{
    static_assert( M <= N );
    std::ranges::copy( src, std::data( elems ) );
}

template<TRIVIAL_T T, u64 N>
inline_vector<T, N>::inline_vector( std::initializer_list<T> il ) : elemCount{ 0 }
{
    this->append_range( il );
}

template<TRIVIAL_T T, u64 N>
inline_vector<T, N>::inline_vector( u64 n, const T& v ) : elemCount{ 0 }
{
    this->resize( n, v );
}

template<TRIVIAL_T T, u64 N>
template<std::ranges::input_range R>
inline_vector<T, N>::inline_vector( std::from_range_t, R&& r ) : elemCount{ 0 }
{
    this->append_range( FWD( r ) );
}

template<TRIVIAL_T T, u64 N>
void inline_vector<T, N>::append_range( this inline_vector& self, std::ranges::input_range auto&& r )
{
    if constexpr( std::ranges::sized_range<decltype( r )> )
    {
        u64 n = std::ranges::size( r );
        HT_ASSERT( ( self.elemCount + n ) <= N );
        std::ranges::copy( r, std::data( self.elems ) + self.elemCount );
        self.elemCount += n;
    }
    else
    {
        for( auto&& e : r ) self.push_back( e );
    }
}

template<TRIVIAL_T T, u64 N>
void inline_vector<T, N>::resize( this inline_vector& self, u64 n, const T& v )
{
    HT_ASSERT( n <= N );
    if( n > self.elemCount ) std::fill_n( std::data( self.elems ) + self.elemCount, n - self.elemCount, v );
    self.elemCount = n;
}

template<TRIVIAL_T T, u64 N>
auto inline_vector<T, N>::operator<=>( const inline_vector& other ) const requires std::three_way_comparable<T>
{
    return std::lexicographical_compare_three_way(
        std::begin( *this ), std::end( *this ), std::begin( other ), std::end( other ) );
}

static_assert( TRIVIAL_T<arena_vector<u8>> && TRIVIAL_T<borrowed_vector<u8>> && TRIVIAL_T<inline_vector<u8, 4>> );

#endif // !__HT_VECTOR_H__
