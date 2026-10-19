// NOTE: tell the compiler to run our STATIC init before the rest of the program ( CRT still runs first )
#pragma init_seg( lib )

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>
#include <winioctl.h>
#include <ioringapi.h>

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
static ULONG g_StackGuaranteeInBytes = 64 * 1024;
static const bool EXCEPTION_HANDLER_HOOKED = ( AddVectoredExceptionHandler( 0, WinExceptionHandler ),
    SetThreadStackGuarantee( &g_StackGuaranteeInBytes ), true );

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

	InterlockedCompareExchange128( ( win32_atomic64* ) pAddr, ( ( __int64* ) &exchange )[ 1 ],
	    ( ( __int64* ) &exchange )[ 0 ], ( __int64* ) &comparand );

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
u64 SysGetFileSizeInBytes( void* hFile )
{
    LARGE_INTEGER largeInt;
    WIN_CHECK( GetFileSizeEx( hFile, &largeInt ) );
    return largeInt.QuadPart;
}

std::span<u8> SysReadWholeFile( const char* path, arena_t auto& arena )
{
    HANDLE hFile = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
    HT_ASSERT( INVALID_HANDLE_VALUE != hFile );
    defer { WIN_CHECK( CloseHandle( hFile ) ); };

    u64     szInBytes       = SysGetFileSizeInBytes( hFile );
    void*   mem             = arena.Alloc( szInBytes, 1 );
    DWORD   readSzInBytes   = 0;
    WIN_CHECK( ReadFile( hFile, mem, ( DWORD ) szInBytes, &readSzInBytes, nullptr ) );
    HT_ASSERT( ( DWORD ) szInBytes == readSzInBytes );

    return { ( u8* ) mem, szInBytes };
}

template std::span<u8> SysReadWholeFile<linear_arena>( const char*, linear_arena& );
template std::span<u8> SysReadWholeFile<virtual_arena>( const char*, virtual_arena& );
template std::span<u8> SysReadWholeFile<scoped_arena<linear_arena>>( const char*, scoped_arena<linear_arena>& );
template std::span<u8> SysReadWholeFile<scoped_arena<virtual_arena>>( const char*, scoped_arena<virtual_arena>& );

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
	if( openFlags & DELETABLE ) { access |= DELETE; }

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
	case SEQUENTIAL:	    return FILE_FLAG_SEQUENTIAL_SCAN;
	case RANDOM:		    return FILE_FLAG_RANDOM_ACCESS;
	case CONCURRENT:	    return FILE_FLAG_OVERLAPPED;
	case RAW_CONCURRENT:    return FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING;
	}

	HT_ASSERT( 0 && "Wrong flags" );
	return 0;
}


void* ht_os_create_file(
    const char*     filePath,
    file_perm_t     permissionFlags,
    file_create_t   createFlags,
    file_access_t   accessFlags
) {
    DWORD dwPermissionFlags		= MakeGenericAccessFlags( permissionFlags );
    DWORD dwCreateFlags			= MakeCreateFlags( createFlags );
    DWORD dwAccessFlags			= MakeAccessFlags( accessFlags );

    HANDLE hFile = CreateFileA( filePath, dwPermissionFlags,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        dwCreateFlags, dwAccessFlags, nullptr );
    WIN_CHECK( INVALID_HANDLE_VALUE != hFile );

    if( file_access_t::RAW_CONCURRENT == accessFlags )
    {
        FS_BPIO_INPUT   bypassIOInput   = { .Operation = FS_BPIO_OP_ENABLE };
        FS_BPIO_OUTPUT  bypassIOOutput  = {};
        OVERLAPPED      overlapped      = {}; // NOTE: we are the first owners so no event needed
        if( !DeviceIoControl( hFile, FSCTL_MANAGE_BYPASS_IO, &bypassIOInput,
            sizeof( bypassIOInput ),&bypassIOOutput, sizeof( bypassIOOutput ),
            nullptr, &overlapped ) )
        {
            WIN_CHECK( ERROR_IO_PENDING == GetLastError() );
        }

        DWORD bytesReturned = 0;
        WIN_CHECK( GetOverlappedResult( hFile, &overlapped, &bytesReturned, TRUE ) );
        HT_ASSERT( 0 == bypassIOOutput.Enable.OpStatus );
    }

    return hFile;
}

void ht_os_close_file( void* hFile ) { WIN_CHECK( CloseHandle( hFile ) ); }
void ht_delete_file_by_handle( void* hFile )
{
    FILE_DISPOSITION_INFO_EX info = { .Flags = FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS };
    WIN_CHECK( SetFileInformationByHandle( hFile, FileDispositionInfoEx, &info, sizeof( info ) ) );
}

std::span<const u8> HtOsCreateROFileMapping( void* hFile )
{
    HANDLE  hFileMapping = CreateFileMappingA( hFile, nullptr, PAGE_READONLY,
        0, 0, nullptr );
    WIN_CHECK( hFileMapping );
    defer { WIN_CHECK( CloseHandle( hFileMapping ) ); };

    u64     qwFileSize	= SysGetFileSizeInBytes( hFile );
    u8*     pData		= ( u8* ) MapViewOfFile( hFileMapping, FILE_MAP_READ, 0,
        0, qwFileSize );
    WIN_CHECK( pData );

    return { pData, qwFileSize };
}

void HtOSUnmapView( std::span<const u8> view ) { WIN_CHECK( UnmapViewOfFile( std::data( view ) ) ); }

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

