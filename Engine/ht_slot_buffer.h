#pragma once

#ifndef __HT_SLOT_BUFFER_H__
#define __HT_SLOT_BUFFER_H__

#include "core_types.h"

#include "ht_stretchy_buffer.h"

// NOTE: this is capped at MAX_ENTRIES_RESERVED

template<TRIVIAL_T T>
struct slot_buffer
{
    static constexpr u64   BIT_WIDTH_MAX_IDX      = 20;
    static constexpr u64   BIT_WIDTH_MAX_GENS     = 11;
                           
    static constexpr u64   MAX_ENTRIES_RESERVED   = 1ull << BIT_WIDTH_MAX_IDX;
                           
    static constexpr u32   SANTINEL_IDX           = MAX_ENTRIES_RESERVED - 1;
    static constexpr u32   MAX_GEN                = ( 1ull << BIT_WIDTH_MAX_GENS ) - 1;

    static constexpr u32   FREELIST_MARKER_U32    = 0xBAD;

    struct freelist_cursor
    {
        u32 slotIdx    : BIT_WIDTH_MAX_IDX = SANTINEL_IDX;
        u32 marker     : 12                = FREELIST_MARKER_U32;
    };
    static_assert( sizeof( T ) >= sizeof( freelist_cursor ) );

    struct hndl32 
    { 
        u32 slotIdx    : BIT_WIDTH_MAX_IDX; 
        u32 generation : BIT_WIDTH_MAX_GENS;
        u32 padding    : 1; 
    };

    virtual_stretchy_buffer<T>      items        = { MAX_ENTRIES_RESERVED };
    freelist_cursor                freelistHead = {};


    u64 size() const { return std::size( items ); } // NOTE: bc we can have dead slots

    T& operator[]( hndl32 h )
    {
        HT_ASSERT( h.slotIdx < std::size( items ) );
        return items[ h.slotIdx ];
    }

    hndl32 PushEntry( const T& val = {} )
    {
        if( SANTINEL_IDX == freelistHead.slotIdx )
        {
            items.push_back( val );
            return { .slotIdx = u32( std::size( items ) - 1 ) };//, .generation = newSlot.generation
        };

        u32 currentFreeSlotIdx = freelistHead.slotIdx;

        freelist_cursor& currentFreelistCursor = ( freelist_cursor& ) items[ currentFreeSlotIdx ];
        HT_ASSERT( FREELIST_MARKER_U32 == currentFreelistCursor.marker );

        freelistHead.slotIdx = currentFreelistCursor.slotIdx;

        std::memset( &items[ currentFreeSlotIdx ], 0, sizeof( items[ currentFreeSlotIdx ] ) );

        items[ currentFreeSlotIdx ] = val;

        return { .slotIdx = currentFreeSlotIdx };
    }

    void RemoveEntry( hndl32 h )
    {
        HT_ASSERT( h.slotIdx < std::size( items ) );

        //HT_ASSERT( slots[ h.slotIdx ].valid );
        //HT_ASSERT( slots[ h.slotIdx ].generation == h.generation );

        std::memset( &items[ h.slotIdx ], 0, sizeof( items[ h.slotIdx ] ) );

        freelist_cursor& currentFreeListCursor = ( freelist_cursor& ) items[ h.slotIdx ];
        currentFreeListCursor = {};

        currentFreeListCursor.slotIdx = freelistHead.slotIdx;
        freelistHead.slotIdx = h.slotIdx;
    }
};

#endif // !__HT_SLOT_BUFFER_H__

