#pragma once

#ifndef _HANDLE_MAP_H_
#define _HANDLE_MAP_H_

// NOTE: inspired by http://jeffkiah.com/game-engine-containers-1-handle_map/
#include <vector>
#include "sys_os_api.h"

// TODO: pod constraint ?

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

	/**
	* Removes all items, leaving the m_sparseIds set intact by adding each entry to the free-
	* list and incrementing its generation. This operation is slower than @c reset, but safer
	* for the detection of stale handle lookups later (in debug builds). Prefer to use @c reset
	* if safety is not a concern.
	* Complexity is linear.
	*/
	void clear() noexcept;

	/**
	* Removes all items, destroying the m_sparseIds set. Leaves the container's capacity, but
	* otherwise equivalent to a default-constructed container. This is faster than @c clear,
	* but cannot safely detect lookups by stale handles obtained before the reset. Use @c clear
	* if safety is a concern, at least until it's proven not to be a problem.
	* Complexity is constant.
	*/
	void reset() noexcept;

	bool isValid( handle64<T> hndl ) const;
	size_t size() const noexcept { return mItems.size(); }
	size_t capacity() const noexcept { return mItems.capacity(); }

	/**
	* defragment uses the comparison function @c comp to establish an ideal order for the dense
	*	set in order to maximum cache locality for traversals. The dense set can become
	*	fragmented over time due to removal operations. This can be an expensive operation, so
	*	the sort operation is reentrant. Use the @c maxSwaps parameter to limit the number of
	*	swaps that will occur before the function returns.
	* @param[in]	comp	comparison function object, function pointer, or lambda
	* @param[in]	maxSwaps	maximum number of items to reorder in the insertion sort
	*	before the function returns. Pass 0 (default) to run until completion.
	* @tparam	Compare	comparison function object which returns ?true if the first argument is
	*	greater than (i.e. is ordered after) the second. The signature of the comparison
	*	function should be equivalent to the following:
	*	@code bool cmp(const T& a, const T& b); @endcode
	*	The signature does not need to have const &, but the function object must not modify
	*	the objects passed to it.
	* @returns the number of swaps that occurred, keeping in mind that this value could
	*	overflow on very large data sets
	*/
	template <typename Compare>
	size_t	defragment( Compare comp, size_t maxSwaps = 0 );

	dense_set_t& getItems() { return mItems; }
	const dense_set_t& getItems() const { return mItems; }
	meta_set_t& getMeta() { return mMeta; }
	const meta_set_t& getMeta() const { return mMeta; }
	handle_set_t& getIds() { return mSparseIds; }
	const handle_set_t& getIds() const { return mSparseIds; }

	u32			getFreeListFront() const { return mFreeListFront; }
	u32			getFreeListBack() const { return mFreeListBack; }

	u32			getInnerIndex( handle64<T> hndl ) const;
private:
	// NOTE: freeList is empty when the front is set to 32 bit max value (the back will match)
	bool freeListEmpty() const { return mFreeListFront == u32( -1 ); }

private:
	handle_set_t		mSparseIds;	
	dense_set_t	mItems;		
	meta_set_t	mMeta;		
	u32	mFreeListFront = u32(-1); //!< start index in the embedded ComponentId freelist
	u32	mFreeListBack = u32( -1 ); //!< last index in the freelist
	u8		mFragmented = 0; //<! set to 1 if modified by insert or erase since last complete defragment
};

#endif