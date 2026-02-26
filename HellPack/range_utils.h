#ifndef __RANGE_UTILS_H__
#define __RANGE_UTILS_H__

#include <ranges>
#include <algorithm>
#include <vector>

template<typename T>
struct typed_view
{
	const T* ptr = nullptr;
	u32 count = 0;

	constexpr const T* data()  const { return ptr; }
	constexpr u32      size()  const { return count; }
	constexpr const T* begin() const { return ptr; }
	constexpr const T* end()   const { return ptr + count; }
	constexpr std::span<const T> span() const { return { ptr, ( u64 ) count }; }
	constexpr const T& operator[](u32 i) const noexcept
	{
		assert( i < count );
		return ptr[ i ];
	}
};

using byte_view = typed_view<u8>;

template<typename T>
static inline byte_view AsBytes( typed_view<T> v )
{
	static_assert( std::is_trivially_copyable_v<T> );
	return { ( const u8* ) std::data( v ), ( u32 ) std::size( v ) * sizeof( T ) };
}

template<typename T>
inline typed_view<T> MakeTypedView( std::span<const T> s )
{
	return { std::data( s ), ( u32 ) std::size( s ) };
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
	return remap | std::views::transform( [&]( Idx oldIdx ) -> const T& { return src[ oldIdx ]; } );
}

inline auto PermutedView( 
	const std::ranges::random_access_range auto& src,
	const std::ranges::random_access_range auto& remap 
) {
	return remap | std::views::transform( [&] ( auto oldIdx ) { return src[ ( u32 ) oldIdx ]; } );
}

template<typename TriIdx, typename PrimIdx>
inline auto PermuteTrianglesByPrimitiveRemap( const std::vector<TriIdx>& oldIdx, const std::vector<PrimIdx>& primitiveIndices ) 
{
	u64 triangleCount = std::size( primitiveIndices );
	HP_ASSERT( ( triangleCount * 3 ) == std::size( oldIdx ) );

	std::vector<TriIdx> newIdx( std::size( oldIdx ) );
	for( u64 ti = 0; ti < triangleCount; ++ti )
	{
		u64 oldTi = primitiveIndices[ ti ];
		u64 src = 3ull * oldTi;
		u64 dst = 3ull * ti;

		newIdx[ dst + 0 ] = oldIdx[ src + 0 ];
		newIdx[ dst + 1 ] = oldIdx[ src + 1 ];
		newIdx[ dst + 2 ] = oldIdx[ src + 2 ];
	}

	return newIdx;
}

template<typename Idx>
inline auto BuildVertexRemapFromPermutedIndices( const std::vector<Idx>& permutedIndices, u64 vtxCount )
{
	constexpr auto INVALID_IDX = u32( -1 );

	HP_ASSERT( Idx( -1 ) >= vtxCount );

	std::vector<Idx> remap( vtxCount, INVALID_IDX );
	u32 next = 0;

	for( Idx idx : permutedIndices )
	{
		Idx oldV = idx;
		if( INVALID_IDX == remap[ oldV ] )
		{
			remap[ oldV ] = next++;
		}
	}

	return remap;
}


inline bool ByteEqual( std::span<const u8> a, std::span<const u8> b )
{
	bool sizeEq = std::size( a ) == std::size( b );
	return sizeEq && ( std::memcmp( std::data( a ), std::data( b ), std::size( a ) ) == 0 );
}

#endif // !__RANGE_UTILS_H__
