#pragma once

#ifndef __HT_ARENA_VECTOR_H__
#define __HT_ARENA_VECTOR_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_mem_arena.h>
#include <memory>
#include <ranges>
#include <span>


// NOTE: a same object allocator, so it takes the arena whole and only ever grows into it.
// NOTE: nothing is ever reallocated, the arena's bytes are the capacity and running past it fires
template<TRIVIAL_T T, arena_t Arena>
struct arena_vector
{
    using value_type             = T;
    using size_type              = u64;
    using difference_type        = i64;
    using reference              = T&;
    using const_reference        = const T&;
    using pointer                = T*;
    using const_pointer          = const T*;
    // NOTE: raw pointer bc we get nicer syntax
    using iterator               = T*;
    using const_iterator         = const T*;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    // NOTE: the arena is inline so nothing ever allocates. This is here only because containers
    // that take an allocator aware container ( ankerl's map ) name the typedef unconditionally
    using allocator_type         = std::allocator<T>;

    Arena                   arena;
    u64                     elemCount = 0;

                            arena_vector() = default;

                            arena_vector( Arena a ) : arena{ a } {}

                            // NOTE: ignored, see allocator_type
                            arena_vector( const allocator_type& ) {}

                            arena_vector( std::initializer_list<T> il );

                            template<typename Iter>
                            arena_vector( Iter first, Iter last );

                            // NOTE: templated on the span's element type so a span<T> converts in a single
                            // user-defined conversion, copy-init from one would need 2 with span<const T>
                            template<typename U, std::size_t E>
                                requires std::same_as<std::remove_const_t<U>, T>
                            arena_vector( std::span<U, E> s );

                            // NOTE: non-explicit so views assign through a braced init: fv = { std::from_range, r };
                            template<std::ranges::sized_range R>
                                requires std::convertible_to<std::ranges::range_reference_t<R>, T>
                            arena_vector( std::from_range_t, R&& r );

                            arena_vector( const arena_vector& )            = default;
                            arena_vector& operator=( const arena_vector& ) = default;
                            arena_vector( arena_vector&& )                 = default;
                            arena_vector& operator=( arena_vector&& )      = default;

    u64                     size()        const { return elemCount; }
    u64                     capacity()    const { return ( arena.capacity() - ( ( u8* ) data() - std::data( arena ) ) ) / sizeof( T ); }
    bool                    empty()       const { return 0 == elemCount; }
    allocator_type          get_allocator() const { return {}; }
    void                    shrink_to_fit()     { /* no-op, the arena is what it is */ }

    reference               back()              { HT_ASSERT( elemCount > 0 ); return data()[ elemCount - 1 ]; }
    const_reference         back()        const { HT_ASSERT( elemCount > 0 ); return data()[ elemCount - 1 ]; }

    reference               push_back( const T& v );

    template<typename... Args>
    reference               emplace_back( Args&&... args );

    value_type              pop_back()              { HT_ASSERT( elemCount > 0 ); return data()[ --elemCount ]; }
    void                    clear()                 { elemCount = 0; }

    void                    resize( u64 n, const T& val = T{} );

    reference               operator[]( u64 i )       { HT_ASSERT( i < elemCount ); return data()[ i ]; }
    const_reference         operator[]( u64 i ) const { HT_ASSERT( i < elemCount ); return data()[ i ]; }

    // NOTE: the arena hands out plain bytes, our first element sits on the first spot T can live on
    T*                      data()       { return ( T* ) FwdAlignPot( ( u64 ) std::data( arena ), alignof( T ) ); }
    const T*                data() const { return ( const T* ) FwdAlignPot( ( u64 ) std::data( arena ), alignof( T ) ); }

    iterator                begin()        { return data(); }
    const_iterator          begin()  const { return data(); }
    const_iterator          cbegin() const { return data(); }

    iterator                end()          { return data() + elemCount; }
    const_iterator          end()    const { return data() + elemCount; }
    const_iterator          cend()   const { return data() + elemCount; }

    reverse_iterator        rbegin()        { return reverse_iterator( end() ); }
    const_reverse_iterator  rbegin()  const { return const_reverse_iterator( end() ); }
    const_reverse_iterator  crbegin() const { return const_reverse_iterator( cend() ); }

    reverse_iterator        rend()          { return reverse_iterator( begin() ); }
    const_reverse_iterator  rend()    const { return const_reverse_iterator( begin() ); }
    const_reverse_iterator  crend()   const { return const_reverse_iterator( cbegin() ); }
};

template<TRIVIAL_T T, arena_t Arena>
arena_vector<T, Arena>::arena_vector( std::initializer_list<T> il )
{
    HT_ASSERT( std::size( il ) <= capacity() );
    for ( const T& v : il )
    {
        data()[ elemCount ] = v;
        elemCount++;
    }

}

template<TRIVIAL_T T, arena_t Arena>
template<typename Iter>
arena_vector<T, Arena>::arena_vector( Iter first, Iter last )
{
    u64 sz = last - first;
    HT_ASSERT( sz <= capacity() );
    for ( ; first != last; ++first )
    {
        data()[ elemCount ] = *first;
        elemCount++;
    }
}

template<TRIVIAL_T T, arena_t Arena>
template<typename U, std::size_t E>
    requires std::same_as<std::remove_const_t<U>, T>
arena_vector<T, Arena>::arena_vector( std::span<U, E> s )
{
    HT_ASSERT( std::size( s ) <= capacity() );
    for ( const T& v : s )
    {
        data()[ elemCount ] = v;
        elemCount++;
    }
}

template<TRIVIAL_T T, arena_t Arena>
template<std::ranges::sized_range R>
    requires std::convertible_to<std::ranges::range_reference_t<R>, T>
arena_vector<T, Arena>::arena_vector( std::from_range_t, R&& r )
{
    HT_ASSERT( std::ranges::size( r ) <= capacity() );
    for ( auto&& v : r )
    {
        data()[ elemCount ] = v;
        elemCount++;
    }
}

template<TRIVIAL_T T, arena_t Arena>
arena_vector<T, Arena>::reference arena_vector<T, Arena>::push_back( const T& v )
{
    HT_ASSERT( elemCount < capacity() );
    u64 oldCount = elemCount++;
    return data()[ oldCount ] = v;

}

template<TRIVIAL_T T, arena_t Arena>
template<typename... Args>
arena_vector<T, Arena>::reference arena_vector<T, Arena>::emplace_back( Args&&... args )
{
    HT_ASSERT( elemCount < capacity() );
    u64 oldCount = elemCount++;
    return data()[ oldCount ] = T{ FWD( args )... };
}

template<TRIVIAL_T T, arena_t Arena>
void arena_vector<T, Arena>::resize( u64 n, const T& val )
{
    HT_ASSERT( n <= capacity() );
    for ( u64 i = elemCount; i < n; ++i )
    {
        data()[ i ] = val;
    }
    elemCount = n;
}

// NOTE: the arena is sized and aligned for exactly N of them, so no byte is lost to the head
template<TRIVIAL_T T, u64 N>
using fixed_vector = arena_vector<T, static_arena<N * sizeof( T ), alignof( T )>>;

#endif // !__HT_ARENA_VECTOR_H__
