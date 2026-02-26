#pragma once

#ifndef __ZIP_PACK_H__
#define __ZIP_PACK_H__

#include "core_types.h"
#include "hp_error.h"

#include <miniz.h>

#include <iostream>
#include <array>
#include <span>
#include <cstdlib>

#include <ankerl/unordered_dense.h>

#define MINIZ_ERR( pZa ) \
	const char* s = mz_zip_get_error_string( mz_zip_get_last_error( pZa ) ); \
	std::cout << std::format( "{} failed: {}\n", __func__, s );

using fixed_str = std::array<char, 256>;

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
			std::abort();
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
		HP_ASSERT( !FileExists( filepath ) );
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


struct vfs_zip_mem
{
	ankerl::unordered_dense::map<vfs_path, u32> files;
	std::span<const u8> archiveBytesView; 
	mz_zip_archive za = {};


	vfs_zip_mem( std::span<const u8> _archiveBytesView ) : archiveBytesView{ _archiveBytesView }
	{
		HP_ASSERT( std::size( archiveBytesView ) != 0 );

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
			HP_ASSERT( st.m_comp_size == st.m_uncomp_size );
			HP_ASSERT( !st.m_is_encrypted );

			if( st.m_is_directory ) continue; // NOTE: dc about explicitly caching dirs

			vfs_path path = {};

			u32 pathNameLen = std::strlen( st.m_filename );
			HP_ASSERT( pathNameLen < std::size( path ) );
			std::memcpy( std::data( path ), st.m_filename, pathNameLen );

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

	u64 GetFileSizeInBytes( const vfs_path& filepath )
	{
		if( !FileExists( filepath ) ) return {};

		// NOTE: this is fine, won't insert empty bc we already checked
		u32 minizEntryIdx = files[ filepath ];

		mz_zip_archive_file_stat st = {};
		HP_ASSERT( mz_zip_reader_file_stat( &za, minizEntryIdx, &st ) );
		HP_ASSERT( std::strcmp( st.m_filename, std::data( filepath ) ) == 0 );
		HP_ASSERT( st.m_comp_size == st.m_uncomp_size );
		HP_ASSERT( !st.m_is_encrypted );

		return st.m_uncomp_size;
	}

	// NOTE: user must query the size first
	bool ReadFileToBufferNoAlloc( const vfs_path& filepath, void* pDst, u64 dstSize )
	{
		if( !FileExists( filepath ) ) return false;

		// NOTE: this is fine, won't insert empty bc we already checked
		u32 minizEntryIdx = files[ filepath ];

		mz_zip_archive_file_stat st = {};
		if( mz_zip_reader_file_stat( &za, minizEntryIdx, &st ) == 0 ) return false;
		HP_ASSERT( std::strcmp( st.m_filename, std::data( filepath ) ) == 0 );
		HP_ASSERT( !st.m_is_directory );
		HP_ASSERT( st.m_comp_size == st.m_uncomp_size );
		HP_ASSERT( !st.m_is_encrypted );
		HP_ASSERT( dstSize == st.m_uncomp_size );

		HP_ASSERT( pDst );

		u8 scratchBuff[ MZ_ZIP_MAX_IO_BUF_SIZE ] = {};
		return mz_zip_reader_extract_to_mem_no_alloc( &za, minizEntryIdx, pDst, dstSize, 0, 
			scratchBuff, std::size( scratchBuff ) );
	}
};

#endif // !__ZIP_PACK_H__

