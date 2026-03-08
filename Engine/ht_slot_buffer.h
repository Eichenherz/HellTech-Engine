#pragma once

#ifndef __HT_SLOT_BUFFER_H__
#define __HT_SLOT_BUFFER_H__

#include "core_types.h"

#include "ht_bitset.h"
#include "ht_stretchy_buffer.h"

template<typename T>
struct slot_buffer
{
    struct slot 
    { 
        T   elem; 
        u32 generation; 
    };
    struct hndl32 
    { 
        u32 index : 24; 
        u32 generation : 8; 
    };

    stable_stretchy_buffer<slot>    data;
    bitset                          alive;
    u64                             count = 0;

    slot_buffer() = default;
    slot_buffer( u64 reservedElems ) : data{ reservedElems }, alive{ reservedElems } {}

    u64 size() const { return count; }

    hndl32 PushEntry( T val )
    {
        u32 idx = alive.FindFirstZeroBitIdx();
        if( u32( -1 ) == idx )
        {
            idx = ( u32 ) std::size( data );
            data.push_back( { std::move( val ), 0 } );
            alive.push_back();
        }
        else
        {
            data[ idx ].elem = std::move( val );
        }
        alive.SetEntry( idx, true );
        ++count;
        return { ( u32 ) idx, ( u32 ) data[ idx ].generation };
    }

    T* Get( hndl32 h )
    {
        if( h.index >= std::size( data ) )               return nullptr;
        if( !alive[ h.index ] )                          return nullptr;
        if( data[ h.index ].generation != h.generation ) return nullptr;
        return &data[ h.index ].elem;
    }

    void RemoveEntry( hndl32 h )
    {
        HT_ASSERT( h.index < std::size( data ) && alive[ h.index ] );
        HT_ASSERT( data[ h.index ].generation == h.generation );
        std::destroy_at( &data[ h.index ].elem );
        alive.SetEntry( h.index, false );
        ++data[ h.index ].generation;
        --count;
    }
};

#endif // !__HT_SLOT_BUFFER_H__

