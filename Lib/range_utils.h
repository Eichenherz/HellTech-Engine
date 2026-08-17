#ifndef __RANGE_UTILS_H__
#define __RANGE_UTILS_H__

#include "ht_core_types.h"
#include "ht_error.h"

#include <ranges>

template<typename T>
struct typed_view
{
	static constexpr u32 STRIDE = sizeof( T );

	const T*	ptr = nullptr;
	u64			count = 0;

	constexpr const T* data()  const { return ptr; }
	constexpr u64      size()  const { return count; }
	constexpr const T* begin() const { return ptr; }
	constexpr const T* end()   const { return ptr + count; }
	
	constexpr const T& operator[]( u64 i ) const
	{
		HT_ASSERT( i < count );
		return ptr[ i ];
	}
};

using byte_view = typed_view<u8>;

template<typename T>
inline byte_view AsBytes( typed_view<T> v )
{
	static_assert( std::is_trivially_copyable_v<T> );
	return { ( const u8* ) std::data( v ), ( u32 ) std::size( v ) * sizeof( T ) };
}

template<typename T, u64 Extent>
inline std::span<const u8> AsBytes( std::span<T, Extent> s )
{
	return { ( const u8* ) std::data( s ), std::size( s ) * sizeof( T ) };
}

template<typename T, u64 Extent>
inline std::span<u8> AsBytesWritable( std::span<T, Extent> s )
{
	static_assert( !std::is_const_v<T> );
	return { ( u8* ) std::data( s ), std::size( s ) * sizeof( T ) };
}

template<typename T>
inline typed_view<T> MakeTypedView( std::span<const T> s )
{
	return { std::data( s ), ( u32 ) std::size( s ) };
}

template<typename T>
inline typed_view<T> MakeTypedView( const u8* pData, u64 sizeInBytes )
{
	return { ( const T* ) pData, sizeInBytes / sizeof( T ) };
}

template<std::ranges::contiguous_range R>
inline byte_view MakeByteView( const R& r )
{
	using T = std::ranges::range_value_t<R>;

	static_assert( std::is_trivially_copyable_v<T> );
	return { ( const u8* ) std::data( r ), ( u32 ) std::size( r ) * sizeof( T ) };
}

inline byte_view MakeByteView( const u8* pData, u64 sizeInBytes )
{
	return { pData, sizeInBytes };
}

inline auto PermutedView( 
	const std::ranges::random_access_range auto& src,
	const std::ranges::random_access_range auto& remap 
) {
	return remap | std::views::transform( [ & ] ( auto oldIdx ) { return src[ ( u32 ) oldIdx ]; } );
}

inline bool ByteEqual( std::span<const u8> a, std::span<const u8> b )
{
	bool sizeEq = std::size( a ) == std::size( b );
	return sizeEq && ( std::memcmp( std::data( a ), std::data( b ), std::size( a ) ) == 0 );
}

template<typename R>
concept CONTIGUOUS_RANGE_T = std::ranges::contiguous_range<R>;

template<typename R, typename T>
concept CONTIGUOUS_TYPED_RANGE_T = CONTIGUOUS_RANGE_T<R> && std::same_as<std::ranges::range_value_t<R>, T>;

template<TRIVIAL_T T, CONTIGUOUS_TYPED_RANGE_T<T> R, typename Set, typename KeyFn = std::identity>
inline bool RangeHasDuplicates( const R& range, Set& seenElems, KeyFn keyFn = {} )
{
	for( const T& elem : range )
	{
		if( !seenElems.insert( std::invoke( keyFn, elem ) ).second ) return true;
	}
	return false;
}

template<typename T>
inline constexpr auto HtCastTo = []( auto x ) { return static_cast<T>(x); };

template <typename T, u64 Extent>
constexpr u32 HtElemStrideInBytes( std::span<T, Extent> ) { return sizeof( T ); }

#endif // !__RANGE_UTILS_H__
