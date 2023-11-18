// NOTE: inspired by http://jeffkiah.com/game-engine-containers-1-handle_map/
#include <vector>
#include <assert.h>
#include "sys_os_api.h"

export module handles;

export {
	template<typename T>
	struct handle64
	{
		union
		{
			// NOTE: order of bitfield is important for sorting prioritized by free, then metaData, then generation, then index
			struct
			{
				u32 index;
				u16 generation;
				u16 meta : 15;
				u16 free : 1;
			};
			u64 value;
		};
	};

	template <typename T>
	class handle_map
	{
	public:
		struct meta_t
		{
			// NOTE: index into mSparseIds array stored in mMeta
			u32	denseToSparse;
		};

		using handle_t = handle64<T>;
		using handle_set_t = std::vector<handle_t>;
		using dense_set_t = std::vector<T>;
		using meta_set_t = std::vector<meta_t>;

		handle_map() = default;
		explicit handle_map( size_t reserveCount )
		{
			mSparseIds.reserve( reserveCount );
			mItems.reserve( reserveCount );
			mMeta.reserve( reserveCount );
		}

		T& at( handle64<T> hndl );
		const T& at( handle64<T> hndl ) const;
		T& operator[]( handle64<T> hndl ) { return at( hndl ); }
		const T& operator[]( handle64<T> hndl ) const { return at( hndl ); }

		template <typename... Params>
		handle64<T> emplace( Params... args ) { return insert( T{ args... } ); }

		typename dense_set_t::iterator		begin() { return mItems.begin(); }
		typename dense_set_t::const_iterator	cbegin() const { return mItems.cbegin(); }
		typename dense_set_t::iterator		end() { return mItems.end(); }
		typename dense_set_t::const_iterator	cend() const { return mItems.cend(); }

		size_t erase( handle64<T> hndl );
		handle64<T> insert( T&& i );
		handle64<T> insert( const T& i );

		void clear() noexcept;

		void reset() noexcept;

		bool isValid( handle64<T> hndl ) const;
		size_t size() const noexcept { return std::size( mItems ); }
		size_t capacity() const noexcept { return mItems.capacity(); }

		template <typename Compare>
		size_t	defragment( Compare comp, size_t maxSwaps = 0 );

		dense_set_t& getItems() { return mItems; }
		const dense_set_t& getItems() const { return mItems; }
		meta_set_t& getMeta() { return mMeta; }
		const meta_set_t& getMeta() const { return mMeta; }
		handle_set_t& getIds() { return mSparseIds; }
		const handle_set_t& getIds() const { return mSparseIds; }

		u32			getFreeListFront() const { return mFreeListStartIdx; }
		u32			getFreeListBack() const { return mFreeListEndIdx; }

		u32			getInnerIndex( handle64<T> hndl ) const;
	private:
		// NOTE: freeList is empty when the front is set to 32 bit max value (the back will match)
		bool freeListEmpty() const { return mFreeListStartIdx == u32( -1 ); }

	private:
		handle_set_t		mSparseIds;
		dense_set_t	mItems;
		meta_set_t	mMeta;
		u32	mFreeListStartIdx = u32( -1 ); // start index in the embedded ComponentId freelist
		u32	mFreeListEndIdx = u32( -1 ); // last index in the freelist
		u8		mFragmented = 0; // set to 1 if modified by insert or erase since last complete defragment
	};
}

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
		u32 outerIndex = this->mFreeListStartIdx;
		handle64<T>& innerId = this->mSparseIds[ outerIndex ];

		// the index of a free slot refers to the next free slot
		this->mFreeListStartIdx = innerId.index;
		if( this->freeListEmpty() )
		{
			this->mFreeListEndIdx = this->mFreeListStartIdx;
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
		this->mFreeListStartIdx = hndl.index;
		this->mFreeListEndIdx = mFreeListStartIdx;
	}
	else
	{
		this->mSparseIds[ mFreeListEndIdx ].index = hndl.index; // previous back of the freelist points to new back
		this->mFreeListEndIdx = hndl.index; // new freelist back is stored
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

		this->mFreeListStartIdx = 0;
		this->mFreeListEndIdx = size - 1;
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
	this->mFreeListStartIdx = u32( -1 );
	this->mFreeListEndIdx = u32( -1 );
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
	for( ; i < std::size( this->mItems ) && ( maxSwaps == 0 || swaps < maxSwaps ); ++i )
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