#pragma once

#ifndef __HT_BITSET_H__
#define __HT_BITSET_H__

#include "core_types.h"

#include <vector>

struct bitset
{
    using word_t = u64;
    std::vector<word_t> words;

    bitset() = default;
    bitset( u64 numBits, bool val = false )
        : words( ( numBits + 63 ) / 64, val ? ~0ull : 0ull ) {}

    void resize( u64 numBits, bool val = false ) 
    {
        words.resize( ( numBits + 63 ) / 64, val ? ~0ull : 0ull );
    }
    void clear() { std::memset( std::data( words ), 0, std::size( words ) ); }
    u64 size() const { return std::size( words ) * 64; }

    bool operator[]( u64 bitIdx ) const { return words[ bitIdx / 64 ] & ( 1ull << ( bitIdx % 64 ) ); }

    void set_entry( u64 bitIdx, bool val ) 
    {
        word_t& w = words[ bitIdx / 64 ];
        word_t mask = 1ull << ( bitIdx % 64 );
        w = val ? ( w | mask ) : ( w & ~mask );
    }
};

#endif // !__HT_BITSET_H__
