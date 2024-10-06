#include <System/sys_file.h>
#include "DEFS_WIN32_NO_BS.h"
#include <Windows.h>

#include "win32_utils.hpp"

enum MMF_OPEN_FLAGS : DWORD
{
	READ = GENERIC_READ,
	WRITE = GENERIC_WRITE,
};

enum MMF_CREATE_FLAGS : DWORD
{
	CREATE = CREATE_NEW,
	OPEN_IF_EXISTS = OPEN_EXISTING,
};

enum MMF_ACCESS_FLAGS : DWORD
{
	SEQUENTIAL = FILE_FLAG_SEQUENTIAL_SCAN,
	RANDOM = FILE_FLAG_RANDOM_ACCESS,
};

struct win32_mmaped_file_handle : file
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = INVALID_HANDLE_VALUE;
	std::span<u8> dataView;

	win32_mmaped_file_handle( std::string_view fileName, DWORD openFlags, DWORD createFlags, DWORD accessFlags )
	{
		this->hFile = CreateFileA(
			std::data( fileName ), openFlags, FILE_SHARE_READ, 0, createFlags, accessFlags, 0 );
		WIN_CHECK( hFile == INVALID_HANDLE_VALUE );

		DWORD dwFlagsFileMapping = 0;
		switch( openFlags )
		{
		case MMF_OPEN_FLAGS::READ: 
			dwFlagsFileMapping = PAGE_READONLY;
			[[fallthrough]];
		case MMF_OPEN_FLAGS::WRITE:
			dwFlagsFileMapping = PAGE_READWRITE;
			break;
		}
		this->hFileMapping = CreateFileMappingA( this->hFile, 0, dwFlagsFileMapping, 0, 0, 0 );
		WIN_CHECK( hFileMapping == INVALID_HANDLE_VALUE );

		DWORD dwFlagsView = 0;
		switch( openFlags )
		{
		case MMF_OPEN_FLAGS::READ: 
			dwFlagsView |= FILE_MAP_READ;
			[[fallthrough]];
		case MMF_OPEN_FLAGS::WRITE:
			dwFlagsView |= FILE_MAP_WRITE; // NOTE: for MapViewOfFile FILE_MAP_WRITE is equivalent to FILE_MAP_ALL_ACCESS
			break;
		}

		DWORD dwFileSizeHigh;
		size_t qwFileSize = GetFileSize( this->hFile, &dwFileSizeHigh );
		qwFileSize += ( size_t( dwFileSizeHigh ) << 32 );
		WIN_CHECK( qwFileSize == 0 );
		u8* pData = ( u8* ) MapViewOfFile( this->hFileMapping, dwFlagsView, 0, 0, qwFileSize );
		this->dataView = { pData, qwFileSize };
	}

	virtual size_t size() override
	{
		return std::size( this->dataView );
	}

	virtual u8* data() override
	{
		return std::data( this->dataView );
	}
	virtual u64 timestamp() override
	{
		FILETIME fileTime = {};
		WIN_CHECK( !GetFileTime( this->hFile, 0, 0, &fileTime ) );

		ULARGE_INTEGER timestamp = {};
		timestamp.LowPart = fileTime.dwLowDateTime;
		timestamp.HighPart = fileTime.dwHighDateTime;

		return u64( timestamp.QuadPart ); 
	}
	virtual ~win32_mmaped_file_handle() override
	{
		UnmapViewOfFile( std::data( this->dataView ) );
		CloseHandle( this->hFileMapping );
		CloseHandle( this->hFile );
	}
};

std::unique_ptr<file> SysCreateFile( std::string_view path, file_permissions filePermissions )
{
	DWORD openFlags = 0;
	switch( filePermissions )
	{
	case file_permissions::READ:
		openFlags |= MMF_OPEN_FLAGS::READ;
		[[fallthrough]];
	case file_permissions::WRITE:
		openFlags |= MMF_OPEN_FLAGS::WRITE;
		break;
	}
	return std::make_unique<win32_mmaped_file_handle>(
		path, openFlags, ( DWORD ) MMF_CREATE_FLAGS::OPEN_IF_EXISTS, ( DWORD ) MMF_ACCESS_FLAGS::SEQUENTIAL );
}