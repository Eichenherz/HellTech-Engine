#pragma once

#ifndef __HT_ATOMIC_STACK_H__
#define __HT_ATOMIC_STACK_H__

#include <ht_core_types.h>
#include <System/sys_sync.h>

#include <ht_mem_arena.h>

#include <bit>

template<typename T>
struct ht_atomic_stack_node_t
{
    ht_atomic_stack_node_t* pNext   = nullptr;
    T                       data    = {};
};

template<typename T, storage_t<ht_atomic_stack_node_t<T>> STORAGE_T>
struct ht_atomic_stack_t : STORAGE_T
{
    static_assert( !STORAGE_T::CAN_GROW );

    using ht_atomic_stack_ptr = tagged_ptr<ht_atomic_stack_node_t<T>>;

    alignas( HT_CACHE_LINE_SZ )
    ht_atomic_stack_ptr head = {};

    constexpr ht_atomic_stack_t( STORAGE_T srcStorage = {} ) : STORAGE_T{ srcStorage }
    {
        for( ht_atomic_stack_node_t<T>& e : this->mem )
        {
            e.pNext     = head.ptr;
            head.ptr    = &e;
        }
    }

    T* pop()
    {
        // NOTE: this load is not atomic so we could see different values for the fields; the CAS128 will solve that.
        // In theory on x86 we could use a simd aligned load, those are atomic now
        ht_atomic_stack_ptr pCurrHead = this->head;
        for( ;; )
        {
            if( !pCurrHead.ptr ) return nullptr;

            ht_atomic_stack_ptr newHead = { .ptr = pCurrHead.ptr->pNext, .tag = pCurrHead.tag + 1 };

            u128 asOpaque = std::bit_cast<u128>( pCurrHead );
            u128 observed = SysAtomicCas128( ( atomic_u128* ) &this->head,
                std::bit_cast<u128>( newHead ), asOpaque );
            if( observed == asOpaque ) break;

            pCurrHead = std::bit_cast<ht_atomic_stack_ptr>( observed );
        }
        return &pCurrHead.ptr->data;
    }

    T* push( T* nodeToInsert )
    {
        auto* pNode = ( ht_atomic_stack_node_t<T>* )( ( u8* ) nodeToInsert - offsetof( ht_atomic_stack_node_t<T>, data ) );

        HT_ASSERT( ( pNode - std::data( this->mem ) ) < std::size( this->mem ) );

        ht_atomic_stack_ptr pCurrHead = this->head;
        for( ;; )
        {
            ht_atomic_stack_ptr newHead   = { .ptr = pNode, .tag = pCurrHead.tag + 1 };
            newHead.ptr->pNext      = pCurrHead.ptr;

            u128 asOpaque = std::bit_cast<u128>( pCurrHead );
            u128 observed = SysAtomicCas128( ( atomic_u128* ) &this->head,
                std::bit_cast<u128>( newHead ), asOpaque );
            if( observed == asOpaque ) break;

            pCurrHead = std::bit_cast<ht_atomic_stack_ptr>( observed );
        }
        return &pNode->data;
    }

    NO_COPY(); NO_MOVE();
};

template<typename T, u64 N>
using inline_atomic_stack = ht_atomic_stack_t<T, inline_storage<ht_atomic_stack_node_t<T>, N>>;

template<typename T>
struct ht_atomic_stack : ht_atomic_stack_t<T, borrowed_storage<ht_atomic_stack_node_t<T>>>
{
    ht_atomic_stack( arena_t auto& arena, u64 elemCount ) :
        ht_atomic_stack::ht_atomic_stack_t{ { ArenaNewArray<ht_atomic_stack_node_t<T>>( arena, elemCount ) } } {}
};


#endif //!__HT_ATOMIC_STACK_H__