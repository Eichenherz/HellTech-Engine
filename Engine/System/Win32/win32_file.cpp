#include <System/sys_file.h>
#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_err.h"

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

struct win32_mmaped_file_handle final : mmap_file
{
	std::span<u8> dataView;
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = INVALID_HANDLE_VALUE;

	win32_mmaped_file_handle( 
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

	virtual size_t size() const override
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
	win32_mmaped_file_handle* pWin32File = ( win32_mmaped_file_handle* ) pFile;
	if( pWin32File )
	{
		UnmapViewOfFile( std::data( pWin32File->dataView ) );
		CloseHandle( pWin32File->hFileMapping );
		CloseHandle( pWin32File->hFile );
		pFile = nullptr;
	}
}

unique_mmap_file SysCreateFileSysCreateFile( 
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
	return { new win32_mmaped_file_handle{ std::data( path ), dwPermissionFlags, dwCreateFlags, 
		dwAccessFalgs, dwFileMappingAccess, dwDataViewAccess }, Win32MmapFileDestroyer };
}