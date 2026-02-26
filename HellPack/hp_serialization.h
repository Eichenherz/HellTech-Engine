#ifndef __HP_SERIALIZATION_H__
#define __HP_SERIALIZATION_H__

#include "core_types.h"
#include "hp_error.h"
#include "range_utils.h"

#include "hell_pack.h"

#include <span>

// NOTE: takes const lvalues and rejects temporaries ( prevents dangling views )
struct hellpack_serializble_buffer
{
	const byte_view data;
	u64 allignmentInBytes;

	template<std::ranges::contiguous_range R>
	inline hellpack_serializble_buffer( const R& r )
		: data{ MakeByteView( r ) }, allignmentInBytes{ alignof( std::ranges::range_value_t<R> ) } {}

	template <std::ranges::contiguous_range R>
	hellpack_serializble_buffer( const R&& ) = delete;
};

using hellpack_blob = std::vector<u8>;

inline hellpack_blob HpkMakeBinaryBlob( std::span<const hellpack_serializble_buffer> buffs, hellpack_entry_t type )
{
	const u32 entriesCount = std::size( buffs );

	hellpack_file_header h = {
		.magic = HELLPACK_MAGIC,
		.version = HELLPACK_VERSION,
		.entriesCount = entriesCount,
		.type = type
	};

	std::vector<hellpack_data_ref> entryTable;
	entryTable.reserve( entriesCount );
	
	u64 entryTableSizeInBytes = entriesCount * sizeof( hellpack_data_ref );
	u64 entryTableOffsetInBytes = AlignUp( sizeof( h ), alignof( hellpack_data_ref ) );

	u64 cursor = entryTableOffsetInBytes + entryTableSizeInBytes;

	for( const hellpack_serializble_buffer& hpkBuff : buffs ) 
	{
		u64 thisBuffSizeInBytes = std::size( hpkBuff.data );

		HP_ASSERT( ( thisBuffSizeInBytes < u64( u32( -1 ) ) ) && ( cursor < u64( u32( -1 ) ) ) );

		cursor = AlignUp( cursor, hpkBuff.allignmentInBytes );
		entryTable.push_back( { .offsetInBytes = cursor, .sizeInBytes = thisBuffSizeInBytes } );

		cursor += thisBuffSizeInBytes;
	}

	h.fileSizeBytes = cursor;

	// NOTE: pad to zero automatically
	hellpack_blob blob( h.fileSizeBytes, 0 );

	std::memcpy( std::data( blob ), &h, sizeof( h ) );
	std::memcpy( std::data( blob ) + entryTableOffsetInBytes, std::data( entryTable ), entryTableSizeInBytes );

	for( u64 ei = 0; ei < entriesCount; ++ei )
	{
		const auto[ offset, size ] = entryTable[ ei ];
		// NOTE: even if we can handle size == 0, there might be an external issue that results in a 0 sized buffer
		HP_ASSERT( 0 != size );

		HP_ASSERT( offset + size <= std::size( blob ) );
		std::memcpy( std::data( blob ) + offset, std::data( buffs[ ei ].data ), size );
	}

	return blob;
}

#endif // !__HP_SERIALIZATION_H__
