#include <System/sys_file.h>
#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_err.h"

enum class MMF_OPEN_FLAGS : DWORD
{
	READ = GENERIC_READ,
	WRITE = GENERIC_WRITE,
};

enum class MMF_CREATE_FLAGS : DWORD
{
	CREATE = CREATE_NEW,
	OPEN_IF_EXISTS = OPEN_EXISTING,
};

enum class MMF_ACCESS_FLAGS : DWORD
{
	SEQUENTIAL = FILE_FLAG_SEQUENTIAL_SCAN,
	RANDOM = FILE_FLAG_RANDOM_ACCESS,
};

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

struct win32_mmaped_file_handle : file
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = INVALID_HANDLE_VALUE;
	std::span<u8> dataView;

	win32_mmaped_file_handle( std::string_view fileName, file_permissions_flags openFlags, DWORD createFlags, DWORD accessFlags )
	{
		DWORD desiredAccessFlags = MakeGenericAccessFlags( openFlags );
		this->hFile = CreateFileA(
			std::data( fileName ), desiredAccessFlags, FILE_SHARE_READ, 0, createFlags, accessFlags, 0 );
		WIN_CHECK( hFile == INVALID_HANDLE_VALUE );

		DWORD dwFlagsFileMapping = MakeFileMappingFlags( openFlags );

		this->hFileMapping = CreateFileMappingA( this->hFile, 0, dwFlagsFileMapping, 0, 0, 0 );
		WIN_CHECK( hFileMapping == INVALID_HANDLE_VALUE );

		DWORD dwFileSizeHigh;
		size_t qwFileSize = GetFileSize( this->hFile, &dwFileSizeHigh );
		qwFileSize += ( size_t( dwFileSizeHigh ) << 32 );
		WIN_CHECK( qwFileSize == 0 );

		DWORD dwFlagsView = MakeMapViewFlags( openFlags );
		u8* pData = ( u8* ) MapViewOfFile( this->hFileMapping, dwFlagsView, 0, 0, qwFileSize );
		this->dataView = { pData, qwFileSize };
	}

	virtual size_t Size() override
	{
		return std::size( this->dataView );
	}

	virtual u8* Data() override
	{
		return std::data( this->dataView );
	}
	virtual u64 Timestamp() override
	{
		FILETIME fileTime = {};
		WIN_CHECK( !GetFileTime( this->hFile, 0, 0, &fileTime ) );

		ULARGE_INTEGER timestamp = {};
		timestamp.LowPart = fileTime.dwLowDateTime;
		timestamp.HighPart = fileTime.dwHighDateTime;

		return u64( timestamp.QuadPart ); 
	}
	virtual std::span<u8> Span() override
	{
		return dataView;
	}
	virtual ~win32_mmaped_file_handle() override
	{
		UnmapViewOfFile( std::data( this->dataView ) );
		CloseHandle( this->hFileMapping );
		CloseHandle( this->hFile );
	}
};

std::unique_ptr<file> SysCreateFile( std::string_view path, file_permissions_flags filePermissions )
{
	return std::make_unique<win32_mmaped_file_handle>(
		path, filePermissions, ( DWORD ) MMF_CREATE_FLAGS::OPEN_IF_EXISTS, ( DWORD ) MMF_ACCESS_FLAGS::SEQUENTIAL );
}