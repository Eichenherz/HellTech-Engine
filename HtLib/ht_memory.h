#pragma once

#ifndef __HT_MEMORY_H__
#define __HT_MEMORY_H__

#include <memory_resource>
#include <span>

#include <System/sys_sync.h>

#include <ht_core_types.h>
#include <ht_error.h>
#include <ht_utils.h>
#include <ht_math.h>

template<u64 SZ_IN_BYTES>
struct static_arena
{
	alignas( 8 ) u8 mem[ SZ_IN_BYTES ] = {};
	u64             offset = 0;

	void	Rewind( u64 mark ) { offset = ( mark <= SZ_IN_BYTES ) ? mark : SZ_IN_BYTES; }
	void	Reset() { offset = 0; }
	void*	Alloc( u64 bytes, u64 alignment )
	{
		u64 base		= ( u64 ) mem;
		u64 alignedAddr = FwdAlignPot( base + offset, alignment );
		u64 newOffset	= ( alignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= SZ_IN_BYTES );

		offset = newOffset;
		return ( void* ) alignedAddr;
	}
};

struct dynamic_arena
{
	u8*     mem    = nullptr;
	u64     offset = 0;
	u64     size   = 0;

	        dynamic_arena() = default;
	        dynamic_arena( u8* mem, u64 size ) : mem{ mem }, size{ size } {}

	void	Rewind( u64 mark ) { offset = ( mark <= size ) ? mark : size; }
	void	Reset() { offset = 0; }
	void*	Alloc( u64 bytes, u64 alignment )
	{
		u64 base		= ( u64 ) mem;
		u64 alignedAddr = FwdAlignPot( base + offset, alignment );
		u64 newOffset	= ( alignedAddr - base ) + bytes;

		HT_ASSERT( newOffset <= size );

		offset = newOffset;
		return ( void* ) alignedAddr;
	}
};

constexpr u64 OS_PAGE_SIZE_IN_BYTES		= 4096;
// NOTE: on win user takes 0 to this and kernel takes the rest of tha range
constexpr u64 OS_USER_MAX_ADDR          = 0x00007ffffffeffff;
constexpr u64 OS_USER_ADDR_BIT_WIDTH	= std::bit_width( OS_USER_MAX_ADDR );

// NOTE: these crash on failure
void*	ht_os_virtual_reserve( u64 sizeInBytes );
// NOTE: release also frees all commited pages in that range
void	ht_os_virtual_release( void* mem );
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes );
void	ht_os_virtual_decommit( void* mem, u64 sizeInBytes );
void*   ht_os_virtual_alloc( u64 sizeInBytes );


template<typename T>
concept arena_t = requires( T a, u64 bytes, u64 alignment, u64 mark )
{
	{ a.mem }						-> std::convertible_to<u8*>;
	{ a.offset }					-> std::convertible_to<u64>;

	{ a.Alloc( bytes, alignment ) } -> std::same_as<void*>;
	{ a.Rewind( mark ) }            -> std::same_as<void>;
	{ a.Reset() }					-> std::same_as<void>;
};

template<typename T, arena_t Arena>
T* ArenaNew( Arena& arena )
{
	return new ( arena.Alloc( sizeof( T ), alignof( T ) ) ) T;
}

template<typename T, arena_t Arena>
T* ArenaNewArray( Arena& arena, u64 count )
{
	// NOTE: otherwise C++ adds 8 bytes top of our alloc
	static_assert( std::is_trivially_destructible<T>::value );
	return new ( arena.Alloc( sizeof( T ) * count, alignof( T ) ) ) T[ count ];
}

template<arena_t Arena>
struct stack_adaptor : std::pmr::memory_resource
{
	Arena&	arena;
	u64		baseFrameOffset;

			stack_adaptor( Arena& a ) : arena{ a }, baseFrameOffset{ a.offset }{}
			~stack_adaptor() override { arena.Rewind( baseFrameOffset ); }
	u8*		BasePtr() { return arena.mem + baseFrameOffset; }
protected: // NOTE: std::pmr::memory_resource's API
	void*   do_allocate( size_t bytes, size_t alignment ) override { return arena.Alloc( bytes, alignment ); }
	void	do_deallocate( void*, size_t, size_t ) override { /* no-op */ }
	bool	do_is_equal( const std::pmr::memory_resource& other ) const noexcept override { return this == &other; }
};


template<typename T, arena_t Arena>
struct arena_allocator
{
	using value_type = T;

	// NOTE: the arena is shared state so assignment must carry it over. Without POCMA a move assign
	// degrades from stealing the pointers to moving every element one by one.
	using propagate_on_container_copy_assignment	= std::true_type;
	using propagate_on_container_move_assignment	= std::true_type;
	using propagate_on_container_swap				= std::true_type;

