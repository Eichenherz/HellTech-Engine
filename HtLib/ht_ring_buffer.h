#pragma once

#ifndef __HT_RING_BUFFER_H__
#define __HT_RING_BUFFER_H__

#include <ht_core_types.h>
#include <System/sys_sync.h>

#include <array>
#include <span>

template<typename T, u64 N>
struct fixed_storage_policy : std::array<T, N>
{
    static_assert( IsPowOf2( N ) );
};

struct no_sync_policy
{
    void Acqurie() {}
    void Release() {}
};

template<typename T, typename Storage_T, typename Sync_T>
struct ring_buffer : Storage_T
{
    EMBED_TYPE Sync_T   lock = {};
    u64                 head = 0;
    u64                 tail = 0;

    template<typename U> requires std::assignable_from<T&, U>
    bool TryPush( this auto&& self, U&& v );
    bool TryPop( this auto&& self, T& out );
};

template<typename T, typename Storage_T, typename Sync_T>
template<typename U> requires std::assignable_from<T&, U>
bool ring_buffer<T, Storage_T, Sync_T>::TryPush( this auto&& self, U&& v )
{
    HT_ASSERT( IsPowOf2( std::size( self ) ) );

    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( ( self.tail - self.head ) >= std::size( self ) ) return false;
    self[ self.tail & ( std::size( self ) - 1 ) ] = FWD( v );
    ++self.tail;
    return true;
}

template<typename T, typename Storage_T, typename Sync_T>
bool ring_buffer<T, Storage_T, Sync_T>::TryPop( this auto&& self, T& out )
{
    HT_ASSERT( IsPowOf2( std::size( self ) ) );

    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( self.tail == self.head ) return false;
    out = self[ self.head & ( std::size( self ) - 1 ) ];
    ++self.head;
    return true;
}

template<typename T, u64 N>
using fixed_ringbuff_w_lock = ring_buffer<T, fixed_storage_policy<T, N>, copyable_srwlock>;

template<typename T>
using ringbuff_w_lock = ring_buffer<T, std::span<T>, copyable_srwlock>;

#endif // !__HT_RING_BUFFER_H__
