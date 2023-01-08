#include "handle_map.hpp"

// NOTE: inspired by http://jeffkiah.com/game-engine-containers-1-handle_map/


#include <vector>
#include <assert.h>

#include "sys_os_api.h"

template<typename T>
inline bool operator==( const handle64<T>& a, const handle64<T>& b ) { return ( a.value == b.value ); }
template<typename T>
inline bool operator!=( const handle64<T>& a, const handle64<T>& b ) { return ( a.value != b.value ); }
template<typename T>
inline bool operator< ( const handle64<T>& a, const handle64<T>& b ) { return ( a.value < b.value ); }
template<typename T>
inline bool operator> ( const handle64<T>& a, const handle64<T>& b ) { return ( a.value > b.value ); }


template <typename T>
handle64<T> handle_map<T>::insert( T&& i )
{
	handle64<T> hndl = {};
	this->mFragmented = 1;

	if( freeListEmpty() )
	{
		handle64<T> innerId = { u32( std::size( this->mItems ) ), 1, 0, 0 };
		hndl = innerId;
		hndl.index = u32( std::size( this->mSparseIds ) );
		this->mSparseIds.push_back( innerId );
	}
	else
	{
		u32 outerIndex = this->mFreeListFront;
		handle64<T>& innerId = this->mSparseIds[ outerIndex ];

		// the index of a free slot refers to the next free slot
		this->mFreeListFront = innerId.index; 
		if( this->freeListEmpty() )
		{
			this->mFreeListBack = this->mFreeListFront;
		}

		// convert the index from freelist to inner index
		innerId.free = 0;
		innerId.index = u32( std::size( this->mItems ) );

		hndl = innerId;
		hndl.index = outerIndex;
	}

	this->mItems.push_back( std::forward<T>( i ) );
	this->mMeta.push_back( { hndl.index } );

	return hndl;
}


template <typename T>
handle64<T> handle_map<T>::insert( const T& i )
{
	return this->insert( std::move( T{ i } ) );
}


template <typename T>
size_t handle_map<T>::erase( handle64<T> hndl )
{
	if( !isValid( hndl ) )
	{
		return 0;
	}
	this->mFragmented = 1;

	handle64<T> innerId = this->mSparseIds[ hndl.index ];
	u32 innerIndex = innerId.index;

	// push this slot to the back of the freelist
	innerId.free = 1;
	++innerId.generation; // increment generation so remaining outer ids go stale
	innerId.index = 0xFFFFFFFF; // max numeric value represents the end of the freelist
	this->mSparseIds[ hndl.index ] = innerId; // write outer id changes back to the array

	if( freeListEmpty() )
	{
		// if the freelist was empty, it now starts (and ends) at this index
		this->mFreeListFront = hndl.index;
		this->mFreeListBack = mFreeListFront;
	}
	else
	{
		this->mSparseIds[ mFreeListBack ].index = hndl.index; // previous back of the freelist points to new back
		this->mFreeListBack = hndl.index; // new freelist back is stored
	}

	// remove the component by swapping with the last element, then pop_back
	if( innerIndex != std::size( this->mItems ) - 1 )
	{
		std::swap( this->mItems[ innerIndex ], this->mItems.back() );
		std::swap( this->mMeta[ innerIndex ], this->mMeta.back() );

		// fix the ComponentId index of the swapped component
		this->mSparseIds[ this->mMeta[ innerIndex ].denseToSparse ].index = innerIndex;
	}

	this->mItems.pop_back();
	this->mMeta.pop_back();

	return 1;
}


template <typename T>
void handle_map<T>::clear() noexcept
{
	u32 size = u32( std::size( this->mSparseIds ) );

	if( size > 0 )
	{
		this->mItems.clear();
		this->mMeta.clear();

		this->mFreeListFront = 0;
		this->mFreeListBack = size - 1;
		this->mFragmented = 0;

		for( uint32_t i = 0; i < size; ++i )
		{
			auto& id = this->mSparseIds[ i ];
			id.free = 1;
			++id.generation;
			id.index = i + 1;
		}
		this->mSparseIds[ size - 1 ].index = u32( -1 );
	}
}


