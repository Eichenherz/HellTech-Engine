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
    void Acquire() {}
    void Release() {}
};

template<typename U, typename T>
concept assignable_to = std::assignable_from<T&, U>;

template<typename Storage_T, typename Sync_T>
struct ring_buffer : Storage_T
{
    using T = Storage_T::value_type;

    EMBED_TYPE Sync_T   lock = {};
    u64                 head = 0;
    u64                 tail = 0;

    bool TryPush( this auto&& self, assignable_to<T> auto&& v );
    bool TryPop( this auto&& self, T& out );
    auto TryPeekHead( this auto&& self ) -> const T* requires std::same_as<Sync_T, no_sync_policy>;
};

template<typename Storage_T, typename Sync_T>
bool ring_buffer<Storage_T, Sync_T>::TryPush( this auto&& self, assignable_to<T> auto&& v )
{
    HT_ASSERT( IsPowOf2( std::size( self ) ) );

    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( ( self.tail - self.head ) >= std::size( self ) ) return false;
    self[ self.tail & ( std::size( self ) - 1 ) ] = FWD( v );
    ++self.tail;
    return true;
}

template<typename Storage_T, typename Sync_T>
bool ring_buffer<Storage_T, Sync_T>::TryPop( this auto&& self, T& out )
{
    HT_ASSERT( IsPowOf2( std::size( self ) ) );

    self.lock.Acquire();
    defer{ self.lock.Release(); };

    if( self.tail == self.head ) return false;
    out = self[ self.head & ( std::size( self ) - 1 ) ];
    ++self.head;
    return true;
}

template<typename Storage_T, typename Sync_T>
auto ring_buffer<Storage_T, Sync_T>::TryPeekHead( this auto&& self ) -> const T*
    requires std::same_as<Sync_T, no_sync_policy>
{
    HT_ASSERT( IsPowOf2( std::size( self ) ) );

    if( self.tail == self.head ) return nullptr;
    return &self[ self.head & ( std::size( self ) - 1 ) ];
}

template<typename T, u64 N>
using fixed_ringbuff_w_lock = ring_buffer<fixed_storage_policy<T, N>, copyable_srwlock>;

template<typename T>
using ringbuff_w_lock = ring_buffer<std::span<T>, copyable_srwlock>;

template<typename T, u64 N>
using fixed_ringbuff = ring_buffer<fixed_storage_policy<T, N>, no_sync_policy>;

#endif // !__HT_RING_BUFFER_H__
