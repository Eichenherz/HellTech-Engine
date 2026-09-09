#pragma once

#ifndef __HT_ATOMIC_STACK_H__
#define __HT_ATOMIC_STACK_H__

#include <ht_core_types.h>
#include <System/sys_sync.h>

#include <bit>
#include <concepts>

template<typename T>
concept TYPE_WITH_LINK = requires( T& v )
{
    { v.pNext } -> std::same_as<T*&>;
};

template<TYPE_WITH_LINK T>
T* AtomicStackPopNode( atomic_u128* pStackHead )
{
    // NOTE: this load is not atomic so we could see different values for the fields; the CAS128 will solve that
    // in theory on x86 we could use a simd aligned load, those are atomic now
    tagged_ptr<T>   pCurrHead = std::bit_cast<tagged_ptr<T>>( *pStackHead );
    for( ;; )
    {
        if( !pCurrHead.ptr ) return nullptr;

        tagged_ptr<T> newHead = { .ptr = pCurrHead.ptr->pNext, .tag = pCurrHead.tag + 1 };

        u128 asOpaque = std::bit_cast<u128>( pCurrHead );
        u128 observed = SysAtomicCas128( pStackHead, std::bit_cast<u128>( newHead ), asOpaque );
        if( observed == asOpaque ) break;

        pCurrHead = std::bit_cast<tagged_ptr<T>>( observed );
    }
    return pCurrHead.ptr;
}

template<TYPE_WITH_LINK T>
T* /* insertedNode */ AtomicStackPushNode( atomic_u128* pStackHead, T* node )
{
    tagged_ptr<T>   pCurrHead   = std::bit_cast<tagged_ptr<T>>( *pStackHead );
    for( ;; )
    {
        tagged_ptr<T> newHead   = { .ptr = node, .tag = pCurrHead.tag + 1 };
        newHead.ptr->pNext      = pCurrHead.ptr;

        u128 asOpaque = std::bit_cast<u128>( pCurrHead );
        u128 observed = SysAtomicCas128( pStackHead, std::bit_cast<u128>( newHead ), asOpaque );
        if( observed == asOpaque ) break;

        pCurrHead = std::bit_cast<tagged_ptr<T>>( observed );
    }
    return node;
}

#endif //!__HT_ATOMIC_STACK_H__