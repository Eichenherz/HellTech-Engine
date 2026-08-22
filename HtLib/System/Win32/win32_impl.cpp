#include <System/sys_sync.h>
#include <ht_memory.h>
#include <System/sys_file.h>
#include <System/sys_thread.h>

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_err.h"

// ===============================================================================================================
// ht_mem_arena.h
// ===============================================================================================================

void*	ht_os_virtual_reserve( u64 sizeInBytes )
{
	void* mem = VirtualAlloc( nullptr, sizeInBytes, MEM_RESERVE, PAGE_READWRITE );
	WIN_CHECK( mem );
	return mem;
}
void	ht_os_virtual_release( void* mem ) { WIN_CHECK( VirtualFree( mem, 0, MEM_RELEASE ) ); }
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes )
{
	u64 alignedSize = ( ( sizeInBytes + OS_PAGE_SIZE_IN_BYTES - 1 ) / OS_PAGE_SIZE_IN_BYTES ) * OS_PAGE_SIZE_IN_BYTES;

	void* newBase = VirtualAlloc( mem, alignedSize, MEM_COMMIT, PAGE_READWRITE );
	WIN_CHECK( newBase );
	return newBase;
}
void	ht_os_virtual_decommit( void* mem, u64 sizeInBytes )
{
	WIN_CHECK( VirtualFree( mem, sizeInBytes, MEM_DECOMMIT ) );
}

void*   ht_os_virtual_alloc( u64 sizeInBytes )
{
	void* mem = VirtualAlloc( nullptr, sizeInBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
	WIN_CHECK( mem );
	return mem;
}

// ===============================================================================================================

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

using win32_atomic64 = volatile __int64;

template<sys_split_barrier_t BARRIER>
u64 SysAtomicCas64( atomic_u64* pAddr, u64 exchange, u64 comparand )
{
	if constexpr( sys_split_barrier_t::NO_FENCE == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchangeNoFence64( ( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}
	else if constexpr( sys_split_barrier_t::ACQUIRE == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchangeAcquire64( ( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}
	else if constexpr( sys_split_barrier_t::RELEASE == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchangeRelease64( ( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}

	return ~0ull;
}

template<sys_split_barrier_t BARRIER>
u64 SysAtomicAnd64( atomic_u64* pAddr, u64 mask )
{
	if constexpr( sys_split_barrier_t::NO_FENCE == BARRIER )
	{
		return ( u64 ) InterlockedAnd64NoFence( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}
	else if constexpr( sys_split_barrier_t::ACQUIRE == BARRIER )
	{
		return ( u64 ) InterlockedAnd64Acquire( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}
	else if constexpr( sys_split_barrier_t::RELEASE == BARRIER )
	{
		return ( u64 ) InterlockedAnd64Release( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}

	return ~0ull;
}

// NOTE: defined here, so every barrier caller in another TU can ask for has to be instantiated here
template u64 SysAtomicCas64<sys_split_barrier_t::NO_FENCE>( atomic_u64*, u64, u64 );
template u64 SysAtomicCas64<sys_split_barrier_t::ACQUIRE>( atomic_u64*, u64, u64 );
template u64 SysAtomicCas64<sys_split_barrier_t::RELEASE>( atomic_u64*, u64, u64 );
template u64 SysAtomicAnd64<sys_split_barrier_t::NO_FENCE>( atomic_u64*, u64 );
template u64 SysAtomicAnd64<sys_split_barrier_t::ACQUIRE>( atomic_u64*, u64 );
template u64 SysAtomicAnd64<sys_split_barrier_t::RELEASE>( atomic_u64*, u64 );

sys_semaphore::sys_semaphore() : hndl{ ( u64 ) CreateSemaphoreW( NULL, 0, LONG_MAX, NULL ) }
{
	WIN_CHECK( NULL != ( HANDLE ) hndl );
}

u32 SysSemaphoreRelease( sys_semaphore sema, u32 releaseVal )
{
	u32 prevCount = 0;
	WIN_CHECK( ReleaseSemaphore( ( HANDLE ) sema.hndl, releaseVal, ( LPLONG ) &prevCount ) );
	return prevCount;
}
void SysSemaphoreWait( sys_semaphore sema, u32 millisecs )
{
	// TODO: might wanna do more stuff based on retval
	WIN_CHECK( WAIT_FAILED != WaitForSingleObject( ( HANDLE ) sema.hndl, millisecs ) );
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
	case CREATE:			return CREATE_NEW;
	case OPEN_IF_EXISTS:	return OPEN_EXISTING;
	}
	HT_ASSERT( 0 && "Wrong flags" );
	return 0;
}

constexpr DWORD MakeAccessFlags( file_access_flags accessFlags )
{
	using enum file_access_flags;
	switch( accessFlags )
	{
	case SEQUENTIAL:	return FILE_FLAG_SEQUENTIAL_SCAN;
	case RANDOM:		return FILE_FLAG_RANDOM_ACCESS;
	}

	HT_ASSERT( 0 && "Wrong falgs" );
	return 0;
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
