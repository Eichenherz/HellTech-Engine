#pragma once

#ifndef __HT_MTX_QUEUE_H__
#define __HT_MTX_QUEUE_H__

#include <ht_core_types.h>
#include "System/sys_sync.h"

#include <array>
#include <mutex>

template<typename T, u64 N>
struct fixed_storage_policy
{
    std::array<T, N> elems;
    u64              capacity() const { return N; }
};
// TODO: use our own memory sys
template<typename T>
struct dynamic_storage_policy
{
    std::vector<T>   elems;
                     
                     dynamic_storage_policy() = default;
                     dynamic_storage_policy( u64 n ) { elems.resize( n ); }
    u64              capacity() const { return std::size( elems ); }
};

struct no_sync_policy
{
    struct scoped_lock { scoped_lock( no_sync_policy& ) {} };
};

struct srwlock_policy
{
    copyable_srwlock lock;
    using scoped_lock = std::lock_guard<copyable_srwlock>;
};

template<typename T, typename Storage_T, typename Sync_T>
struct ring_buffer : Storage_T, Sync_T
{
    EMBED_TYPE Sync_T                   lock;
    u64                                 head = 0;
    u64                                 tail = 0;

    using Storage_T::Storage_T; // NOTE: inherit ctor
    using scoped_lock_t = typename Sync_T::scoped_lock;


    inline bool TryPush( const T& v )
    {
        scoped_lock_t guard{ lock };

        if ( tail - head >= this->capacity() ) return false;
        this->elems[ tail % this->capacity() ] = v;
        ++tail;
        return true;
    }

    inline bool TryPush( T&& v )
    {
        scoped_lock_t guard{ lock };
        if ( tail - head >= this->capacity() ) return false;
        this->elems[ tail % this->capacity() ] = MOV( v );
        ++tail;
        return true;
    }

    inline bool TryPop( T& out )
    {
        scoped_lock_t guard{ lock };
        if ( tail == head ) return false;
        out = this->elems[ head % this->capacity() ];
        ++head;
        return true;
    }
};

template<typename T, u64 N>
using fixed_ringbuff_w_lock = ring_buffer<T, fixed_storage_policy<T, N>, srwlock_policy>;

template<typename T>
using ringbuff_w_lock = ring_buffer<T, dynamic_storage_policy<T>, srwlock_policy>;

template<typename T>
using ringbuff = ring_buffer<T, dynamic_storage_policy<T>, no_sync_policy>;

#endif // !__HT_MTX_QUEUE_H__