u64 SysReadNBytesFromFileBlocking( void* hFile, u64 fileOffsetInBytes, u64 readSzInBytes, std::span<u8> outBuff )
{
    u64 buffSzInBytes = std::size( outBuff );
    HT_ASSERT( buffSzInBytes >= readSzInBytes );

    OVERLAPPED overlapped = {
        .Offset       = ( DWORD ) fileOffsetInBytes,
        .OffsetHigh   = ( DWORD ) ( fileOffsetInBytes >> 32 ),
    };

    if( !ReadFile( hFile, &outBuff[ 0 ], readSzInBytes, 0, &overlapped ) )
    {
        WIN_CHECK( ERROR_IO_PENDING == GetLastError() );
    }

    DWORD readByteCount = 0;
    WIN_CHECK( GetOverlappedResult( hFile, &overlapped, &readByteCount, TRUE ) );

    return readByteCount;
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

u64 SysGetFileTimestamp( void* hFile )
{
    FILETIME fileTime = {};
    WIN_CHECK( SUCCEEDED( GetFileTime( hFile, 0, 0, &fileTime ) ) );

    ULARGE_INTEGER timestamp = { .LowPart = fileTime.dwLowDateTime, .HighPart = fileTime.dwHighDateTime };
    return u64( timestamp.QuadPart );
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

// ---------------------------------------------------------------------------------------------------------------
#include <System/sys_io_ring.h>
// ---------------------------------------------------------------------------------------------------------------

void* SysMakeIoRing( u64 maxRequests, void* hFile, std::span<const std::span<u8>> readBuffers )
{
    IORING_CAPABILITIES ioRingCaps = {};
    WIN_CHECK( SUCCEEDED( QueryIoRingCapabilities( &ioRingCaps ) ) );

    HIORING hIORing = nullptr;
    WIN_CHECK( SUCCEEDED( CreateIoRing( ioRingCaps.MaxVersion, {}, maxRequests,
        2 * maxRequests, &hIORing ) ) );

    WIN_CHECK( SUCCEEDED( BuildIoRingRegisterFileHandles( hIORing, 1, &hFile, 0 ) ) );

    inline_array<IORING_BUFFER_INFO, 8> ioRingBuffInfos = {};
    for( const std::span<u8> buff : readBuffers )
    {
        HT_ASSERT( UINT32_MAX >= std::size( buff ) );
        ioRingBuffInfos.push_back( {
            .Address = &buff[ 0 ],
            .Length = ( u32 ) std::size( buff )
        } );
    }

    WIN_CHECK( SUCCEEDED( BuildIoRingRegisterBuffers( hIORing, ( UINT32 ) std::size( ioRingBuffInfos ),
        &ioRingBuffInfos[ 0 ], 0 ) ) );

    // NOTE: win internals says that those buffers will dangle is we don't submit here
    WIN_CHECK( SUCCEEDED( SubmitIoRing( hIORing, IORING_SUBMIT_WAIT_ALL, INFINITE, nullptr ) ) );

    // NOTE: drain the registered reqs
    for( u64 cmpi = 0; cmpi < 2; cmpi++ )
    {
        IORING_CQE ioRingCQE = {};
        while( S_OK != PopIoRingCompletion( ( HIORING ) hIORing, &ioRingCQE ) ) {}
        WIN_CHECK( SUCCEEDED( ioRingCQE.ResultCode ) );
    }

    return hIORing;
}

bool SysTryPushIoRingRequest(
    void*   hIORing,
    u64     readBuffIdx,
    u32     readBuffOffset,
    u64     fileOffset,
    u32     readSize
) {
    HRESULT res = BuildIoRingReadFile( ( HIORING ) hIORing, IoRingHandleRefFromIndex( 0 ),
        IoRingBufferRefFromIndexAndOffset( readBuffIdx, readBuffOffset ),
        readSize, fileOffset, 0, IOSQE_FLAGS_NONE );

    if( IORING_E_SUBMISSION_QUEUE_FULL == res ) return false;

    WIN_CHECK( SUCCEEDED( res ) );

    return true;
}

bool SysTryPushIoRingRequestWRawBuffer( void* hIORing, void* rawBuffer, u64 fileOffset, u32 readSize )
{
    HRESULT res = BuildIoRingReadFile( ( HIORING ) hIORing, IoRingHandleRefFromIndex( 0 ),
        IoRingBufferRefFromPointer( rawBuffer ), readSize, fileOffset, 0, IOSQE_FLAGS_NONE );

    if( IORING_E_SUBMISSION_QUEUE_FULL == res ) return false;

    WIN_CHECK( SUCCEEDED( res ) );

    return true;
}

u64 SysIoRingSubmit( void* hIORing, u64 waitInMs )
{
    u32 subsInQueue = 0;
    HRESULT res = SubmitIoRing( ( HIORING ) hIORing, 0, waitInMs, &subsInQueue );
    //if( IORING_E_WAIT_TIMEOUT == res ) return false;

    WIN_CHECK( SUCCEEDED( res ) );

    return subsInQueue;
}

bool SysIoRingPollCompletion( void* hIORing )
{
    IORING_CQE ioRingCQE = {};
    if( S_OK == PopIoRingCompletion( ( HIORING ) hIORing, &ioRingCQE ) )
    {
        WIN_CHECK( SUCCEEDED( ioRingCQE.ResultCode ) );
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------
