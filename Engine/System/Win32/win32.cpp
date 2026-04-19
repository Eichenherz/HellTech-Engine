#include <System/sys_sync.h>
#include <ht_mem_arena.h>
#include <System/sys_file.h>
#include <System/sys_thread.h>

#include "Win32/DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "Win32/win32_err.h"

// ===============================================================================================================
// sys_sync.h
// ===============================================================================================================
static_assert( sizeof( SRWLOCK ) == sizeof( void* ), "SRWLOCK storage size mismatch" );

copyable_srwlock::copyable_srwlock()                             { *( SRWLOCK* )( &osLock ) = SRWLOCK_INIT; };
copyable_srwlock::copyable_srwlock( const copyable_srwlock& )    { *( SRWLOCK* )( &osLock ) = SRWLOCK_INIT; }
copyable_srwlock::copyable_srwlock( copyable_srwlock&& )         { *( SRWLOCK* )( &osLock ) = SRWLOCK_INIT; }

copyable_srwlock& copyable_srwlock::operator=( const copyable_srwlock& ) { return *this; }
copyable_srwlock& copyable_srwlock::operator=( copyable_srwlock&& )      { return *this; }

void copyable_srwlock::lock()   const { AcquireSRWLockExclusive( ( SRWLOCK* ) ( &osLock ) ); }
void copyable_srwlock::unlock() const { ReleaseSRWLockExclusive( ( SRWLOCK* ) ( &osLock ) ); }

void SysAtomicSignalSingleThread( ht_atomic64& signal, sys_thread_signal val )
{
	InterlockedExchange64( &signal, val );
	WakeByAddressSingle( ( void* ) &signal );
}

i64 SysAtomicWaitOnAddr( ht_atomic64& signal, void* compareAddr, u32 millisecs )
{
	WaitOnAddress( &signal, compareAddr, sizeof( signal ), millisecs );
	return InterlockedAddAcquire64( &signal, 0 );
}
// ===============================================================================================================

// ===============================================================================================================
// sys_file.h
// ===============================================================================================================
constexpr DWORD MakeGenericAccessFlags( file_permissions_flags openFlags )
{
	DWORD access = 0;

	if( openFlags & file_permissions_bits::READ )
	{
		access |= GENERIC_READ;
	}

	if( openFlags & file_permissions_bits::WRITE )
	{
		access |= GENERIC_WRITE;
	}

	if( ( openFlags & file_permissions_bits::READ ) && ( openFlags & file_permissions_bits::WRITE ) )
	{
		access = GENERIC_ALL;
	}

	return access;
}
constexpr DWORD MakeFileMappingFlags( file_permissions_flags openFlags )
{
	if( ( openFlags & file_permissions_bits::READ ) && ( openFlags & file_permissions_bits::WRITE ) )
	{
		return PAGE_READWRITE;
	}
	else if( openFlags & file_permissions_bits::READ )
	{
		return PAGE_READONLY;
	}
	else if( openFlags & file_permissions_bits::WRITE )
	{
		return PAGE_WRITECOPY;
	}

	return 0;
}
constexpr DWORD MakeMapViewFlags( file_permissions_flags openFlags )
{
	if( ( openFlags & file_permissions_bits::READ ) && ( openFlags & file_permissions_bits::WRITE ) )
	{
		return FILE_MAP_ALL_ACCESS;
	}
	else if( openFlags & file_permissions_bits::READ )
	{
		return FILE_MAP_READ;
	}
	else if( openFlags & file_permissions_bits::WRITE )
	{
		return FILE_MAP_WRITE;
	}

	return 0;
}

constexpr DWORD MakeCreateFlags( file_create_flags createFlags )
{
	using enum file_create_flags;
	switch( createFlags )
	{
	case CREATE: return CREATE_NEW;
	case OPEN_IF_EXISTS: return OPEN_EXISTING;
	default: HT_ASSERT( 0 && "Wrong flags" ); return 0;
	}
}

constexpr DWORD MakeAccessFlags( file_access_flags accessFlags )
{
	using enum file_access_flags;
	switch( accessFlags )
	{
	case SEQUENTIAL: return FILE_FLAG_SEQUENTIAL_SCAN;
	case RANDOM: return FILE_FLAG_RANDOM_ACCESS;
	default: HT_ASSERT( 0 && "Wrong falgs" ); return 0;
	}
}

u64 mmap_file::Timestamp()
{
	FILETIME fileTime = {};
	WIN_CHECK( !GetFileTime( ( HANDLE ) hFile, 0, 0, &fileTime ) );

	ULARGE_INTEGER timestamp = {};
	timestamp.LowPart = fileTime.dwLowDateTime;
	timestamp.HighPart = fileTime.dwHighDateTime;

	return u64( timestamp.QuadPart );
}

mmap_file SysCreateMmapFile(
	const char*				path,
	file_permissions_flags	permissionFlags,
	file_create_flags		createFlags,
	file_access_flags		accessFlags
) {
	DWORD dwPermissionFlags		= MakeGenericAccessFlags( permissionFlags );
	DWORD dwCreateFlags			= MakeCreateFlags( createFlags );
	DWORD dwAccessFlags			= MakeAccessFlags( accessFlags );
	DWORD dwFileMappingAccess	= MakeFileMappingFlags( permissionFlags );
	DWORD dwDataViewAccess		= MakeMapViewFlags( permissionFlags );

	HANDLE hFile = CreateFileA( path, dwPermissionFlags, FILE_SHARE_READ, 0, dwCreateFlags, dwAccessFlags, NULL );
	WIN_CHECK( INVALID_HANDLE_VALUE != hFile );

	HANDLE hFileMapping = CreateFileMappingA( hFile, 0, dwFileMappingAccess, 0, 0, 0 );
	WIN_CHECK( INVALID_HANDLE_VALUE != hFileMapping );

	DWORD dwFileSizeHigh;
	u64 qwFileSize = GetFileSize( hFile, &dwFileSizeHigh );
	qwFileSize += u64( dwFileSizeHigh ) << 32;
	WIN_CHECK( 0 != qwFileSize );

	u8* pData = ( u8* ) MapViewOfFile( hFileMapping, dwDataViewAccess, 0, 0, qwFileSize );
	WIN_CHECK( 0 != pData );

	return {
		.hFile			= ( u64 ) hFile,
		.hFileMapping	= ( u64 ) hFileMapping,
		.dataView		= { pData, qwFileSize }
	};
}

void SysDestroyMmapFile( mmap_file* mmapFile )
{
	if( mmapFile )
	{
		UnmapViewOfFile( std::data( mmapFile->dataView ) );
		CloseHandle( ( HANDLE ) mmapFile->hFileMapping );
		CloseHandle( ( HANDLE ) mmapFile->hFile );
		mmapFile = nullptr;
	}
}
// ===============================================================================================================

// ===============================================================================================================
// sys_thread.h
// ===============================================================================================================
void SysNameThread( u64 hThread, const wchar_t* name )
{
	HT_ASSERT( SUCCEEDED( SetThreadDescription( ( HANDLE ) hThread, name ) ) );
}
// ===============================================================================================================