	// NOTE: containers default construct their allocator ( it's a default arg on their ctors ), so
	// this has to survive being null until one with an arena is assigned over it
	Arena*	pArena = nullptr;

			arena_allocator() = default;
			arena_allocator( Arena& arena ) : pArena{ &arena } {}
	// NOTE: rebind ctor, containers need it to allocate their internal bucket / node types
	template<typename U>
			arena_allocator( const arena_allocator<U, Arena>& other ) : pArena{ other.pArena } {}

	T*		allocate( u64 count )
	{
		HT_ASSERT( pArena );
		return ( T* ) pArena->Alloc( count * sizeof( T ), alignof( T ) );
	}
	void	deallocate( T*, u64 ) { /* no-op, the arena frees in bulk */ }

	// NOTE: allocators are interchangeable only if one can free what the other handed out
	template<typename U>
	bool	operator==( const arena_allocator<U, Arena>& other ) const { return pArena == other.pArena; }
};

#ifndef HT_BLOCK_SZ_IN_BYTES
#define HT_BLOCK_SZ_IN_BYTES	( 64 * KB )
#endif
constexpr u64 BLOCK_SZ_IN_BYTES	= HT_BLOCK_SZ_IN_BYTES;

// NOTE: we will demand every allocation be aligned to this, like that we'll get the low 16 bits for metadata as well
constexpr u64 HT_INTERNAL_ALIGNMENT = 64 * KB;
static_assert( FwdAlignPot( BLOCK_SZ_IN_BYTES, HT_INTERNAL_ALIGNMENT ) == BLOCK_SZ_IN_BYTES );

// NOTE: we capped the max alloc at this because we can only atomically CAS up to u64 bits
constexpr u64 BLOCKS_PER_BIN				= 64;
constexpr u64 BINS_PER_CHUNK				= 8;
constexpr u64 BIN_SZ_IN_BYTES				= BLOCKS_PER_BIN * BLOCK_SZ_IN_BYTES;
constexpr u64 CHUNK_SZ_IN_BYTES 			= BINS_PER_CHUNK * BIN_SZ_IN_BYTES;
// NOTE: while we can alloc anything from 1 block to full bin, this defeats the point of the chunks.
// Basically, if we alloc a big run we're killing the benefits of the amortized os_commit. ( ie 50/64 from the same bin )
// To avoid this we will cap the max bin alloc size.
constexpr u64 MAX_BIN_ALLOC_SZ_IN_BLOCKS	= 4;
// NOTE: we enforce that our alloc handles have a `selfType` field so we can use them in the same map
// call it "poor man's bitfield polymorphism"
template<typename T>
constexpr bool HtHasSelfTypeTopBit()
{
	T h = {};
	h.selfType = 1;
	return std::bit_cast<u64>( h ) == ( 1ull << 63 );
}

template<typename T>
concept HT_GENERIC_ALLOC_HANDLE_T = ( sizeof( T ) == sizeof( u64 ) ) &&
	requires( T h ) { h.selfType = 1u; } && HtHasSelfTypeTopBit<T>();

enum ht_halloc_self_type : u64
{
	MIP_ZONE	= 0,
	VIRTUAL		= 1
};

using ht_halloc = u64; // NOTE: allocation handle
constexpr u64 HT_NULL_ALLOC_HANDLE = 0;

enum ht_virt_alloc_type : u64
{
	DEDICATED	= 0,
	BLOCK		= 1
};

constexpr u64 HT_OS_ADDR_HIGHER_FREE_BITS_COUNT	= 64 - OS_USER_ADDR_BIT_WIDTH;
constexpr u64 HT_OS_ADDR_LOWER_FREE_BITS_COUNT	= std::bit_width( HT_INTERNAL_ALIGNMENT ) - 1;
// NOTE: OS_USER_MAX_ADDR is a max value, not a mask ( it has holes ), the mask is width worth of ones
constexpr u64 HT_OS_ADDR_MASK					= ( 1ull << OS_USER_ADDR_BIT_WIDTH ) - 1;

constexpr u64 HT_META_BIT_COUNT					= HT_OS_ADDR_HIGHER_FREE_BITS_COUNT + HT_OS_ADDR_LOWER_FREE_BITS_COUNT;
constexpr u64 HT_ADDR_BIT_COUNT					= 64 - HT_META_BIT_COUNT;

