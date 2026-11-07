#pragma once

#ifndef HT_SLOT_ARRAY_H
#define HT_SLOT_ARRAY_H

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_array.h>

#include <tuple>


// NOTE: this is capped at HT_SLOT_MAX_ENTRIES

constexpr u64   HT_SLOT_BIT_WIDTH_MAX_IDX   = 20;
constexpr u64   HT_SLOT_BIT_WIDTH_MAX_GENS  = 11;

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

template<typename T>
struct hndl32
{
    static constexpr u64 MAX_ENTRIES = 1ull << HT_SLOT_BIT_WIDTH_MAX_IDX;

    u32 slotIdx     : HT_SLOT_BIT_WIDTH_MAX_IDX;
    u32 generation  : HT_SLOT_BIT_WIDTH_MAX_GENS;
    u32 inUse       : 1;

    auto operator<=>( const hndl32& ) const = default;
};

template<typename ARRAY_T>
struct slot_array : ARRAY_T
{
    using T = decltype( ARRAY_T::value_type::item );
    using hndl_t = hndl32<T>;

    ht_freelist_cursor  freelistHead = ~0u;

    slot_array() = default;

    slot_array( ARRAY_T arr ) : ARRAY_T{ arr } { HT_ASSERT( this->capacity() <= hndl_t::MAX_ENTRIES ); }

    auto&   operator[]( this auto&& self, const hndl_t h )
    {
        HT_ASSERT( h.inUse );
        HT_ASSERT( h.slotIdx < std::size( self ) );

        auto& slot = std::data( self )[ h.slotIdx ];
        HT_ASSERT( slot.hasItem );
        HT_ASSERT( h.generation == slot.generation );

        return slot.item;
    }

    hndl_t  push_entry( this auto&& self, const T& val )
    {
        if( ~0u == self.freelistHead )
        {
            auto& newSlot = self.push_back( { .hasItem = true, .item = val } );
            return { .slotIdx = u32( &newSlot - std::data( self ) ), .generation = newSlot.generation, .inUse = 1 };
        }

        u32     currentFreeSlotIdx  = self.freelistHead;
        auto&   currentFreeSlot     = std::data( self )[ currentFreeSlotIdx ];
        HT_ASSERT( !currentFreeSlot.hasItem );

        self.freelistHead   = currentFreeSlot.nextFree;
        currentFreeSlot     = { .generation = currentFreeSlot.generation, .hasItem = true, .item = val };

        return { .slotIdx = currentFreeSlotIdx, .generation = currentFreeSlot.generation, .inUse = 1 };
    }

    T       remove_entry( this auto&& self, hndl_t h )
    {
        T removedItem = self.slot_array::operator[]( h );

        auto& slot = std::data( self )[ h.slotIdx ];
        HT_ASSERT( ( slot.generation + 1 ) < HT_SLOT_MAX_GEN );

        slot = { .generation = slot.generation + 1, .hasItem = false, .nextFree = self.freelistHead };
        self.freelistHead = h.slotIdx;

        return removedItem;
    }
};

template<TRIVIAL_T T>
using virtual_slot_array    = slot_array<virtual_array<ht_array_slot_t<T>>>;

template<TRIVIAL_T T>
using borrowed_slot_array   = slot_array<borrowed_array<ht_array_slot_t<T>>>;

template<TRIVIAL_T T, u64 N>
using inline_slot_array     = slot_array<inline_array<ht_array_slot_t<T>, N>>;

template<typename...> struct ht_no_item {};

template<TRIVIAL_T... COMP_T>
struct soa_slot_array : borrowed_slot_array<ht_no_item<COMP_T...>>
{
    using slot_array_t  = borrowed_slot_array<ht_no_item<COMP_T...>>;
    using hndl_t        = slot_array_t::hndl_t;

    std::tuple<std::span<COMP_T>...> components;

    soa_slot_array() = default;

    soa_slot_array( arena_t auto& arena, u64 slotCount ) :
        slot_array_t{ ArenaNewArray<typename slot_array_t::value_type>( arena, slotCount ) },
        components{ ArenaNewArray<COMP_T>( arena, slotCount )... } {}

    hndl_t  push_entry( this auto&& self, const COMP_T&... vals )
    {
        hndl_t h = self.slot_array_t::push_entry( {} );
        ( ( std::get<std::span<COMP_T>>( self.components )[ h.slotIdx ] = vals ), ... );
        return h;
    }

    template<TRIVIAL_T T>
    auto&   get( this auto&& self, hndl_t h )
    {
        self.slot_array_t::operator[]( h );
        return std::get<std::span<T>>( self.components )[ h.slotIdx ];
    }

    template<TRIVIAL_T T>
    auto    get( this auto&& self ) { return std::get<std::span<T>>( self.components ).first( std::size( self ) ); }
};

#endif // !HT_SLOT_ARRAY_H