template <typename T>
void handle_map<T>::reset() noexcept
{
	this->mFreeListFront = u32( -1 );
	this->mFreeListBack = u32( -1 );
	this->mFragmented = 0;

	this->mItems.clear();
	this->mMeta.clear();
	this->mSparseIds.clear();
}


template <typename T>
inline T& handle_map<T>::at( handle64<T> hndl )
{
	assert( ( hndl.index < std::size( this->mSparseIds ) ) && "outer index out of range" );

	handle64<T> innerId = this->mSparseIds[ hndl.index ];

	assert( ( hndl.generation == innerId.generation ) && "at called with old generation" );
	assert( ( innerId.index < std::size( this->mItems ) ) && "inner index out of range" );

	return this->mItems[ innerId.index ];
}


template <typename T>
inline const T& handle_map<T>::at( handle64<T> hndl ) const
{
	assert( ( hndl.index < std::size( this->mSparseIds ) ) && "outer index out of range" );

	handle64<T> innerId = this->mSparseIds[ hndl.index ];

	assert( ( hndl.generation == innerId.generation ) && "at called with old generation" );
	assert( ( innerId.index < std::size( this->mItems ) ) && "inner index out of range" );

	return this->mItems[ innerId.index ];
}


template <typename T>
inline bool handle_map<T>::isValid( handle64<T> hndl ) const
{
	if( hndl.index >= std::size( this->mSparseIds ) )
	{
		return false;
	}

	handle64<T> innerId = this->mSparseIds[ hndl.index ];

	return ( innerId.index < std::size( this->mItems ) ) && ( hndl.generation == innerId.generation );
}


template <typename T>
inline uint32_t handle_map<T>::getInnerIndex( handle64<T> hndl ) const
{
	assert( ( hndl.index < std::size( this->mSparseIds ) ) && "outer index out of range" );

	handle64<T> innerId = this->mSparseIds[ hndl.index ];

	assert( ( hndl.generation == innerId.generation ) && "at called with old generation" );
	assert( ( innerId.index < std::size( this->mItems ) ) && "inner index out of range" );

	return innerId.index;
}


template <typename T>
template <typename Compare>
size_t handle_map<T>::defragment( Compare comp, size_t maxSwaps )
{
	if( mFragmented == 0 ) { return 0; }
	size_t swaps = 0;

	i32 i = 1;
	for( ; i < std::size(this->mItems ) && ( maxSwaps == 0 || swaps < maxSwaps ); ++i )
	{
		T tmp = this->mItems[ i ];
		meta_t tmpMeta = this->mMeta[ i ];

		i32 j = i - 1;
		i32 j1 = j + 1;

		if( std::is_trivially_copyable<T>::value )
		{
			while( j >= 0 && comp( this->mItems[ j ], tmp ) )
			{
				this->mSparseIds[ this->mMeta[ j ].denseToSparse ].index = j1;
				--j;
				--j1;
			}
			if( j1 != i )
			{
				std::memmove( &this->mItems[ j1 + 1 ], &this->mItems[ j1 ], sizeof( T ) * ( i - j1 ) );
				std::memmove( &this->mMeta[ j1 + 1 ], &this->mMeta[ j1 ], sizeof( meta_t ) * ( i - j1 ) );
				++swaps;

				this->mItems[ j1 ] = tmp;
				this->mMeta[ j1 ] = tmpMeta;
				this->mSparseIds[ this->mMeta[ j1 ].denseToSparse ].index = j1;
			}
		}
		else
		{
			while( j >= 0 && ( maxSwaps == 0 || swaps < maxSwaps ) && comp( this->mItems[ j ], tmp ) )
			{
				this->mItems[ j1 ] = std::move( this->mItems[ j ] );
				this->mMeta[ j1 ] = std::move( this->mMeta[ j ] );
				this->mSparseIds[ this->mMeta[ j1 ].denseToSparse ].index = j1;
				--j;
				--j1;
				++swaps;
			}

			if( j1 != i )
			{
				this->mItems[ j1 ] = tmp;
				this->mMeta[ j1 ] = tmpMeta;
				this->mSparseIds[ this->mMeta[ j1 ].denseToSparse ].index = j1;
			}
		}
	}
	if( i == std::size( this->mItems ) )
	{
		this->mFragmented = 0;
	}

	return swaps;
}
