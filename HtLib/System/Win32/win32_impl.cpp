// NOTE: tell the compiler to run our STATIC init before the rest of the program ( CRT still runs first )
#pragma init_seg( lib )

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include <ht_error.h>
#include "win32_err.h"


static LONG WINAPI WinExceptionHandler( EXCEPTION_POINTERS* pException )
{
    constexpr u32 NTSTATUS_SEVERITY_ERROR   = 0xC0000000;
    constexpr u32 NTSTATUS_CUSTOMER_BIT     = 0x20000000;
    constexpr u32 NTSTATUS_RESERVED_BIT     = 0x10000000;
    constexpr u32 NTSTATUS_CLASS_MASK       = NTSTATUS_SEVERITY_ERROR | NTSTATUS_CUSTOMER_BIT | NTSTATUS_RESERVED_BIT;

    static constexpr char EXCEPTION_FORMAT_STR[] ="SEH {:#010x} at {:#018x}: {:#018x} {:#018x}\n";

    const EXCEPTION_RECORD* pRecord = pException->ExceptionRecord;
    // NOTE: for C++ try catch or other things that we're not interested in
    if( NTSTATUS_SEVERITY_ERROR != ( pRecord->ExceptionCode & NTSTATUS_CLASS_MASK ) ) return EXCEPTION_CONTINUE_SEARCH;

    char msg[ 2048 ] = {};
    std::format_to_n( msg, std::size( msg ) - 1, EXCEPTION_FORMAT_STR,
        ( u32 ) pRecord->ExceptionCode, ( u64 ) pRecord->ExceptionAddress,
        ( u64 ) pRecord->ExceptionInformation[ 0 ], ( u64 ) pRecord->ExceptionInformation[ 1 ] );

    if( !IsDebuggerPresent() )
    {
        SysErrMsgBox( msg );
        std::abort();
    }

    i32 retVal = MessageBoxA( nullptr, msg, "SEH",
        MB_RETRYCANCEL | MB_ICONERROR | MB_APPLMODAL );
    // NOTE: user pressed retry so we delegate again; this makes Win hand it to the debugger
    if( IDRETRY == retVal ) return EXCEPTION_CONTINUE_SEARCH;

    std::abort();
}
// NOTE: can't be first if we have clang and clang-asan
static const bool EXCEPTION_HANDLER_HOOKED = ( AddVectoredExceptionHandler( 0, WinExceptionHandler ), true );

