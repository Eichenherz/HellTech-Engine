#pragma once

#ifndef __ZIP_PACK_H__
#define __ZIP_PACK_H__

#include "core_types.h"
#include "ht_error.h"
#include "ht_fixed_string.h"

#include <miniz.h>

#include <iostream>
#include <array>
#include <span>
#include <cstdlib>

#include <ankerl/unordered_dense.h>

#define MINIZ_ERR( pZa )										\
do{																\
    mz_zip_error mzErr = mz_zip_get_last_error( pZa );			\
	const char* strErr = mz_zip_get_error_string( mzErr );		\
	HtPrintErrAndDie( "{} failed: {}\n", __func__, strErr );	\
}while( 0 )                                                       


struct zip_writer
{
	ankerl::unordered_dense::set<vfs_path> files;
	mz_zip_archive za = {};

	zip_writer( const char* physicalPath )
	{
		mz_zip_zero_struct( &za );

		if( !mz_zip_writer_init_file( &za, physicalPath, 0 /* reserve_at_beginning */ ) )
		{
			MINIZ_ERR( &za );
		}
	}

	zip_writer( const zip_writer& ) = delete;
	zip_writer& operator=( const zip_writer& ) = delete;

	~zip_writer()
	{
		if( !mz_zip_writer_finalize_archive( &za ) )
		{
			MINIZ_ERR( &za );
		}

		if( !mz_zip_writer_end( &za ) )
		{
			MINIZ_ERR( &za );
		}
		mz_zip_zero_struct( &za );
		files.clear();
	}

	inline bool FileExists( const vfs_path& filepath )
	{
		return std::cend( files ) != files.find( filepath );
	}

	bool MkDir( const char* dirPathStr )
	{
		if( mz_zip_writer_add_mem( &za, dirPathStr, nullptr, 0, MZ_NO_COMPRESSION ) ) return true;

		MINIZ_ERR( &za );
		return false;
	}

	bool WriteBytesToFile( const vfs_path& filepath, std::span<const u8> bytes )
	{
		HT_ASSERT( !FileExists( filepath ) );
		files.emplace( filepath );

		// NOTE: miniz flushes the writes immediatley 
		if( mz_zip_writer_add_mem( &za, std::data( filepath ), std::data( bytes ), std::size( bytes ), MZ_NO_COMPRESSION ) )
		{
			return true;
		}

		MINIZ_ERR( &za );
		return false;
	}
};

// NOTE: not thread safe !
struct vfs_zip_mem
{
	mutable ankerl::unordered_dense::map<vfs_path, u32> files;
	std::span<const u8> archiveBytesView; 
	mutable mz_zip_archive za = {};


	vfs_zip_mem( std::span<const u8> _archiveBytesView ) : archiveBytesView{ _archiveBytesView }
	{
		HT_ASSERT( std::size( archiveBytesView ) != 0 );

		mz_zip_zero_struct( &za );

		if( !mz_zip_reader_init_mem( &za, std::data( archiveBytesView ), std::size( archiveBytesView ), 0 ) )
		{
			MINIZ_ERR( &za );
			std::abort();
		}

		u32 numFiles = mz_zip_reader_get_num_files( &za );

		for( u32 fi = 0; fi < numFiles; ++fi )
		{
			mz_zip_archive_file_stat st = {};
			if( !mz_zip_reader_file_stat( &za, fi, &st ) ) continue;

			// NOTE: we don't support compressed files
			HT_ASSERT( st.m_comp_size == st.m_uncomp_size );
			HT_ASSERT( !st.m_is_encrypted );

			if( st.m_is_directory ) continue; // NOTE: dc about explicitly caching dirs

			vfs_path path = { st.m_filename };
			files.emplace( path, fi );
		}
	}

	vfs_zip_mem( const vfs_zip_mem& ) = delete;
	vfs_zip_mem& operator=( const vfs_zip_mem& ) = delete;

	~vfs_zip_mem()
	{
		mz_zip_reader_end( &za );
	}

	inline bool FileExists( const vfs_path& filepath ) const
	{
		return std::cend( files ) != files.find( filepath );
	}

	u64 GetFileSizeInBytes( const vfs_path& filepath ) const
	{
		if( !FileExists( filepath ) ) return {};

		// NOTE: this is fine, won't insert empty bc we already checked
		u32 minizEntryIdx = files[ filepath ];

		mz_zip_archive_file_stat st = {};
		HT_ASSERT( mz_zip_reader_file_stat( &za, minizEntryIdx, &st ) );
		HT_ASSERT( std::strcmp( st.m_filename, std::data( filepath ) ) == 0 );
		HT_ASSERT( st.m_comp_size == st.m_uncomp_size );
		HT_ASSERT( !st.m_is_encrypted );

		return st.m_uncomp_size;
	}

	// NOTE: this is really a hack, so in the future we need our own pak file
	std::span<const u8> ZipGetFileView( const void* base, u32 offset ) const
	{
	#pragma pack( push, 1 )
		struct zip_local_header 
		{
			u32 signature;    // 0x04034b50
			u16 version;
			u16 flags;
			u16 compression;  // 0 == "stored"
			u16 modTime;
			u16 modDate;
			u32 crc32;
			u32 compressedSize;
			u32 uncompressedSize;
			u16 filenameLen;
			u16 extraLen;
		};
	#pragma pack( pop )

		const zip_local_header* h = ( const zip_local_header* ) ( ( u8* ) base + offset );
		HT_ASSERT( 0x04034b50 == h->signature );
		HT_ASSERT( 0 == h->compression );
		HT_ASSERT( h->uncompressedSize == h->compressedSize );

		return { ( u8* ) h + sizeof( zip_local_header ) + h->filenameLen + h->extraLen, h->uncompressedSize };
	}

	// NOTE: user must query the size first
	std::span<const u8> GetFileByteView( const vfs_path& filepath ) const
	{
		if( !FileExists( filepath ) ) return {};

		// NOTE: this is fine, won't insert empty bc we already checked
		u32 minizEntryIdx = files[ filepath ];

		mz_zip_archive_file_stat st = {};
		if( mz_zip_reader_file_stat( &za, minizEntryIdx, &st ) == 0 ) return {};
		HT_ASSERT( std::strcmp( st.m_filename, std::data( filepath ) ) == 0 );
		HT_ASSERT( !st.m_is_directory );
		HT_ASSERT( st.m_comp_size == st.m_uncomp_size );
		HT_ASSERT( !st.m_is_encrypted );
		
		return ZipGetFileView( std::data( archiveBytesView ), st.m_local_header_ofs );
	}
};

#endif // !__ZIP_PACK_H__

