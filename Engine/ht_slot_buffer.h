#pragma once

#ifndef __HT_SLOT_BUFFER_H__
#define __HT_SLOT_BUFFER_H__

#include "core_types.h"

#include "ht_stretchy_buffer.h"

// NOTE: this is capped at MAX_ENTRIES_RESERVED

template<TRIVIAL_T T>
struct slot_buffer
{
    static constexpr u64 BIT_WIDTH_MAX_ENTITIES = 20;
    static constexpr u64 BIT_WIDTH_MAX_GENS     = 11;

    static constexpr u64 MAX_ENTRIES_RESERVED   = 1ull << BIT_WIDTH_MAX_ENTITIES;

    static constexpr u32 SANTINEL_IDX           = MAX_ENTRIES_RESERVED - 1;
    static constexpr u32 MAX_GEN                = ( 1ull << BIT_WIDTH_MAX_GENS ) - 1;

    struct slot_t 
    { 
        u32 itemIdx    : BIT_WIDTH_MAX_ENTITIES;
        u32 generation : BIT_WIDTH_MAX_GENS;
        u32 valid      : 1;
    };

    struct hndl32 
    { 
        u32 slotIdx    : BIT_WIDTH_MAX_ENTITIES; 
        u32 generation : BIT_WIDTH_MAX_GENS;
        u32 padding    : 1; 
    };

    stable_stretchy_buffer<T>      items        = { MAX_ENTRIES_RESERVED };
    stable_stretchy_buffer<u32>    itemsToSlots = { MAX_ENTRIES_RESERVED };
    stable_stretchy_buffer<slot_t> slots        = { MAX_ENTRIES_RESERVED };
    u32                            freelistHead = SANTINEL_IDX;


    u64 size() const { return std::size( items ); } // NOTE: bc we can have dead slots

    T& operator[]( hndl32 h )
    {
        HT_ASSERT( h.slotIdx < std::size( slots ) );

        slot_t& slot = slots[ h.slotIdx ];
        HT_ASSERT( slot.valid );
        HT_ASSERT( slot.generation == h.generation );
        HT_ASSERT( slot.itemIdx < std::size( items ) );

        return items[ slot.itemIdx ];
    }

    const T& operator[]( hndl32 h ) const
    {
        HT_ASSERT( h.slotIdx < std::size( slots ) );

        const slot_t& slot = slots[ h.slotIdx ];
        HT_ASSERT( slot.valid );
        HT_ASSERT( slot.generation == h.generation );
        HT_ASSERT( slot.itemIdx < std::size( items ) );

        return items[ slot.itemIdx ];
    }

    hndl32 PushEntry( const T& val = {} )
    {
        u32 itemIdx = ( u32 ) std::size( items );

        items.push_back( val );

        u32 currentSlotIdx = freelistHead;

        hndl32 hSlot = {};

        if( SANTINEL_IDX == currentSlotIdx )
        {
            slots.push_back( { .itemIdx = itemIdx, .generation = 0, .valid = true } );
            u32 slotIdx = u32( std::size( slots ) - 1 );
            hSlot = { .slotIdx = slotIdx, .generation = 0 };
        }
        else
        {
            slot_t currFreeSlot = slots[ currentSlotIdx ];
            HT_ASSERT( !currFreeSlot.valid );
            HT_ASSERT( currFreeSlot.generation < MAX_GEN );

            slot_t newSlot = { .itemIdx = itemIdx, .generation = currFreeSlot.generation + 1, .valid = true };
            slots[ currentSlotIdx ] = newSlot;

            freelistHead = currFreeSlot.itemIdx;

            hSlot = { .slotIdx = currentSlotIdx, .generation = newSlot.generation };
        }

        itemsToSlots.push_back( hSlot.slotIdx );

        return hSlot;
    }

    void RemoveEntry( hndl32 h )
    {
        HT_ASSERT( h.slotIdx < std::size( slots ) );
        HT_ASSERT( slots[ h.slotIdx ].valid );
        HT_ASSERT( slots[ h.slotIdx ].generation == h.generation );

        u32 denseIdx = slots[ h.slotIdx ].itemIdx;
        HT_ASSERT( denseIdx < std::size( items ) );

        u32 lastDenseIdx = u32( std::size( items ) - 1 );

        // NOTE: only swap if we're not last entry
        if( denseIdx != lastDenseIdx )
        {
            items[ denseIdx ] = items[ lastDenseIdx ];
            itemsToSlots[ denseIdx ] = itemsToSlots[ lastDenseIdx ];
            slots[ items[ denseIdx ].slotIdx ].itemIdx = denseIdx;
        }
        items.pop_back();
        itemsToSlots.pop_back();

        slots[ h.slotIdx ].valid   = false;
        slots[ h.slotIdx ].itemIdx = freelistHead;
        freelistHead = h.slotIdx;
    }
};

#endif // !__HT_SLOT_BUFFER_H__