// ---------------------------------------------------------------------------------------------------------------
#include <ht_memory.h>
// ---------------------------------------------------------------------------------------------------------------
void*	ht_os_virtual_reserve( u64 sizeInBytes )
{
	void* mem = VirtualAlloc( nullptr, sizeInBytes, MEM_RESERVE, PAGE_READWRITE );
	WIN_CHECK( mem );
	return mem;
}
void	ht_os_virtual_release( void* mem ) { WIN_CHECK( VirtualFree( mem, 0, MEM_RELEASE ) ); }
void*	ht_os_virtual_commit( void* mem, u64 sizeInBytes )
{
	u64 alignedSize = ( ( sizeInBytes + OS_COMMIT_PAGE_SIZE_IN_BYTES - 1 )
	    / OS_COMMIT_PAGE_SIZE_IN_BYTES ) * OS_COMMIT_PAGE_SIZE_IN_BYTES;

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
// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
#include <System/sys_sync.h>
// ---------------------------------------------------------------------------------------------------------------
static_assert( sizeof( SRWLOCK ) == sizeof( void* ), "SRWLOCK storage size mismatch" );

copyable_srwlock::copyable_srwlock()                             { *( SRWLOCK* )( &osLock ) = SRWLOCK_INIT; };
copyable_srwlock::copyable_srwlock( const copyable_srwlock& )    { *( SRWLOCK* )( &osLock ) = SRWLOCK_INIT; }
copyable_srwlock::copyable_srwlock( copyable_srwlock&& )         { *( SRWLOCK* )( &osLock ) = SRWLOCK_INIT; }

copyable_srwlock& copyable_srwlock::operator=( const copyable_srwlock& ) { return *this; }
copyable_srwlock& copyable_srwlock::operator=( copyable_srwlock&& )      { return *this; }

void copyable_srwlock::Acquire() const { AcquireSRWLockExclusive( ( SRWLOCK* ) ( &osLock ) ); }
void copyable_srwlock::Release() const { ReleaseSRWLockExclusive( ( SRWLOCK* ) ( &osLock ) ); }

using win32_atomic64 = volatile __int64;

template<sys_fence_t BARRIER>
u64 SysAtomicCas64( atomic_u64* pAddr, u64 exchange, u64 comparand )
{
	if constexpr( sys_fence_t::NONE == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchangeNoFence64(
			( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}
	else if constexpr( sys_fence_t::ACQ == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchangeAcquire64(
			( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}
	else if constexpr( sys_fence_t::REL == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchangeRelease64(
			( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}
	else if constexpr( sys_fence_t::SEQ_CST == BARRIER )
	{
		return ( u64 ) InterlockedCompareExchange64(
			( win32_atomic64* ) pAddr,  ( LONG64 ) exchange, ( LONG64 ) comparand );
	}

	return ~0ull;
}

template<sys_fence_t BARRIER>
u128 SysAtomicCas128( atomic_u128* pAddr, u128 exchange, u128 comparand )
{
#if defined( _M_ARM64 )
#error "Impl this"
#endif

	InterlockedCompareExchange128( ( win32_atomic64* ) pAddr, ( __int64 ) exchange.hi,
	    ( __int64 ) exchange.lo, ( __int64* ) &comparand );

	return comparand;
}

template<sys_fence_t BARRIER>
u64 SysAtomicAnd64( atomic_u64* pAddr, u64 mask )
{
	if constexpr( sys_fence_t::NONE == BARRIER )
	{
		return ( u64 ) InterlockedAnd64NoFence( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}
	else if constexpr( sys_fence_t::ACQ == BARRIER )
	{
		return ( u64 ) InterlockedAnd64Acquire( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}
	else if constexpr( sys_fence_t::REL == BARRIER )
	{
		return ( u64 ) InterlockedAnd64Release( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}
	else if constexpr( sys_fence_t::SEQ_CST == BARRIER )
	{
		return ( u64 ) InterlockedAnd64( ( win32_atomic64* ) pAddr, ( LONG64 ) mask );
	}

	return ~0ull;
}

template<sys_fence_t BARRIER>
u64 SysAtomicOr64( atomic_u64* pAddr, u64 value )
{
    if constexpr( sys_fence_t::NONE == BARRIER )
    {
        return ( u64 ) InterlockedOr64NoFence( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
    }
    else if constexpr( sys_fence_t::ACQ == BARRIER )
    {
        return ( u64 ) InterlockedOr64Acquire( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
    }
    else if constexpr( sys_fence_t::REL == BARRIER )
    {
        return ( u64 ) InterlockedOr64Release( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
    }
    else if constexpr( sys_fence_t::SEQ_CST == BARRIER )
    {
        return ( u64 ) InterlockedOr64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
    }

    return ~0ull;
}

template<sys_fence_t BARRIER>
u64 SysAtomicAdd64( atomic_u64* pAddr, u64 value )
{
	if constexpr( sys_fence_t::NONE == BARRIER )
	{
		return ( u64 ) InterlockedExchangeAddNoFence64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}
	else if constexpr( sys_fence_t::ACQ == BARRIER )
	{
		return ( u64 ) InterlockedExchangeAddAcquire64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}
	else if constexpr( sys_fence_t::REL == BARRIER )
	{
		return ( u64 ) InterlockedExchangeAddRelease64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}
	else if constexpr( sys_fence_t::SEQ_CST == BARRIER )
	{
		return ( u64 ) InterlockedExchangeAdd64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}

	return ~0ull;
}

template<sys_fence_t BARRIER>
u64 SysAtomicRead64( atomic_u64* pAddr )
{
	if constexpr( sys_fence_t::NONE == BARRIER )
	{
		return ( u64 ) ReadNoFence64( ( win32_atomic64* ) pAddr );
	}
	else if constexpr( sys_fence_t::ACQ == BARRIER )
	{
		return ( u64 ) ReadAcquire64( ( win32_atomic64* ) pAddr );
	}
	else if constexpr( sys_fence_t::SEQ_CST == BARRIER )
	{
		return ( u64 ) ReadAcquire64( ( win32_atomic64* ) pAddr );
	}

	return ~0ull;
}

template<sys_fence_t BARRIER>
void SysAtomicWrite64( atomic_u64* pAddr, u64 value )
{
	if constexpr( sys_fence_t::NONE == BARRIER )
	{
		WriteNoFence64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}
	else if constexpr( sys_fence_t::REL == BARRIER )
	{
		WriteRelease64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}
	else if constexpr( sys_fence_t::SEQ_CST == BARRIER )
	{
		InterlockedExchange64( ( win32_atomic64* ) pAddr, ( LONG64 ) value );
	}
}

// NOTE: defined here, so every barrier caller in another TU can ask for has to be instantiated here
template u64 SysAtomicCas64<sys_fence_t::NONE>( atomic_u64*, u64, u64 );
template u64 SysAtomicCas64<sys_fence_t::ACQ>( atomic_u64*, u64, u64 );
template u64 SysAtomicCas64<sys_fence_t::REL>( atomic_u64*, u64, u64 );
template u64 SysAtomicCas64<sys_fence_t::SEQ_CST>( atomic_u64*, u64, u64 );
template u128 SysAtomicCas128<sys_fence_t::NONE>( atomic_u128*, u128, u128 );
template u128 SysAtomicCas128<sys_fence_t::ACQ>( atomic_u128*, u128, u128 );
template u128 SysAtomicCas128<sys_fence_t::REL>( atomic_u128*, u128, u128 );
template u128 SysAtomicCas128<sys_fence_t::SEQ_CST>( atomic_u128*, u128, u128 );
template u64 SysAtomicAnd64<sys_fence_t::NONE>( atomic_u64*, u64 );
template u64 SysAtomicAnd64<sys_fence_t::ACQ>( atomic_u64*, u64 );
template u64 SysAtomicAnd64<sys_fence_t::REL>( atomic_u64*, u64 );
template u64 SysAtomicAnd64<sys_fence_t::SEQ_CST>( atomic_u64*, u64 );
template u64 SysAtomicOr64<sys_fence_t::NONE>( atomic_u64*, u64 );
template u64 SysAtomicOr64<sys_fence_t::ACQ>( atomic_u64*, u64 );
template u64 SysAtomicOr64<sys_fence_t::REL>( atomic_u64*, u64 );
template u64 SysAtomicOr64<sys_fence_t::SEQ_CST>( atomic_u64*, u64 );
template u64 SysAtomicAdd64<sys_fence_t::NONE>( atomic_u64*, u64 );
template u64 SysAtomicAdd64<sys_fence_t::ACQ>( atomic_u64*, u64 );
template u64 SysAtomicAdd64<sys_fence_t::REL>( atomic_u64*, u64 );
template u64 SysAtomicAdd64<sys_fence_t::SEQ_CST>( atomic_u64*, u64 );
template u64 SysAtomicRead64<sys_fence_t::NONE>( atomic_u64* );
template u64 SysAtomicRead64<sys_fence_t::ACQ>( atomic_u64* );
template u64 SysAtomicRead64<sys_fence_t::SEQ_CST>( atomic_u64* );
template void SysAtomicWrite64<sys_fence_t::NONE>( atomic_u64*, u64 );
template void SysAtomicWrite64<sys_fence_t::REL>( atomic_u64*, u64 );
template void SysAtomicWrite64<sys_fence_t::SEQ_CST>( atomic_u64*, u64 );

sys_semaphore::sys_semaphore() : hndl{ ( u64 ) CreateSemaphoreW( 0, 0, LONG_MAX, 0 ) }
{
	WIN_CHECK( Win32IsHandleValid( ( HANDLE ) hndl ) );
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
// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
#include <System/sys_file.h>
// ---------------------------------------------------------------------------------------------------------------
static u64 WinGetFileSizeInBytes( HANDLE hFile )
{
    LARGE_INTEGER largeInt;
    WIN_CHECK( GetFileSizeEx( hFile, &largeInt ) );
    return largeInt.QuadPart;
}

std::span<u8> SysReadFileBinary( const char* path, linear_arena& arena )
{
    HANDLE hFile = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
    HT_ASSERT( INVALID_HANDLE_VALUE != hFile );
    defer { WIN_CHECK( CloseHandle( hFile ) ); };

    u64     szInBytes       = WinGetFileSizeInBytes( hFile );
    void*   mem             = arena.Alloc( szInBytes, 1 );
    DWORD   readSzInBytes   = 0;
    WIN_CHECK( ReadFile( hFile, mem, ( DWORD ) szInBytes, &readSzInBytes, nullptr ) );
    HT_ASSERT( ( DWORD ) szInBytes == readSzInBytes );

    return { ( u8* ) mem, szInBytes };
}

constexpr DWORD MakeGenericAccessFlags( file_perm_t openFlags )
{
    using enum file_perm_t;
	DWORD access = 0;

	if( openFlags & READ ) { access |= GENERIC_READ; }
	if( openFlags & WRITE ) { access |= GENERIC_WRITE; }
	if( ( openFlags & READ ) && ( openFlags & WRITE ) )
	{
		access = GENERIC_ALL;
	}

	return access;
}
constexpr DWORD MakeFileMappingFlags( file_perm_t openFlags )
{
    using enum file_perm_t;
	if( ( openFlags & READ ) && ( openFlags & WRITE ) ) { return PAGE_READWRITE; }
	if( openFlags & READ ) return PAGE_READONLY;
	if( openFlags & WRITE ) return PAGE_WRITECOPY;

	return 0;
}
constexpr DWORD MakeMapViewFlags( file_perm_t openFlags )
{
    using enum file_perm_t;
	if( ( openFlags & READ ) && ( openFlags & WRITE ) ) { return FILE_MAP_ALL_ACCESS; }
	if( openFlags & READ ) return FILE_MAP_READ;
	if( openFlags & WRITE ) return FILE_MAP_WRITE;

	return 0;
}

constexpr DWORD MakeCreateFlags( file_create_t createFlags )
{
	using enum file_create_t;
	switch( createFlags )
	{
	case CREATE:			return CREATE_NEW;
	case OPEN_IF_EXISTS:	return OPEN_EXISTING;
	case OVERWRITE:			return CREATE_ALWAYS;
	}
	HT_ASSERT( 0 && "Wrong flags" );
	return 0;
}

constexpr DWORD MakeAccessFlags( file_access_t accessFlags )
{
	using enum file_access_t;
	switch( accessFlags )
	{
	case SEQUENTIAL:	        return FILE_FLAG_SEQUENTIAL_SCAN;
	case RANDOM:		        return FILE_FLAG_RANDOM_ACCESS;
	case CONCURRENT:	        return FILE_FLAG_OVERLAPPED;
	case CONCURRENT_UNBUFFERED:	return FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING;
	}

	HT_ASSERT( 0 && "Wrong flags" );
	return 0;
}

u64 mmap_file::Timestamp() const
{
	FILETIME fileTime = {};
	WIN_CHECK( SUCCEEDED( GetFileTime( ( HANDLE ) hFile, nullptr, nullptr,
	    &fileTime ) ) );

	ULARGE_INTEGER timestamp = { .LowPart = fileTime.dwLowDateTime, .HighPart = fileTime.dwHighDateTime };
	return u64( timestamp.QuadPart );
}

mmap_file SysCreateMmapFile(
	const char*				path,
	file_perm_t		permissionFlags,
	file_create_t		createFlags,
	file_access_t		accessFlags
) {
	DWORD dwPermissionFlags		= MakeGenericAccessFlags( permissionFlags );
	DWORD dwCreateFlags			= MakeCreateFlags( createFlags );
	DWORD dwAccessFlags			= MakeAccessFlags( accessFlags );
	DWORD dwFileMappingAccess	= MakeFileMappingFlags( permissionFlags );
	DWORD dwDataViewAccess		= MakeMapViewFlags( permissionFlags );

	HANDLE hFile		= CreateFileA( path, dwPermissionFlags, FILE_SHARE_READ,
		nullptr, dwCreateFlags, dwAccessFlags, nullptr );
	WIN_CHECK( INVALID_HANDLE_VALUE != hFile );

	HANDLE hFileMapping = CreateFileMappingA( hFile, 0, dwFileMappingAccess,
		0, 0, nullptr );
	WIN_CHECK( hFileMapping );

	u64		qwFileSize	= WinGetFileSizeInBytes( hFile );
	u8* pData			= ( u8* ) MapViewOfFile( hFileMapping, dwDataViewAccess, 0,
		0, qwFileSize );
	WIN_CHECK( pData );

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

void* ht_os_create_file(
    const char*         filePath,
    file_perm_t  permissionFlags,
    file_create_t   createFlags,
    file_access_t   accessFlags,
    void*               hCompletionPort
) {
    DWORD dwPermissionFlags		= MakeGenericAccessFlags( permissionFlags );
    DWORD dwCreateFlags			= MakeCreateFlags( createFlags );
    DWORD dwAccessFlags			= MakeAccessFlags( accessFlags );

    HANDLE hFile = CreateFileA( filePath, dwPermissionFlags,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        dwCreateFlags, dwAccessFlags, nullptr );
    WIN_CHECK( INVALID_HANDLE_VALUE != hFile );

    if( hCompletionPort )
    {
        WIN_CHECK( CreateIoCompletionPort( hFile, hCompletionPort, 0, 0 ) );
    }

    return hFile;
}

std::span<const u8> HtOsCreateROFileMapping( void* hFile )
{
    HANDLE  hFileMapping = CreateFileMappingA( hFile, nullptr, PAGE_READONLY,
        0, 0, nullptr );
    WIN_CHECK( hFileMapping );
    defer { WIN_CHECK( CloseHandle( hFileMapping ) ); };

    u64     qwFileSize	= WinGetFileSizeInBytes( hFile );
    u8*     pData		= ( u8* ) MapViewOfFile( hFileMapping, FILE_MAP_READ, 0,
        0, qwFileSize );
    WIN_CHECK( pData );

    return { pData, qwFileSize };
}

void HtOSUnmapView( std::span<const u8> view ) { WIN_CHECK( UnmapViewOfFile( std::data( view ) ) ); }

void* HtOsCreateIOCompletionPort( u64 maxWorkerThreads )
{
    HANDLE hIOCompletionPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE, nullptr,
        0, maxWorkerThreads );
    WIN_CHECK( hIOCompletionPort );
    return hIOCompletionPort;
}

void SysWriteFileConcurrentBlocking( void* hFile, u64 offsetInBytes, std::span<const u8> bytes )
{
    HT_ASSERT( std::size( bytes ) <= MAXDWORD );

    HANDLE hEvent = CreateEventA( nullptr, TRUE, FALSE, nullptr );
    WIN_CHECK( hEvent );
    defer { WIN_CHECK( CloseHandle( hEvent ) ); };
    
    OVERLAPPED overlapped   = {
        .Offset       = ( DWORD ) offsetInBytes,
        .OffsetHigh   = ( DWORD ) ( offsetInBytes >> 32 ),
        .hEvent       = hEvent
    };
    
    if( !WriteFile( hFile, std::data( bytes ), DWORD( std::size( bytes ) ),
        nullptr, &overlapped ) )
    {
        WIN_CHECK( ERROR_IO_PENDING == GetLastError() );
    }

    DWORD writtenInBytes = 0;
    WIN_CHECK( GetOverlappedResult( hFile, &overlapped, &writtenInBytes, TRUE ) );
    HT_ASSERT( std::size( bytes ) == writtenInBytes );
}

void SysReadFileAsyncUnbuffered( void* hFile, u64 offsetInBytes, std::span<u8> outBytes, ht_os_io_request* pIOReq )
{
    static_assert( sizeof( OVERLAPPED ) <= sizeof( ht_os_io_request ) );
    static_assert( alignof( OVERLAPPED ) <= alignof( ht_os_io_request ) );

    HT_ASSERT( hFile && std::data( outBytes ) && std::size( outBytes ) );
    HT_ASSERT( IsAlignedToPot( ( u64 ) std::data( outBytes ), OS_UNBUFFERED_IO_MEM_ALIGNMENT ) );
    HT_ASSERT( IsAlignedToPot( std::size( outBytes ), OS_UNBUFFERED_IO_MEM_ALIGNMENT ) );
    HT_ASSERT( IsAlignedToPot( offsetInBytes, OS_UNBUFFERED_IO_MEM_ALIGNMENT ) );

    OVERLAPPED* pOverlapped = ( OVERLAPPED* ) pIOReq;
    *pOverlapped = {
        .Offset       = ( DWORD ) offsetInBytes,
        .OffsetHigh   = ( DWORD ) ( offsetInBytes >> 32 ),
    };

    if( !ReadFile( hFile, std::data( outBytes ), std::size( outBytes ),
        nullptr, pOverlapped ) )
    {
        WIN_CHECK( ERROR_IO_PENDING == GetLastError() );
    }
}

ht_io_comp_array SysPollIOCompletionsStatus( void* hPort, u64 waitInMilliSecs )
{
    OVERLAPPED_ENTRY    entries[ OS_MAX_ASYNC_IO_REQS_IN_FLIGHT ] = {};
    ULONG               entryCount = 0;
    if( !GetQueuedCompletionStatusEx( hPort, entries, std::size( entries ), &entryCount,
        waitInMilliSecs, FALSE ) )
    {
        WIN_CHECK( WAIT_TIMEOUT == GetLastError() );
    }

    std::span cmplOverlapped = { entries, entryCount };
    for( const OVERLAPPED_ENTRY& cov : cmplOverlapped )
    {
        WIN_CHECK( !cov.lpOverlapped->Internal );
    }

    auto LmbdToHtReq = []( const OVERLAPPED_ENTRY& cov ) HT_LAMBDA_FORCEINLINE -> ht_os_io_completion
    {
        return {
            .pReq = ( ht_os_io_request* ) cov.lpOverlapped,
            .numBytesTransferred = cov.dwNumberOfBytesTransferred
        };
    };

    return { std::from_range, cmplOverlapped | std::views::transform( LmbdToHtReq ) };
}

// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
#define THREAD_CALLING_CONV WINAPI
#include <System/sys_thread.h>
// ---------------------------------------------------------------------------------------------------------------
sys_thread SysCreateThread( u64	stackSize, PfnSysThreadProc ThreadProc, void* pData, const wchar_t* name )
{
	DWORD threadId = 0;
	HANDLE hThread = CreateThread( nullptr, stackSize, ( LPTHREAD_START_ROUTINE ) ThreadProc,
		pData, 0, &threadId );
	WIN_CHECK( INVALID_HANDLE_VALUE != hThread );

	if( name ) SysNameThread( ( u64 ) hThread, name );

	return {
		.hndl		= ( u64 ) hThread,
		.threadId	= threadId
	};
}

void SysThreadSleep( u32 milliSecs ) { return Sleep( milliSecs ); }

void SysNameThread( u64 hThread, const wchar_t* name )
{
	WIN_CHECK( SUCCEEDED( SetThreadDescription( ( HANDLE ) hThread, name ) ) );
}
// ---------------------------------------------------------------------------------------------------------------


// ---------------------------------------------------------------------------------------------------------------
#include <System/sys_timer.h>
// ---------------------------------------------------------------------------------------------------------------
u64 SysGetCpuFreq()
{
	LARGE_INTEGER freq;
	QueryPerformanceFrequency( &freq );
	return freq.QuadPart;
}
u64 SysTicks()
{
	LARGE_INTEGER tick;
	QueryPerformanceCounter( &tick );
	return tick.QuadPart;
}
// ---------------------------------------------------------------------------------------------------------------

// ---------------------------------------------------------------------------------------------------------------
#include <System/sys_std_streams.h>
// ---------------------------------------------------------------------------------------------------------------
void SysWriteToStdStream( const char* str, sys_stream_t streamType )
{
	HANDLE hStream = INVALID_HANDLE_VALUE;
	if( sys_stream_t::OUTPUT == streamType )	hStream = GetStdHandle( STD_OUTPUT_HANDLE );
	else if( sys_stream_t::ERR == streamType )	hStream = GetStdHandle( STD_ERROR_HANDLE );
	DWORD lpNumberOfBytesWritten;
	WriteFile( hStream, str, ( DWORD ) strlen( str ), &lpNumberOfBytesWritten, nullptr );
}
void SysErrMsgBox( const char* str )
{
	MessageBoxA( nullptr, str, TEXT( "Error" ), MB_OK | MB_ICONERROR | MB_APPLMODAL );
}
// ---------------------------------------------------------------------------------------------------------------