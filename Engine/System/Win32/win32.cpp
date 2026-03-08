#include <System/sys_sync.h>
#include <System/sys_mem_arena.h>
#include <System/sys_file.h>

#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_err.h"

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
// ===============================================================================================================

// ===============================================================================================================
// sys_mem_arena.h
// ===============================================================================================================
virtual_arena::virtual_arena( u64 reservedBytesCount ) : reserved{ reservedBytesCount }
{
    base = ( u8* ) VirtualAlloc( nullptr, reserved, MEM_RESERVE, PAGE_READWRITE );
	WIN_CHECK( base );
}

virtual_arena::~virtual_arena()
{
	if( base )
	{
		WIN_CHECK( VirtualFree( base, 0, MEM_RELEASE ) );
	}
}

virtual_arena::virtual_arena( virtual_arena&& o )
    : base{ std::exchange( o.base, nullptr ) }
    , offset{ std::exchange( o.offset, 0 ) }
    , committed{ std::exchange( o.committed, 0 ) }
    , reserved{ std::exchange( o.reserved, 0 ) }
{}

virtual_arena& virtual_arena::operator=( virtual_arena&& o )
{
    HT_ASSERT( this != &o && "self move-assign" );
    base      = std::exchange( o.base, nullptr );
    offset    = std::exchange( o.offset, 0 );
    committed = std::exchange( o.committed, 0 );
    reserved  = std::exchange( o.reserved, 0 );
    return *this;
}

void virtual_arena::Rewind( u64 mark ) 
{
    HT_ASSERT( mark <= offset && "rewind past current offset" );
#ifdef _DEBUG
    std::memset( base + mark, 0xDE, offset - mark );
#endif
    offset = mark;
}

void virtual_arena::Reset() 
{
    if( committed )
    {
    #ifdef _DEBUG
        std::memset( base, 0xDE, committed );
    #endif
		WIN_CHECK( VirtualFree( base, committed, MEM_DECOMMIT ) );
    }

    committed = 0;
    offset = 0;
}

void* virtual_arena::Alloc( u64 bytes, u64 alignment ) 
{
    u64 alignedOffset = FwdAlign( offset, alignment );
    u64 newOffset = alignedOffset + bytes;

    HT_ASSERT( newOffset <= reserved );

    if( newOffset > committed )
    {
        u64 newCommitted = ( ( newOffset + PAGE_SIZE - 1 ) / PAGE_SIZE ) * PAGE_SIZE;
        if( newCommitted > reserved ) newCommitted = reserved;
		WIN_CHECK( VirtualAlloc( base + committed, newCommitted - committed, MEM_COMMIT, PAGE_READWRITE ) );
        committed = newCommitted;
    }

    void* alignedAddr = ( void* ) ( base + alignedOffset );

    offset = newOffset;
    return alignedAddr;
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

constexpr DWORD MakeCreateFalgs( file_create_flags createFlags )
{
	using enum file_create_flags;
	switch( createFlags )
	{
	case CREATE: return CREATE_NEW;
	case OPEN_IF_EXISTS: return OPEN_EXISTING;
	default: HT_ASSERT( 0 && "Wrong falgs" ); return 0;
	}
}

constexpr DWORD MakeAccessFalgs( file_access_flags accessFlags )
{
	using enum file_access_flags;
	switch( accessFlags )
	{
	case SEQUENTIAL: return FILE_FLAG_SEQUENTIAL_SCAN;
	case RANDOM: return FILE_FLAG_RANDOM_ACCESS;
	default: HT_ASSERT( 0 && "Wrong falgs" ); return 0;
	}
}

struct win32_mmaped_file final : mmap_file
{
	std::span<u8> dataView;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = INVALID_HANDLE_VALUE;

	win32_mmaped_file( 
		LPCSTR fileName, 
		DWORD filePermFlags, 
		DWORD createFlags, 
		DWORD accessFlags, 
		DWORD fileMappingAccess,
		DWORD dataViewAccess
	) {
		hFile = CreateFileA( fileName, filePermFlags, FILE_SHARE_READ, 0, createFlags, accessFlags, NULL );
		WIN_CHECK( INVALID_HANDLE_VALUE != hFile );

		hFileMapping = CreateFileMappingA( hFile, 0, fileMappingAccess, 0, 0, 0 );
		WIN_CHECK( INVALID_HANDLE_VALUE != hFileMapping );

		DWORD dwFileSizeHigh;
		u64 qwFileSize = GetFileSize( hFile, &dwFileSizeHigh );
		qwFileSize += ( u64( dwFileSizeHigh ) << 32 );
		WIN_CHECK( 0 != qwFileSize );

		u8* pData = ( u8* ) MapViewOfFile( hFileMapping, dataViewAccess, 0, 0, qwFileSize );
		WIN_CHECK( 0 != pData );
		dataView = { pData, qwFileSize };
	}

	virtual u64 size() const override
	{
		return std::size( dataView );
	}

	virtual u8* data() override
	{
		return std::data( dataView );
	}
	virtual const u8* data() const override
	{
		return std::data( dataView );
	}

	virtual u64 Timestamp() override
	{
		FILETIME fileTime = {};
		WIN_CHECK( !GetFileTime( hFile, 0, 0, &fileTime ) );

		ULARGE_INTEGER timestamp = {};
		timestamp.LowPart = fileTime.dwLowDateTime;
		timestamp.HighPart = fileTime.dwHighDateTime;

		return u64( timestamp.QuadPart ); 
	}
};

void Win32MmapFileDestroyer( mmap_file* pFile )
{
	win32_mmaped_file* pWin32File = ( win32_mmaped_file* ) pFile;
	if( pWin32File )
	{
		UnmapViewOfFile( std::data( pWin32File->dataView ) );
		CloseHandle( pWin32File->hFileMapping );
		CloseHandle( pWin32File->hFile );
		pFile = nullptr;
	}
}

unique_mmap_file SysCreateFile( 
	std::string_view path, 
	file_permissions_flags permissionFlags,
	file_create_flags createFlags,
	file_access_flags accessFalgs
) {
	DWORD dwPermissionFlags = MakeGenericAccessFlags( permissionFlags );
	DWORD dwCreateFlags = MakeCreateFalgs( createFlags );
	DWORD dwAccessFalgs = MakeAccessFalgs( accessFalgs );
	DWORD dwFileMappingAccess = MakeFileMappingFlags( permissionFlags );
	DWORD dwDataViewAccess = MakeMapViewFlags( permissionFlags );
	return { new win32_mmaped_file{ std::data( path ), dwPermissionFlags, dwCreateFlags, 
		dwAccessFalgs, dwFileMappingAccess, dwDataViewAccess }, Win32MmapFileDestroyer };
}
// ===============================================================================================================