#pragma once

#ifndef __HT_SLOT_ARRAY_H__
#define __HT_SLOT_ARRAY_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_array.h>


// NOTE: this is capped at HT_SLOT_MAX_ENTRIES

constexpr u64   HT_SLOT_BIT_WIDTH_MAX_IDX   = 20;
constexpr u64   HT_SLOT_BIT_WIDTH_MAX_GENS  = 11;

constexpr u64   HT_SLOT_MAX_ENTRIES         = 1ull << HT_SLOT_BIT_WIDTH_MAX_IDX;
constexpr u32   HT_SLOT_MAX_GEN             = ( 1ull << HT_SLOT_BIT_WIDTH_MAX_GENS ) - 1;

using ht_freelist_cursor = u32;

template<TRIVIAL_T T>
struct ht_array_slot_t
{
    u32 generation  = 0;
    u32 hasItem     = false; // NOTE: all slots are empty basically
    union {
        T                   item;
        ht_freelist_cursor  nextFree = ~0u;
    };
};

template<TRIVIAL_T T, storage_t<ht_array_slot_t<T>> STORAGE_T>
struct slot_array : ht_array<ht_array_slot_t<T>, STORAGE_T>
{
    struct hndl32
    {
        u32 slotIdx    : HT_SLOT_BIT_WIDTH_MAX_IDX;
        u32 generation : HT_SLOT_BIT_WIDTH_MAX_GENS;
        u32 padding    : 1;
    };

    ht_freelist_cursor  freelistHead = ~0u;

    auto&   operator[]( this auto&& self, hndl32 h )
    {
        HT_ASSERT( h.slotIdx < std::size( self ) );

        auto& slot = std::data( self )[ h.slotIdx ];
        HT_ASSERT( slot.hasItem );
        HT_ASSERT( slot.generation == h.generation );

        return slot.item;
    }
};

auto SlotArrayPushEntry( auto& self, const auto& val )
{
    using hndl32 = decltype( auto( self ) )::hndl32;

    if( ~0u == self.freelistHead )
    {
        auto& newSlot = self.push_back( { .hasItem = true, .item = val } );
        return hndl32{ .slotIdx = u32( &newSlot - std::data( self ) ), .generation = newSlot.generation };
    }

    u32     currentFreeSlotIdx  = self.freelistHead;
    auto&   currentFreeSlot     = std::data( self )[ currentFreeSlotIdx ];
    HT_ASSERT( !currentFreeSlot.hasItem );

    self.freelistHead   = currentFreeSlot.nextFree;
    currentFreeSlot     = { .generation = currentFreeSlot.generation, .hasItem = true, .item = val };

    return hndl32{ .slotIdx = currentFreeSlotIdx, .generation = currentFreeSlot.generation };
}

auto SlotArrayRemoveEntry( auto& self, auto h )
{
    HT_ASSERT( h.slotIdx < std::size( self ) );

    auto& slot = std::data( self )[ h.slotIdx ];
    HT_ASSERT( slot.hasItem );
    HT_ASSERT( slot.generation == h.generation );

    auto removedItem = slot.item;

    HT_ASSERT( ( slot.generation + 1 ) < HT_SLOT_MAX_GEN );

    slot = { .generation = slot.generation + 1, .hasItem = false, .nextFree = self.freelistHead };
    self.freelistHead = h.slotIdx;

    return removedItem;
}

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
using arena_slot_array      = slot_array<T, arena_storage<ht_array_slot_t<T>, ARENA_T>>;

template<TRIVIAL_T T>
using borrowed_slot_array   = slot_array<T, borrowed_storage<ht_array_slot_t<T>>>;

template<TRIVIAL_T T, u64 N>
using inline_slot_array     = slot_array<T, inline_storage<ht_array_slot_t<T>, N>>;

template<TRIVIAL_T T, arena_t ARENA_T>
borrowed_slot_array<T> HtMakeSlotArray( ARENA_T& arena, u64 slotCount )
{
    return { ArenaNewArray<ht_array_slot_t<T>>( arena, slotCount ) };
}

#endif // !__HT_SLOT_ARRAY_H__