struct ht_virt_alloc
{
	u64 address		: HT_ADDR_BIT_COUNT		= 0;
	u64 metadata	: HT_META_BIT_COUNT - 2 = 0;
	u64 type		: 1						= 0;
	u64	selfType	: 1						= ht_halloc_self_type::VIRTUAL;

	ht_virt_alloc() = default;
	ht_virt_alloc( void* ptr, u64 payload, ht_virt_alloc_type allocType ) :
		address	{ ( ( ( u64 ) ptr ) & HT_OS_ADDR_MASK ) >> ( HT_OS_ADDR_LOWER_FREE_BITS_COUNT ) },
		metadata{ payload },
		type	{ allocType }
	{
		HT_ASSERT( ( ( address << HT_OS_ADDR_LOWER_FREE_BITS_COUNT ) ) == ( u64 ) ptr );
		HT_ASSERT( ( payload & ( ( ( 1ull << ( HT_META_BIT_COUNT - 2 ) ) - 1 ) ) ) == payload );
	}

	// NOTE: C++20 will use this for !=
	bool operator==( ht_virt_alloc other ) const { return std::bit_cast<u64>( *this ) == std::bit_cast<u64>( other ); }
	// NOTE: use them like pointers, exclude highest bit
	operator bool() const { return 0 != ( std::bit_cast<u64>( *this ) & ( ( 1ull << 63 ) - 1 ) ); }
};

static_assert( HT_GENERIC_ALLOC_HANDLE_T<ht_virt_alloc>, "Type can't be used as an allocation handle !!!!" );

inline void* HtGetAllocPtr( ht_virt_alloc alloc )
{
	return ( void* ) ( u64( alloc.address ) << HT_OS_ADDR_LOWER_FREE_BITS_COUNT );
}

static_assert( sizeof( u64 ) == sizeof( ht_virt_alloc ) );

// NOTE: inspired by mimalloc
struct alignas( 64 ) ht_virtual_chunk
{
	// NOTE: 0 for free, 1 for committed
	atomic_u64 blockBitmap[ BINS_PER_CHUNK ];
};

struct ht_virtual_allocator
{
	void*						pMemBase		= nullptr; // NOTE: points at the start of the blocks from chunkMap
	u64							reservedInBytes	= 0;
	std::span<ht_virtual_chunk>	chunkMap		= {};

	ht_virt_alloc	AllocVirtualBlock( u64 requestSzInBytes, u64 threadIndex );
	void			FreeVirtualBlock( ht_virt_alloc alloc, u64 threadIdx );
};

ht_virtual_allocator HtMakeVirtualAllocator( u64 maxMemCapInBytes );


constexpr u64 HT_ZONE_SZ_CLASS_GCD		= 64; // NOTE: we use this to minimize the payload in the handle
constexpr u64 HT_ZONE_SZ_CLASS_COUNT	= 16;

enum class ht_zone_size_class_t : u64
{
	BYTES_192 =   192,  BYTES_256 =   256,  BYTES_384 =   384,  BYTES_512 =   512,
	BYTES_768 =   768,  BYTES_1K  =  1024,  BYTES_1K5 =  1536,  BYTES_2K  =  2048,
	BYTES_3K  =  3072,  BYTES_4K  =  4096,  BYTES_6K  =  6144,  BYTES_8K  =  8192,
	BYTES_12K = 12288,  BYTES_16K = 16384,  BYTES_24K = 24576,  BYTES_32K = 32768
};

constexpr u64 HtSizeFromClassIdx( u64 clsIdx )
{
	// k = 3 or 4, doubling every two steps
	return ( 3u + ( clsIdx & 1u ) ) << ( 6u + ( clsIdx >> 1u ) );
}

constexpr ht_zone_size_class_t HtZoneSzClassFromIdx( u64 clsIdx )
{
	return ( ht_zone_size_class_t ) HtSizeFromClassIdx( clsIdx );
}

constexpr u64 HT_THEAP_MIN_ALLOC_SZ_IN_BYTES		= 64 * KB;
constexpr u64 HT_THEAP_ZONE_MAX_BMP_SZ_IN_QWORDS	= // TODO: claude fix this, should be 6 now not 5
	( HT_THEAP_MIN_ALLOC_SZ_IN_BYTES / ( u64 ) ht_zone_size_class_t::BYTES_192 ) / 64;


struct ht_zone_alloc
{
	static constexpr u64 ZONE_IDX_BIT_COUNT		= 16;
	static constexpr u64 SZCLS_FACTOR_BIT_COUNT = 10;

