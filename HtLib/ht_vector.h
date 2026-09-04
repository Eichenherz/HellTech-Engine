#pragma once

#ifndef __HT_VECTOR_H__
#define __HT_VECTOR_H__

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_mem_arena.h>
#include <array>
#include <memory>
#include <vector>

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
struct ht_arena_allocator
{
	using value_type								= T;
	using propagate_on_container_copy_assignment	= std::true_type;
	using propagate_on_container_move_assignment	= std::true_type;
	using propagate_on_container_swap				= std::true_type;

	ARENA_T*	pArena		= nullptr;
	T*			run			= nullptr;
	u64			runInBytes	= 0;

    ht_arena_allocator() = default;
    ht_arena_allocator( ARENA_T* pSrcArena )	: pArena{ pSrcArena } { HT_ASSERT( nullptr != pArena ); }
    ht_arena_allocator( ARENA_T& srcArena )		: pArena{ &srcArena } {}

	ht_arena_allocator( const ht_arena_allocator& other )	: pArena{ other.pArena } {}
    ht_arena_allocator( ht_arena_allocator&& other );

	template<TRIVIAL_T U>
    ht_arena_allocator( const ht_arena_allocator<U, ARENA_T>& other ) : pArena{ other.pArena } {}

	ht_arena_allocator&			operator=( const ht_arena_allocator& other );
	ht_arena_allocator&			operator=( ht_arena_allocator&& other );

	T*							allocate( u64 reqSzInElems );
	std::allocation_result<T*>	allocate_at_least( u64 reqSzInElems );
	void						deallocate( T*, u64 ) {}

	bool						operator==( const ht_arena_allocator& other ) const { return pArena == other.pArena; }
};

template<TRIVIAL_T T, arena_t ARENA_T>
ht_arena_allocator<T, ARENA_T>::ht_arena_allocator( ht_arena_allocator&& other )
	: pArena{ other.pArena }, run{ other.run }, runInBytes{ other.runInBytes }
{
	other.run			= nullptr;
	other.runInBytes	= 0;
}

template<TRIVIAL_T T, arena_t ARENA_T>
auto ht_arena_allocator<T, ARENA_T>::operator=( const ht_arena_allocator& other ) -> ht_arena_allocator&
{
	pArena		= other.pArena;
	run			= nullptr;
	runInBytes	= 0;
	return *this;
}

template<TRIVIAL_T T, arena_t ARENA_T>
auto ht_arena_allocator<T, ARENA_T>::operator=( ht_arena_allocator&& other ) -> ht_arena_allocator&
{
	pArena				= other.pArena;
	run					= other.run;
	runInBytes			= other.runInBytes;
	other.run			= nullptr;
	other.runInBytes	= 0;
	return *this;
}

template<TRIVIAL_T T, arena_t ARENA_T>
T* ht_arena_allocator<T, ARENA_T>::allocate( u64 reqSzInElems )
{
	u64 reqSzInBytes = reqSzInElems * sizeof( T );

	if( reqSzInBytes <= runInBytes ) return run;

	// NOTE: we can only grow in place while we're the arena's tail run
	if( nullptr != run )
	{
		u64 stretchedInBytes = pArena->TryStretchAlloc( { ( u8* ) run, runInBytes }, reqSzInBytes - runInBytes );
		if( ~0ull != stretchedInBytes )
		{
			runInBytes = stretchedInBytes;
			return run;
		}
	}

	run			= ( T* ) pArena->Alloc( reqSzInBytes, alignof( T ) );
	runInBytes	= reqSzInBytes;

	return run;
}

template<TRIVIAL_T T, arena_t ARENA_T>
std::allocation_result<T*> ht_arena_allocator<T, ARENA_T>::allocate_at_least( u64 reqSzInElems )
{
	T* mem = allocate( reqSzInElems );
	return { mem, runInBytes / sizeof( T ) };
}

template<TRIVIAL_T T, u64 N>
struct ht_static_storage_allocator
{
	using value_type								= T;
	using is_always_equal							= std::false_type;
	using propagate_on_container_copy_assignment	= std::false_type;
	using propagate_on_container_move_assignment	= std::false_type;
	using propagate_on_container_swap				= std::false_type;

	alignas( 8 ) std::array<T, N>	elems;

