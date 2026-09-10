#pragma once

#ifndef __HT_RING_BUFFER_H__
#define __HT_RING_BUFFER_H__

#include <ht_core_types.h>
#include <ht_mem_arena.h>
#include <System/sys_sync.h>


template<typename STORAGE_T>
consteval bool IsPow2StaticCapacity()
{
    if constexpr( STORAGE_T::OWNS_ELEMENTS ) return IsPowOf2( std::tuple_size_v<decltype( STORAGE_T::mem )> );
    else return true;
}

struct no_sync_policy
{
    void Acqurie() {}
    void Release() {}
};

template<typename T, storage_t<T> STORAGE_T, typename Sync_T>
struct ring_buffer : STORAGE_T
{
    static_assert( IsPow2StaticCapacity<STORAGE_T>() );

    EMBED_TYPE Sync_T   lock = {};
    u64                 head = 0;
    u64                 tail = 0;

    ring_buffer() = default;
    ring_buffer( decltype( STORAGE_T::mem ) srcMem );

    template<typename U> requires std::assignable_from<T&, U>
    bool TryPush( this auto&& self, U&& v );
    bool TryPop( this auto&& self, T& out );
    bool TryPopIf( this auto&& self, T& out, auto&& PfnIsPoppable );
};

template<typename T, storage_t<T> STORAGE_T, typename Sync_T>
ring_buffer<T, STORAGE_T, Sync_T>::ring_buffer( decltype( STORAGE_T::mem ) srcMem ) : STORAGE_T{ srcMem }
{
    HT_ASSERT( IsPowOf2( std::size( this->mem ) ) );
}

template<typename T, storage_t<T> STORAGE_T, typename Sync_T>
template<typename U> requires std::assignable_from<T&, U>
bool ring_buffer<T, STORAGE_T, Sync_T>::TryPush( this auto&& self, U&& v )
{
    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( ( self.tail - self.head ) >= std::size( self.mem ) ) return false;
    self.mem[ self.tail & ( std::size( self.mem ) - 1 ) ] = FWD( v );
    ++self.tail;
    return true;
}

template<typename T, storage_t<T> STORAGE_T, typename Sync_T>
bool ring_buffer<T, STORAGE_T, Sync_T>::TryPop( this auto&& self, T& out )
{
    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( self.tail == self.head ) return false;
    out = self.mem[ self.head & ( std::size( self.mem ) - 1 ) ];
    ++self.head;
    return true;
}

template<typename T, storage_t<T> STORAGE_T, typename Sync_T>
bool ring_buffer<T, STORAGE_T, Sync_T>::TryPopIf( this auto&& self, T& out, auto&& PfnIsPoppable )
{
    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( self.tail == self.head ) return false;

    const T& front = self.mem[ self.head & ( std::size( self.mem ) - 1 ) ];
    if( !PfnIsPoppable( front ) ) return false;

    out = front;
    ++self.head;
    return true;
}

template<TRIVIAL_T T, u64 N>
using fixed_ringbuff_w_lock = ring_buffer<T, inline_storage<T, N>, copyable_srwlock>;

template<TRIVIAL_T T>
using ringbuff_w_lock = ring_buffer<T, borrowed_storage<T>, copyable_srwlock>;

template<TRIVIAL_T T>
struct mpmc_slot_t
{
    T                           data        = {};
    // NOTE: lap counter means how many times was the slot lapped by the ring so to say
    alignas( 64 ) atomic_u64    lapCounter  = 0;
};

template<TRIVIAL_T T, storage_t<mpmc_slot_t<T>> STORAGE_T>
struct lockless_ring_buffer : STORAGE_T
{
    static_assert( IsPow2StaticCapacity<STORAGE_T>() );

    alignas( 64 ) atomic_u64 head = 0;
    alignas( 64 ) atomic_u64 tail = 0;

    lockless_ring_buffer() = default;
    lockless_ring_buffer( decltype( STORAGE_T::mem ) srcMem );
};

template<TRIVIAL_T T, storage_t<mpmc_slot_t<T>> STORAGE_T>
lockless_ring_buffer<T, STORAGE_T>::lockless_ring_buffer( decltype( STORAGE_T::mem ) srcMem ) : STORAGE_T{ srcMem }
{
    HT_ASSERT( IsPowOf2( std::size( this->mem ) ) );
}

template <typename... Args>
bool MpmcRingbuffTryEmplace( auto& self, Args &&...args )
{
    u64 head = SysAtomicRead64<sys_fence_t::ACQ>( &self.head );
    for( ;; )
    {
        auto&   slotRef = self.mem[ head & ( std::size( self.mem ) - 1 ) ];
        u64     pushLap = 2 * ( head / std::size( self.mem ) );
        if( SysAtomicRead64<sys_fence_t::ACQ>( &slotRef.lapCounter ) == pushLap )
        {
            if( SysAtomicCas64<sys_fence_t::SEQ_CST>( &self.head, head + 1, head ) == head )
            {
                slotRef.data = { FWD( args )... };
                // NOTE: + 1 bc now only the tail can lap it; otherwise we're full
                SysAtomicWrite64<sys_fence_t::REL>( &slotRef.lapCounter, pushLap + 1 );
                return true;
            }
        }
        else
        {
            const u64 prevHead = head;
            head = SysAtomicRead64<sys_fence_t::ACQ>( &self.head );
            if( prevHead == head ) return false; // NOTE: the ring hasn't moved, it's "FULL"; else head was claimed
        }
    }
}

bool MpmcRingbuffTryPush( auto& self, const auto& val ) { return MpmcRingbuffTryEmplace( self, val ); }

bool MpmcRingbuffTryPopIf( auto& self, auto& out, auto&& PfnIsPoppable )
{
    u64 tail = SysAtomicRead64<sys_fence_t::ACQ>( &self.tail );
    for( ;; )
    {
        auto&   slotRef = self.mem[ tail & ( std::size( self.mem ) - 1 ) ];
        u64     popLap  = 2 * ( tail / std::size( self.mem ) ) + 1;
        if( SysAtomicRead64<sys_fence_t::ACQ>( &slotRef.lapCounter ) == popLap )
        {
            if( !PfnIsPoppable( slotRef.data ) ) return false;

            if( SysAtomicCas64<sys_fence_t::SEQ_CST>( &self.tail, tail + 1, tail ) == tail )
            {
                out = std::exchange( slotRef.data, {} );
                SysAtomicWrite64<sys_fence_t::REL>( &slotRef.lapCounter, popLap + 1 );
                return true;
            }
        }
        else
        {
            const u64 prevTail = tail;
            tail = SysAtomicRead64<sys_fence_t::ACQ>( &self.tail );
            if( prevTail == tail ) return false; // NOTE: "EMPTY"; else tail was claimed
        }
    }
}

bool MpmcRingbuffTryPop( auto& self, auto& out )
{
    return MpmcRingbuffTryPopIf( self, out, []( const auto& ){ return true; } );
}

template<TRIVIAL_T T, u64 N>
using fixed_mpmc_ringbuff = lockless_ring_buffer<T, inline_storage<mpmc_slot_t<T>, N>>;

template<TRIVIAL_T T>
using mpmc_ringbuff = lockless_ring_buffer<T, borrowed_storage<mpmc_slot_t<T>>>;

#endif // !__HT_RING_BUFFER_H__
