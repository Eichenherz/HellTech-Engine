#pragma once

#ifndef __SYS_FILE_H__
#define __SYS_FILE_H__

#include <ht_core_types.h>
#include <ht_mem_arena.h>
#include <ht_fixed_string.h>
#include <span>

#include <System/sys_consts.h>

std::span<u8> SysReadFileBinary( const char* path, linear_arena& arena );

enum file_perm_bits : u64
{
	READ = 1,
	WRITE = 1 << 1
};

using file_perm_flags = u64;

enum class file_create_flags : u64
{
	CREATE,
	OPEN_IF_EXISTS,
	OVERWRITE
};

enum class file_access_flags : u64
{
	SEQUENTIAL,
	RANDOM,
	CONCURRENT
};

struct mmap_file
{
	using iterator          = u8*;
	using const_iterator    = const u8*;

	u64				hFile			= ~u64{};
	u64				hFileMapping	= ~u64{};
	std::span<u8>	dataView		= {};


	 iterator		begin()			{ return data(); }
	 iterator		end()			{ return data() + size(); }

	 const_iterator	cbegin() const	{ return data(); }
	 const_iterator	cend()   const	{ return data() + size(); }

	 u64			size() const	{ return std::size( dataView ); }
	 u8*			data()			{ return std::data( dataView ); }
	 const u8*		data() const	{ return std::data( dataView ); }

	u64				Timestamp() const;
};

mmap_file SysCreateMmapFile(
	const char*				path,
	file_perm_flags	permissionFlags,
	file_create_flags		createFlags,
	file_access_flags		accessFlags
);

void SysDestroyMmapFile( mmap_file* mmapFile );

u64 ht_os_create_file(
    const char*				filePath,
    file_perm_flags	permissionFlags,
    file_create_flags		createFlags,
    file_access_flags		accessFlags
);

// NOTE: hFile must have been opened with file_access_flags::CONCURRENT, else the kernel serializes per handle
void SysWriteFileConcurrentBlocking( u64 hFile, u64 offsetInBytes, std::span<const u8> bytes );

using sys_path = fixed_string<SYS_MAX_PATH_LEN>;

constexpr std::string_view SysPathFileName( std::string_view path )
{
	return path.substr( path.find_last_of( "\\/" ) + 1 );
}

constexpr std::string_view SysPathExt( std::string_view path )
{
	std::string_view name = SysPathFileName( path );
	u64 dot = name.find_last_of( '.' );
	return ( std::string_view::npos == dot ) ? std::string_view{} : name.substr( dot );
}

constexpr std::string_view SysPathStem( std::string_view path )
{
	std::string_view name = SysPathFileName( path );
	return name.substr( 0, name.find_last_of( '.' ) );
}

#endif // !__SYS_FILE_H__