    ht_static_storage_allocator() = default;
	ht_static_storage_allocator( const ht_static_storage_allocator& ) {}
    ht_static_storage_allocator& operator=( const ht_static_storage_allocator& ) { return *this; }

    template<TRIVIAL_T U>
    ht_static_storage_allocator( const ht_static_storage_allocator<U, N>& ) {}

	T*							allocate( u64 reqSzInElems ) { HT_ASSERT( reqSzInElems <= N ); return std::data( elems ); }
	std::allocation_result<T*>	allocate_at_least( u64 reqSzInElems ) { return { allocate( reqSzInElems ), N }; }
	void						deallocate( T*, u64 ) {}

	bool						operator==( const ht_static_storage_allocator& other ) const { return this == &other; }
};

template<TRIVIAL_T T>
struct ht_borrowed_storage_allocator
{
	using value_type								= T;
	using is_always_equal							= std::false_type;
	using propagate_on_container_copy_assignment	= std::false_type;
	using propagate_on_container_move_assignment	= std::true_type;
	using propagate_on_container_swap				= std::true_type;

	std::span<T>	run = {};

    ht_borrowed_storage_allocator() = default;
    ht_borrowed_storage_allocator( std::span<T> borrowedRun ) : run{ borrowedRun } {}

	ht_borrowed_storage_allocator( const ht_borrowed_storage_allocator& ) {}
    ht_borrowed_storage_allocator( ht_borrowed_storage_allocator&& other );

    template<TRIVIAL_T U>
    ht_borrowed_storage_allocator( const ht_borrowed_storage_allocator<U>& ) {}

	ht_borrowed_storage_allocator&	operator=( const ht_borrowed_storage_allocator& ) { return *this; }
	ht_borrowed_storage_allocator&	operator=( ht_borrowed_storage_allocator&& other );

	T*							allocate( u64 reqSzInElems ) { HT_ASSERT( reqSzInElems <= std::size( run ) ); return std::data( run ); }
	std::allocation_result<T*>	allocate_at_least( u64 reqSzInElems ) { return { allocate( reqSzInElems ), std::size( run ) }; }
	void						deallocate( T*, u64 ) {}

	bool						operator==( const ht_borrowed_storage_allocator& other ) const { return std::data( run ) == std::data( other.run ); }
};

template<TRIVIAL_T T>
ht_borrowed_storage_allocator<T>::ht_borrowed_storage_allocator( ht_borrowed_storage_allocator&& other ) : run{ other.run }
{
	other.run = {};
}

template<TRIVIAL_T T>
auto ht_borrowed_storage_allocator<T>::operator=( ht_borrowed_storage_allocator&& other ) -> ht_borrowed_storage_allocator&
{
	run			= other.run;
	other.run	= {};
	return *this;
}

template<TRIVIAL_T T, arena_t ARENA_T = linear_arena>
using arena_vector = std::vector<T, ht_arena_allocator<T, ARENA_T>>;

template<TRIVIAL_T T>
using borrowed_vector = std::vector<T, ht_borrowed_storage_allocator<T>>;

template<TRIVIAL_T T, u64 N>
struct inline_vector : std::vector<T, ht_static_storage_allocator<T, N>>
{
	using base_t = std::vector<T, ht_static_storage_allocator<T, N>>;

	using base_t::base_t;
	using base_t::operator=;

    inline_vector() = default;
    inline_vector( const inline_vector& other )	: base_t{ ( const base_t& ) other } {}
    inline_vector( inline_vector&& other )		: base_t{ ( const base_t& ) other } { other.clear(); }

	inline_vector&	operator=( const inline_vector& other );
	inline_vector&	operator=( inline_vector&& other );

	void				swap( this inline_vector& self, inline_vector& other );
};

template<TRIVIAL_T T, u64 N>
auto inline_vector<T, N>::operator=( const inline_vector& other ) -> inline_vector&
{
	( base_t& )*this = ( const base_t& )other;
	return *this;
}

template<TRIVIAL_T T, u64 N>
auto inline_vector<T, N>::operator=( inline_vector&& other ) -> inline_vector&
{
	( base_t& )*this = ( const base_t& )other;
	other.clear();
	return *this;
}

template<TRIVIAL_T T, u64 N>
void inline_vector<T, N>::swap( this inline_vector& self, inline_vector& other )
{
	inline_vector tmp = MOV( self );

	self	= MOV( other );
	other	= MOV( tmp );
}

#endif // !__HT_VECTOR_H__
