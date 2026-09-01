#pragma once

#ifndef __HT_FIXED_HASHMAP_H__
#define __HT_FIXED_HASHMAP_H__

#include <ht_core_types.h>
#include <ht_arena_vector.h>

#include <ankerl/unordered_dense.h>

// NOTE: ankerl keeps its load factor, initial_shifts and calc_shifts_for_size private
// NOTE: ankerl's map has a load factor of 0.8f and rounds its bucket count up to a pow 2. We keep it as
// an exact ratio so the bucket count and the load ceiling below can't drift apart, 4/5 is exact in fp too
constexpr u64	HT_HASH_MAP_LOAD_NUM		= 4;
constexpr u64	HT_HASH_MAP_LOAD_DEN		= 5;
constexpr float HT_HASH_MAP_LOAD_FACTOR	= float( HT_HASH_MAP_LOAD_NUM ) / HT_HASH_MAP_LOAD_DEN;

// NOTE: Keep in sync with detail::table::allocate_buckets_from_shift / is_full.
// NOTE: smallest pow2 B with B * NUM / DEN >= elemCap, ankerl does it in fp but that only diverges past 2^24
constexpr u64 HtHashBucketCount( u64 elemCap )
{
    return std::bit_ceil( ( elemCap * HT_HASH_MAP_LOAD_DEN + HT_HASH_MAP_LOAD_NUM - 1 ) / HT_HASH_MAP_LOAD_NUM );
}
// NOTE: mirror of is_full(), the real elem ceiling a bucket array holds before it grows
constexpr u64 HtHashMaxLoad( u64 bucketCount ) { return bucketCount * HT_HASH_MAP_LOAD_NUM / HT_HASH_MAP_LOAD_DEN; }

template<typename T, typename HASH_T, typename CMP_T, u64 SLOT_COUNT>
using ht_fixed_hashset = ankerl::unordered_dense::set<
    T,
    HASH_T,
    CMP_T,
    fixed_vector<T, SLOT_COUNT>,
    ankerl::unordered_dense::bucket_type::standard,
    fixed_vector<ankerl::unordered_dense::bucket_type::standard, HtHashBucketCount( SLOT_COUNT )>>;

template<typename KEY_T, typename VAL_T, typename HASH_T, typename CMP_T, u64 SLOT_COUNT>
using ht_fixed_hashmap = ankerl::unordered_dense::map<
    KEY_T,
    VAL_T,
    HASH_T,
    CMP_T,
    fixed_vector<std::pair<KEY_T, VAL_T>, SLOT_COUNT>,
    ankerl::unordered_dense::bucket_type::standard,
    fixed_vector<ankerl::unordered_dense::bucket_type::standard, HtHashBucketCount( SLOT_COUNT )>>;

#endif //!__HT_FIXED_HASHMAP_H__