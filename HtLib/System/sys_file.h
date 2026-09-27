#pragma once

#ifndef __SYS_FILE_H__
#define __SYS_FILE_H__

#include <ht_core_types.h>
#include <ht_mem_arena.h>
#include <ht_fixed_string.h>
#include <span>

#include <System/sys_consts.h>

#include <ht_array.h>

std::span<u8> SysReadFileBinary( const char* path, linear_arena& arena );

enum class file_perm_t : u64
{
	READ = 1,
	WRITE = 1 << 1
};

constexpr file_perm_t operator|( file_perm_t a, file_perm_t b )
{
    return file_perm_t( ( u64 ) a | ( u64 ) b );
}
constexpr bool operator&( file_perm_t a, file_perm_t b ) { return 0 != ( ( u64 ) a & ( u64 ) b ); }

enum class file_create_t : u64
{
	CREATE,
	OPEN_IF_EXISTS,
	OVERWRITE
};

enum class file_access_t : u64
{
	SEQUENTIAL,
	RANDOM,
	CONCURRENT,
	CONCURRENT_UNBUFFERED
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
	file_perm_t		permissionFlags,
	file_create_t		createFlags,
	file_access_t		accessFlags
);

void SysDestroyMmapFile( mmap_file* mmapFile );

constexpr u64 OS_MAX_ASYNC_IO_REQS_IN_FLIGHT    = 16;
constexpr u64 OS_UNBUFFERED_IO_MEM_ALIGNMENT    = 4 * KB;
constexpr u64 OS_MAX_TRANSFER_LEN_IN_BYTES      = 1 * MB;

void* ht_os_create_file(
    const char*		filePath,
    file_perm_t	    permissionFlags,
    file_create_t	createFlags,
    file_access_t	accessFlags,
    void*           hCompletionPort = nullptr
);

std::span<const u8> HtOsCreateROFileMapping( void* hFile );
void HtOSUnmapView( std::span<const u8> view );

void* HtOsCreateIOCompletionPort( u64 maxWorkerThreads );

// NOTE: hFile must have been opened with file_access_flags::CONCURRENT, else the kernel serializes per handle
void SysWriteFileConcurrentBlocking( void* hFile, u64 offsetInBytes, std::span<const u8> bytes );

struct HT_CACHE_ALIGN ht_os_io_request
{
    alignas( HT_CACHE_LINE_SZ ) u8 opaque[ HT_CACHE_LINE_SZ ] = {};
};

void SysReadFileAsyncUnbuffered( void* hFile, u64 offsetInBytes, std::span<u8> outBytes, ht_os_io_request* pIOReq );

struct ht_os_io_completion
{
    ht_os_io_request*   pReq;
    u64                 numBytesTransferred;
};

using ht_io_comp_array = inline_array<ht_os_io_completion, OS_MAX_ASYNC_IO_REQS_IN_FLIGHT>;
ht_io_comp_array SysPollIOCompletionsStatus( void* hPort, u64 waitInMilliSecs );

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