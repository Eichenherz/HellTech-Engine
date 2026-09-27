#pragma once

#ifndef __HT_ATOMIC_STACK_H__
#define __HT_ATOMIC_STACK_H__

#include <ht_core_types.h>
#include <System/sys_sync.h>

#include <bit>
#include <concepts>
#include <ranges>

template<typename T>
concept TYPE_W_LINK = requires( T& v )
{
    { v.pNext } -> std::same_as<T*&>;
};

template<TYPE_W_LINK T>
T* AtomicStackPopNode( tagged_ptr<T>* pStackHead )
{
    // NOTE: this load is not atomic so we could see different values for the fields; the CAS128 will solve that
    // in theory on x86 we could use a simd aligned load, those are atomic now
    tagged_ptr<T> pCurrHead = *pStackHead;
    for( ;; )
    {
        if( !pCurrHead.ptr ) return nullptr;

        tagged_ptr<T> newHead = { .ptr = pCurrHead.ptr->pNext, .tag = pCurrHead.tag + 1 };

        u128 asOpaque = std::bit_cast<u128>( pCurrHead );
        u128 observed = SysAtomicCas128( ( atomic_u128* ) pStackHead,
            std::bit_cast<u128>( newHead ), asOpaque );
        if( observed == asOpaque ) break;

        pCurrHead = std::bit_cast<tagged_ptr<T>>( observed );
    }
    return pCurrHead.ptr;
}

template<TYPE_W_LINK T>
T* /* insertedNode */ AtomicStackPushNode( tagged_ptr<T>* pStackHead, T* node )
{
    tagged_ptr<T> pCurrHead = *pStackHead;
    for( ;; )
    {
        tagged_ptr<T> newHead   = { .ptr = node, .tag = pCurrHead.tag + 1 };
        newHead.ptr->pNext      = pCurrHead.ptr;

        u128 asOpaque = std::bit_cast<u128>( pCurrHead );
        u128 observed = SysAtomicCas128( ( atomic_u128* ) pStackHead,
            std::bit_cast<u128>( newHead ), asOpaque );
        if( observed == asOpaque ) break;

        pCurrHead = std::bit_cast<tagged_ptr<T>>( observed );
    }
    return node;
}

template<typename R>
concept RANGE_W_LINK = std::ranges::range<R> && TYPE_W_LINK<std::ranges::range_value_t<R>>;

auto AtomicStackInit( RANGE_W_LINK auto&& elem )
{
    tagged_ptr<std::ranges::range_value_t<decltype( elem )>> head = {};
    for( auto& e : elem )
    {
        e.pNext     = head.ptr;
        head.ptr    = &e;
    }
    return head;
}

#endif //!__HT_ATOMIC_STACK_H__