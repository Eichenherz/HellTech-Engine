#pragma once

#ifndef SYS_FILE_H
#define SYS_FILE_H

#include <ht_core_types.h>
#include <ht_mem_arena.h>
#include <ht_fixed_string.h>
#include <span>

#include <System/sys_consts.h>

constexpr u64 RAW_DISK_SECTOR_ALIGNMENT = 4 << 10;

// NOTE: this is helpful for ht_array/ cursors
struct alignas( RAW_DISK_SECTOR_ALIGNMENT ) ht_disk_sector { u8 bytes[ RAW_DISK_SECTOR_ALIGNMENT ]; };

enum class file_permission_t : u64
{
	READ = 1,
	WRITE = 1 << 1,
	DELETABLE = 1 << 2
};

constexpr file_permission_t operator|( file_permission_t a, file_permission_t b )
{
    return file_permission_t( ( u64 ) a | ( u64 ) b );
}
constexpr bool operator&( file_permission_t a, file_permission_t b ) { return 0 != ( ( u64 ) a & ( u64 ) b ); }

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
	RAW_CONCURRENT
};

void* ht_os_create_file(
    const char*		    filePath,
    file_permission_t   permissionFlags,
    file_create_t	    createFlags,
    file_access_t	    accessFlags
);
void ht_os_close_file( void* hFile );
void ht_delete_file_by_handle( void* hFile );

std::span<const u8> HtOsCreateROFileMapping( void* hFile );
void HtOSUnmapView( std::span<const u8> view );

// NOTE: hFile must have been opened with file_access_flags::CONCURRENT, else the kernel serializes per handle
void SysFileWriteConcurrentBlocking( void* hFile, u64 offsetInBytes, std::span<const u8> bytes );

// NOTE: raw reads need to be sector aligned;
// ioReadBuff must have enough space to hold the sec aligned data, but we only expose the requested data
std::span<u8> SysFileRawReadNBytesBlocking( void* hFile, u64 fileOffsetInBytes, u64 readSzInBytes, std::span<u8> ioReadBuff );

std::span<u8> SysReadWholeFile( const char* path, arena_t auto& arena );



void* SysMakeIoRing( u64 maxRequests, void* hFile, std::span<u8> readBuffer );

bool SysTryPushIoRingRequest(
    void*   hIORing,
    u64     readBuffIdx,
    u32     readBuffOffset,
    u64     fileOffset,
    u32     readSize,
    void*   userData );

// NOTE: this is intended for one off/rare stuff, using an unregistered buffer is costly
bool SysTryPushIoRingRequestWRawBuffer( void* hIORing, void* rawBuffer, u64 fileOffset, u32 readSize, void* userData );

u64 SysIoRingSubmit( void* hIORing, u64 waitInMs = 1 );

bool SysIoRingPollCompletion( void *__restrict hIORing, void **__restrict pUserDataOut );



using sys_path = fixed_string<SYS_MAX_PATH_LEN>;

u64	SysFileGetTimestamp( void* hFile );
u64 SysFileGetSizeInBytes( void* hFile );

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

HT_FORCEINLINE auto SysAlignReadToDiskSector( u64 fileOffsetInBytes, u64 readSzInBytes )
{
    u64 readBeg = BwdAlignPot( fileOffsetInBytes, RAW_DISK_SECTOR_ALIGNMENT );
    u64 readEnd = FwdAlignPot( fileOffsetInBytes + readSzInBytes, RAW_DISK_SECTOR_ALIGNMENT );

    return std::tuple{ readBeg, readEnd - readBeg };
}

#endif // !SYS_FILE_H