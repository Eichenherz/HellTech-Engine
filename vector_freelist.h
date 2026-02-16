#ifndef __VECTOR_FREELIST_H__
#define __VECTOR_FREELIST_H__

// NOTE: all credit to https://upcoder.com/12/vector-hosted-lists/

#include "core_types.h"
#include "ht_error.h"

#include <vector>

struct tagged_index
{
    u32 val : 31;
    u32 marker : 1;
};

struct vector_freelist
{
    static_assert( sizeof( tagged_index ) == sizeof( u32 ) );
    static constexpr u32 MAX_CAP = 0x7FFFFFFFu; // NOTE: "u31(-1)". Will also use as invlid ixd


    std::vector<tagged_index> v;
    u32 maxSize;
    u32 freeHead;
    u32 numFree;

    vector_freelist() = default;

    inline vector_freelist( u32 maxSz )
    {
        HT_ASSERT( maxSz < MAX_CAP );
        maxSize = maxSz;
        freeHead = MAX_CAP;
        numFree = 0;
    }

    inline u64 size() const
    {
        return std::size( v );
    }

    //inline u64 LiveCount() const
    //{
    //    return std::size( v ) - ( u64 ) numDead;
    //}
    //
    //inline u32 NumberDead() const
    //{
    //    return numDead;
    //}

    inline u32 push()
    {
        if( MAX_CAP != freeHead )
        {
            u32 idx = freeHead;

            HT_ASSERT( idx < ( u32 ) std::size( v ) );
            HT_ASSERT( v[ idx ].marker != 0 );
            HT_ASSERT( numFree > 0 );

            freeHead = v[ idx ].val;
            numFree--;

            v[ idx ].marker = 0u;
            return idx;
        }

        HT_ASSERT( std::size( v ) < ( u64 ) maxSize );
        u32 idx = ( u32 ) std::size( v );
        v.push_back( { .val = MAX_CAP, .marker = 0u } );
        return idx;
    }

    inline void erase( u32 i )
    {
        HT_ASSERT( i < ( u32 ) std::size( v ) );
        HT_ASSERT( v[ i ].marker == 0u );

        v[ i ].val = freeHead;
        v[ i ].marker = 1u;
        freeHead = i;
        numFree++;
    }

    inline void reserve( u32 cap )
    {
        HT_ASSERT( cap < MAX_CAP );
        v.reserve( cap );
    }
};

#endif // !__VECTOR_FREELIST_H__
