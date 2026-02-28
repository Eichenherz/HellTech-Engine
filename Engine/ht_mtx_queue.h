#pragma once

#ifndef __HT_MTX_QUEUE_H__
#define __HT_MTX_QUEUE_H__

#include "core_types.h"
#include "System/sys_sync.h"
#include <array>

template<typename T, u64 N>
struct fixed_storage_policy
{
    std::array<T, N> elems;
    u64              capacity() const { return N; }
};

template<typename T>
struct dynamic_storage_policy
{
    std::vector<T>   elems;
                     
                     dynamic_storage_policy() = default;
                     dynamic_storage_policy( u64 n ) { elems.resize( n ); }
    u64              capacity() const { return std::size( elems ); }
};

template<typename T, typename Storage_T>
struct ring_buffer : Storage_T
{
    copyable_srwlock                    lock;
    u64                                 head = 0;
    u64                                 tail = 0;

    using Storage_T::Storage_T; // NOTE: inherit ctor

    template<std::convertible_to<T> TYPE_T>
    inline bool TryPush( TYPE_T&& v )
    {
        std::lock_guard lockGuard{ lock };
        if ( tail - head >= this->capacity() ) return false;
        this->elems[ tail % this->capacity() ] = std::forward<TYPE_T>( v );
        ++tail;
        return true;
    }

    inline bool TryPop( T& out )
    {
        std::lock_guard lockGuard{ lock };
        if ( tail == head ) return false;
        out = this->elems[ head % this->capacity() ];
        ++head;
        return true;
    }
};

template<typename T, u64 N>
using fixed_mtx_queue = ring_buffer<T, fixed_storage_policy<T, N>>;

template<typename T>
using mtx_queue = ring_buffer<T, dynamic_storage_policy<T>>;

#endif // !__HT_MTX_QUEUE_H__
