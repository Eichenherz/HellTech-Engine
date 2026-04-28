#ifndef __RANGE_UTILS_H__
#define __RANGE_UTILS_H__

#include "ht_core_types.h"
#include "ht_error.h"

#include <ranges>
#include <vector>

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


template<typename T, typename Idx>
inline auto PermutedView( std::vector<T>& src, const std::vector<Idx>& remap )
{
	return remap | std::views::transform( [&]( Idx oldIdx ) -> T& { return src[ oldIdx ]; } );
}

template<typename T, typename Idx>
inline auto PermutedView( const std::vector<T>& src, const std::vector<Idx>& remap )
{
	return remap | std::views::transform( [ & ] ( Idx oldIdx ) -> const T& { return src[ oldIdx ]; } );
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

#endif // !__RANGE_UTILS_H__
