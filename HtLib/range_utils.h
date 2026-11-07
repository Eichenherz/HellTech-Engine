#ifndef __RANGE_UTILS_H__
#define __RANGE_UTILS_H__

#include <ht_core_types.h>
#include <ht_error.h>

#include <algorithm>
#include <ranges>
#include <numeric>

template<typename T, u64 Extent>
std::span<const u8> AsBytes( std::span<T, Extent> s )
{
	return { ( const u8* ) std::data( s ), std::size( s ) * sizeof( T ) };
}

template<typename T, u64 Extent>
std::span<u8> AsBytesWritable( std::span<T, Extent> s )
{
	static_assert( !std::is_const_v<T> );
	return { ( u8* ) std::data( s ), std::size( s ) * sizeof( T ) };
}

template<TRIVIAL_T T, u64 Extent>
std::span<T> FromBytes( std::span<u8, Extent> s )
{
    HT_ASSERT( 0 == ( ( u64 ) std::data( s ) % alignof( T ) ) );
    return { ( T* ) std::data( s ), std::size( s ) / sizeof( T ) };
}

constexpr bool ByteEqual( const std::ranges::contiguous_range auto& a, const std::ranges::contiguous_range auto& b )
{
	auto sizeA = std::size( a ) * sizeof( *std::data( a ) );
	auto sizeB = std::size( b ) * sizeof( *std::data( b ) );
	return ( sizeA == sizeB ) && !std::memcmp( std::data( a ), std::data( b ), sizeA );
}

template<typename R>
concept CONTIGUOUS_RANGE_T = std::ranges::contiguous_range<R>;

template<typename T>
concept KV_QSORT_ELEM_T = std::is_arithmetic_v<T> && ( 4 == sizeof( T ) || 8 == sizeof( T ) );

template<typename R, typename T>
concept CONTIGUOUS_TYPED_RANGE_T = CONTIGUOUS_RANGE_T<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template<TRIVIAL_T T, CONTIGUOUS_TYPED_RANGE_T<T> R, typename Set, typename KeyFn = std::identity>
bool RangeHasDuplicates( const R& range, Set& seenElems, KeyFn keyFn = {} )
{
	for( const T& elem : range )
	{
		if( !seenElems.insert( std::invoke( keyFn, elem ) ).second ) return true;
	}
	return false;
}

namespace ht
{
    HT_FORCEINLINE constexpr auto* end_ptr( std::ranges::contiguous_range auto&& r )
    {
        return std::to_address( std::ranges::end( r ) );
    }

    constexpr auto permuted_view(
        const std::ranges::random_access_range auto& src,
        const std::ranges::random_access_range auto& remap
    ) {
        return remap | std::views::transform( [ &src ]( auto oldIdx ) HT_LAMBDA_FORCEINLINE -> decltype( auto )
        {
            return src[ ( u64 ) oldIdx ];
        } );
    }

    void kv_qsort( KV_QSORT_ELEM_T auto* keys, KV_QSORT_ELEM_T auto* vals, u64 count );
}

template<typename T>
inline constexpr auto HtCastTo = []( auto x ) HT_LAMBDA_FORCEINLINE { return static_cast<T>(x); };

template<typename T>
inline constexpr auto HtReinterpretAs = []( const auto& x ) HT_LAMBDA_FORCEINLINE -> const T& { return *( const T* ) &x; };

template <typename T, u64 Extent>
constexpr u32 HtElemStrideInBytes( std::span<T, Extent> ) { return sizeof( T ); }

constexpr u64 HtRangeSizeInBytes( const std::ranges::sized_range auto& range )
{
    return std::ranges::size( range ) * sizeof( std::ranges::range_value_t<decltype( range )> );
}

template<std::ranges::contiguous_range R>
std::span<const u8> AsBytes( const R& r )
{
    return { ( const u8* ) std::ranges::data( r ), HtRangeSizeInBytes( r ) };
}

constexpr auto HtCopyFullRange( std::ranges::contiguous_range auto&& src, std::ranges::contiguous_range auto&& dst )
    requires std::same_as<std::ranges::range_value_t<decltype( src )>, std::ranges::range_value_t<decltype( dst )>>
{
    HT_ASSERT( std::size( src ) <= std::size( dst ) );
    std::ranges::copy( src, std::data( dst ) );
    return std::span{ std::data( dst ), std::size( src ) };
}

namespace ht::ranges
{
    template<
        std::ranges::common_range R,
        std::output_iterator<std::ranges::range_value_t<R>> OutIt,
        typename T, typename BinOp = std::plus<>>
    constexpr OutIt exclusive_scan( R&& range, OutIt dest, T init, BinOp op = {} )
    {
        return std::exclusive_scan(
            std::ranges::begin( range ), std::ranges::end( range ), dest, MOV( init ), op );
    }

    template<std::ranges::common_range R, typename T, typename BinOp = std::plus<>>
        requires std::ranges::forward_range<R> && std::invocable<BinOp&, T, std::ranges::range_reference_t<R>>
    constexpr auto exclusive_scan( R&& range, T init, BinOp op = {} )
    {
        auto beg = std::ranges::begin( range );
        return std::exclusive_scan( beg, std::ranges::end( range ), beg, MOV( init ), op );
    }

    template<
        std::ranges::common_range R,
        std::output_iterator<std::ranges::range_value_t<R>> OutIt,
        typename T, typename BinOp = std::plus<>>
    constexpr OutIt inclusive_scan( R&& range, OutIt dest, T init, BinOp op = {} )
    {
        return std::inclusive_scan(
            std::ranges::begin( range ), std::ranges::end( range ), dest, op, MOV( init ) );
    }

    template<std::ranges::common_range R, typename T, typename BinOp = std::plus<>>
        requires std::ranges::forward_range<R> && std::invocable<BinOp&, T, std::ranges::range_reference_t<R>>
    constexpr auto inclusive_scan( R&& range, T init, BinOp op = {} )
    {
        auto beg = std::ranges::begin( range );
        return std::inclusive_scan( beg, std::ranges::end( range ), beg, op, MOV( init ) );
    }
}

#endif // !__RANGE_UTILS_H__
