#pragma once

#ifndef __HT_BITSET_H__
#define __HT_BITSET_H__

#include "core_types.h"

#include "ht_stretchy_buffer.h"

struct bitset
{
    using word_t = u64;

    virtual_stretchy_buffer<word_t> words;
    u64                            bitCount = 0;

            bitset() = default;
            bitset( u64 numReservedEntries ) : words{ ( numReservedEntries + 63 ) / 64 } {}

    void    resize( u64 numEntries, bool val = false );
    void    clear() { std::memset( std::data( words ), 0, std::size( words ) ); }
    u64     size() const { return bitCount; }

    void    push_back( bool val = false );

    bool    operator[]( u64 bitIdx ) const;

    void    SetEntry( u64 bitIdx, bool val );

    u32     FindFirstZeroBitIdx() const;
};

inline void bitset::resize( u64 numEntries, bool val ) 
{
    u64 bits = val ? ~0ull : 0ull;
    words.resize( ( numEntries + 63 ) / 64, bits ); 
    bitCount = numEntries;
}

inline void bitset::push_back( bool val )
{
    if( std::size( words ) * 8 == bitCount )
    {
        words.push_back( 0 );
    }
    ++bitCount;
    SetEntry( size() - 1, val ); 
}

inline bool bitset::operator[]( u64 bitIdx ) const 
{ 
    HT_ASSERT( bitIdx < bitCount );
    return words[ bitIdx / 64 ] & ( 1ull << ( bitIdx % 64 ) ); 
}

inline void bitset::SetEntry( u64 bitIdx, bool val ) 
{
    HT_ASSERT( bitIdx < bitCount );
    word_t& w = words[ bitIdx / 64 ];
    word_t mask = 1ull << ( bitIdx % 64 );
    w = val ? ( w | mask ) : ( w & ~mask );
}

inline u32 bitset::FindFirstZeroBitIdx() const
{
    for( u64 wi = 0; wi < std::size( words ); ++wi )
    {
        u64 currWord = words[ wi ];
        if( currWord != ~0ull )
        {
            u32 idx = wi * 64 + _tzcnt_u64( ~currWord );
            if( idx >= size() ) break;
            return idx;
        }
    }

    return u32( -1 );
}

#endif // !__HT_BITSET_H__