	u64 baseAddr	: HT_ADDR_BIT_COUNT		= 0;
	u64 zoneIdx		: ZONE_IDX_BIT_COUNT	= 0;
	u64 szClsFactor	: SZCLS_FACTOR_BIT_COUNT= 0;
	u64 padding		: 6						= 0;
	u64 selfType	: 1						= ht_halloc_self_type::MIP_ZONE;

	ht_zone_alloc() = default;
	ht_zone_alloc( void* ptr, u64 zoneDescIdx, u64 szClsFactor ) :
		baseAddr	{ ( ( ( u64 ) ptr ) & HT_OS_ADDR_MASK ) >> ( HT_OS_ADDR_LOWER_FREE_BITS_COUNT ) },
		zoneIdx		{ zoneDescIdx },
		szClsFactor	{ szClsFactor }
	{
		HT_ASSERT( ( ( baseAddr << HT_OS_ADDR_LOWER_FREE_BITS_COUNT ) ) == ( u64 ) ptr );
		HT_ASSERT( zoneIdx == zoneDescIdx );
	}

	// NOTE: C++20 will use this for !=
	bool operator==( ht_zone_alloc other ) const { return std::bit_cast<u64>( *this ) == std::bit_cast<u64>( other ); }
	operator bool() const { return 0 != ( std::bit_cast<u64>( *this ) & ( ( 1ull << 63 ) - 1 ) ); }
};

static_assert( HT_GENERIC_ALLOC_HANDLE_T<ht_zone_alloc>, "Type can't be used as an allocation handle !!!!" );

struct alignas( 64 ) ht_szcls_zone_desc
  {
  	u64						freeBmp[ HT_THEAP_ZONE_MAX_BMP_SZ_IN_QWORDS ];
  	ht_virt_alloc			hZoneAlloc;
  	ht_zone_size_class_t	sizeClassIdx;
  };
static_assert( 64 == sizeof( ht_szcls_zone_desc ) );

#include <ht_fixed_vector.h>
#include <ht_fixed_hashmap.h>

constexpr u64 HT_THEAP_ZONE_CAP				= 8;
constexpr u64 HT_ALLOC_MAP_ALLOC_CAP		= 3'276;
static_assert( HT_ALLOC_MAP_ALLOC_CAP <= HtHashMaxLoad( HtHashBucketCount( HT_ALLOC_MAP_ALLOC_CAP ) ) );

inline void* HtGetAllocPtr( ht_halloc hAlloc )
{
	u64 selfType = std::bit_cast<u64>( hAlloc ) >> 63;
	if( ht_halloc_self_type::VIRTUAL == selfType ) return HtGetAllocPtr( std::bit_cast<ht_virt_alloc>( hAlloc ) );
	//if( ht_halloc_self_type::MIP_ZONE == selfType ) return HtGetAllocPtr( std::bit_cast<ht_zone_alloc>( hAlloc ) );

	HT_ASSERT( false );
	return nullptr;
}

struct ht_halloc_hash
{
	// NOTE: splitmix64 is already a finalizer, this tells ankerl to skip its own wyhash pass
	using is_avalanching = void;
	// NOTE: this is what unlocks find/erase by raw ptr, ankerl gates its hetero overloads on it
	using is_transparent = void;

	u64 operator()( ht_halloc h ) const { return SplitmixHash64( ( u64 ) HtGetAllocPtr( h ) ); }
	u64 operator()( const void* p ) const { return SplitmixHash64( ( u64 ) p ); }
};
struct ht_halloc_eq
{
	using is_transparent = void;

	// NOTE: ankerl always calls us as ( lookupKey, storedKey ), never flipped
	// TODO: make sure that we can use typed handles with same addr/different type ?
	bool operator()( ht_halloc a, ht_halloc b ) const   { return a == b; }
	bool operator()( const void* p, ht_halloc h ) const { return p == HtGetAllocPtr( h ); }
};

// NOTE: for now we max cap these and alloc once at init if we run out of space we crash; this is intended

struct alignas( 64 ) ht_thread_heap
{
	using ht_fixed_hashset_t	= ht_fixed_hashset<ht_halloc, ht_halloc_hash, ht_halloc_eq, HT_ALLOC_MAP_ALLOC_CAP>;

	//ht_zone_list_t		mipZoneList	= {};
	ht_fixed_hashset_t	allocMap	= {};

	// NOTE: these are allocated as an array so we can get the idx from ptr math
	u64					OwningThreadIdx() const;
	std::span<u8>		Allocate( u64 szInBytes );
	void				Free( void* ptr );
};

// NOTE: public allocator api
ht_thread_heap *const GetThreadHeap( u64 threadIdx );
void HtMakeAllocator( u64 maxMemSz, u64 threadCount );


#endif // !__HT_MEMORY_H__
