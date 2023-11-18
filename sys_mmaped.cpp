#include "sys_os_api.h"
#include "sys_err.inl"

#include <span>
#include <string_view>
#include "pak_file.h"

/*
enum MMF_OPENFLAGS : u8
{
	OPN_READ = 0,
	OPN_READWRITE
};

// TODO: file system should ref count
struct win32_mmaped_file_handle
{
	HANDLE hFile = INVALID_HANDLE_VALUE;
	HANDLE hFileMapping = INVALID_HANDLE_VALUE;
	std::span<u8> dataView;

	~win32_mmaped_file_handle()
	{
		UnmapViewOfFile( std::data( dataView ) );
		CloseHandle( hFileMapping );
		CloseHandle( hFile );
	}
};

win32_mmaped_file_handle OpenMmappedFile( std::string_view fileName, MMF_OPENFLAGS oflags )
{
	DWORD dwflags = ( OPN_READWRITE == oflags ) ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ;
	HANDLE hFile = CreateFileA(
		std::data( fileName ), dwflags, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_FLAG_SEQUENTIAL_SCAN, 0 );
	WIN_CHECK( hFile == INVALID_HANDLE_VALUE );

	DWORD dwFileSizeHigh;
	size_t qwFileSize = GetFileSize( hFile, &dwFileSizeHigh );
	qwFileSize += ( size_t( dwFileSizeHigh ) << 32 );
	WIN_CHECK( qwFileSize == 0 );

	DWORD dwFlagsFileMapping = ( OPN_READWRITE == oflags ) ? PAGE_READWRITE : PAGE_READONLY;
	HANDLE hFileMapping = CreateFileMappingA( hFile, 0, dwFlagsFileMapping, 0, 0, 0 );
	WIN_CHECK( !hFileMapping );

	//m_dwflagsView = (OPN_WRITE == oflags || OPN_READWRITE == oflags)?
	//FILE_MAP_WRITE: FILE_MAP_READ;
	DWORD dwFlagsView = FILE_MAP_ALL_ACCESS;
	u8* pData = (u8*) MapViewOfFile( hFileMapping, dwFlagsView, 0, 0, qwFileSize );

	WIN_CHECK( !pData );

	return win32_mmaped_file_handle{ hFile, hFileMapping, std::span<u8>{pData, qwFileSize} };
}

struct win32_pak_filesystem
{	
	static constexpr u32 PAK_VERSION = 2;
	
	win32_mmaped_file_handle hFile;
	pak_file_header header;
	std::vector<pak_file_entry> entries;
};

win32_pak_filesystem MakeFilesystem() 
{
	return {};
}

/*
class win32_mmaped_file_io
{
private:
	BYTE cRefCount;
	MMF_OPENFLAGS eOpenflags;

	PBYTE pbFile = 0;
	DWORD dwBytesInView;
	i64 qwFileSize;
	i64 nViewBegin;//from begining of file
	i64 nCurPos;//from begining of file

	DWORD dwAllocGranularity;
	LONG lExtendOnWriteLength;
	DWORD dwflagsFileMapping;
	DWORD dwflagsView;
	bool bFileExtended;
	std::string_view strErrMsg;

	void _Flush();
	bool _CheckFileExtended();
	bool _Seek( i64 lOffset, MMF_SEEKPOS eseekpos );
	void _Close();

public:

	bool Open( std::string_view strfile, );
	bool Close();

	void ReadIntoView( std::span<u8> buffView );
	void WriteIntoView( std::span<u8> buffView );

	bool Seek( i64 lOffset, MMF_SEEKPOS eseekpos );
	u64 GetPosition();

	u64 GetLength();
	bool SetLength( i64 nLength );

	inline std::string_view GetMMFLastError() { return strErrMsg; }
};

#define MMF_ERR_ZERO_BYTE_FILE           std::string_view{"Cannot open zero byte file."}
#define MMF_ERR_INVALID_SET_FILE_POINTER std::string_view{"The file pointer cannot be set to specified location."}
#define MMF_ERR_WRONG_OPEN               std::string_view{"Close previous file before opening another."}
#define MMF_ERR_OPEN_FILE                std::string_view{"Error encountered during file open."}
#define MMF_ERR_CREATEFILEMAPPING        std::string_view{"Failed to create file mapping object."}
#define MMF_ERR_MAPVIEWOFFILE            std::string_view{"Failed to map view of file."}
#define MMF_ERR_SETENDOFFILE             std::string_view{"Failed to set end of file."}
#define MMF_ERR_INVALIDSEEK              std::string_view{"Seek request lies outside file boundary."}
#define MMF_ERR_WRONGSEEK                std::string_view{"Offset must be negative while seeking from file end."};

//~~~ CWinMMFIO implementation ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
win32_mmaped_file_io::win32_mmaped_file_io()
{
	SYSTEM_INFO sinf;
	GetSystemInfo( &sinf );
	this->dwAllocGranularity = sinf.dwAllocationGranularity;
	this->lExtendOnWriteLength = this->dwAllocGranularity;

	this->dwBytesInView = this->dwAllocGranularity;
}

void win32_mmaped_file_io::_Close()
{
	this->_CheckFileExtended();

	if( this->pbFile )
	{
		FlushViewOfFile( this->pbFile, 0 );
		UnmapViewOfFile( this->pbFile );
		this->pbFile = 0;
	}

	if( this->hFileMapping )
	{
		CloseHandle( this->hFileMapping );
		this->hFileMapping = INVALID_HANDLE_VALUE;
	}

	if( this->hFile )
	{
		CloseHandle( this->hFile );
		this->hFile = 0;
	}
}

win32_mmaped_file_io::~win32_mmaped_file_io()
{
	this->_Close();
}

bool win32_mmaped_file_io::Open( std::string_view strfile, MMF_OPENFLAGS oflags )
{

}

bool win32_mmaped_file_io::Close()
{
	assert( this->cRefCount == 1 );

	--this->cRefCount;
	this->_Close();
	return true;
}


void win32_mmaped_file_io::ReadIntoView( std::span<u8> buffView )
{
	size_t readCount = std::size( buffView );
	if( readCount == 0 )
	{
		return;
	}

	this->_CheckFileExtended();

	if( this->nCurPos >= this->qwFileSize )
	{
		return;
	}

	int nCount = readCount;//int is used to detect any bug

	this->dwBytesInView = this->dwAllocGranularity;
	//check if m_nViewBegin+m_dwBytesInView crosses filesize
	if( this->nViewBegin + this->dwBytesInView > this->qwFileSize )
	{
		this->dwBytesInView = this->qwFileSize - this->nViewBegin;
	}

	i64 nDataEndPos = this->nCurPos + nCount;
	if( nDataEndPos >= this->qwFileSize )
	{
		nDataEndPos = this->qwFileSize;
		nCount = this->qwFileSize - this->nCurPos;
	}

	assert( nCount >= 0 );//nCount is int, if -ve then error

	i64 nViewEndPos = this->nViewBegin + this->dwBytesInView;

	if( nDataEndPos < nViewEndPos )
	{
		memcpy_s( std::data( buffView ), readCount, this->pbFile + ( this->nCurPos - this->nViewBegin ), nCount );
		this->nCurPos += nCount;
	}
	else if( nDataEndPos == nViewEndPos )
	{
		//Last exact bytes are read from the view
		memcpy_s( std::data( buffView ), readCount, this->pbFile + ( this->nCurPos - this->nViewBegin ), nCount );
		this->nCurPos += nCount;

		this->_Seek( this->nCurPos, SP_BEGIN );
		nViewEndPos = this->nViewBegin + this->dwBytesInView;
	}
	else
	{
		LPBYTE pBufRead = LPBYTE( std::data( buffView ) );
		if( nDataEndPos > nViewEndPos )
		{
			//nDataEndPos can span multiple view blocks
			while( this->nCurPos < nDataEndPos )
			{
				int nReadBytes = nViewEndPos - this->nCurPos;

				if( nViewEndPos > nDataEndPos )
					nReadBytes = nDataEndPos - this->nCurPos;

				memcpy_s( pBufRead, readCount, this->pbFile + ( this->nCurPos - this->nViewBegin ), nReadBytes );
				pBufRead += nReadBytes;

				this->nCurPos += nReadBytes;
				//seeking does view remapping if m_nCurPos crosses view boundary
				this->_Seek( this->nCurPos, SP_BEGIN );
				nViewEndPos = this->nViewBegin + this->dwBytesInView;
			}
		}
	}
}

bool win32_mmaped_file_io::SetLength( i64 nLength )
{
	UnmapViewOfFile( this->pbFile );
	CloseHandle( this->hFileMapping );

	LONG nLengthHigh = ( nLength >> 32 );
	DWORD dwPtrLow = SetFilePointer( this->hFile, LONG( nLength & 0xFFFFFFFF ), &nLengthHigh, FILE_BEGIN );

	if( INVALID_SET_FILE_POINTER == dwPtrLow && GetLastError() != NO_ERROR )
	{
		this->strErrMsg = MMF_ERR_INVALID_SET_FILE_POINTER;
		return false;
	}
	if( SetEndOfFile( this->hFile ) == 0 )
	{
		this->strErrMsg = MMF_ERR_SETENDOFFILE;
		return false;
	}

	this->qwFileSize = nLength;
	this->hFileMapping = CreateFileMapping( this->hFile, 0, this->dwflagsFileMapping, 0, 0, "SMP" );
	this->pbFile = PBYTE( MapViewOfFile(
		this->hFileMapping, this->dwflagsView, DWORD( this->nViewBegin >> 32 ),
		DWORD( this->nViewBegin & 0xFFFFFFFF ), this->dwBytesInView ) );

	return true;
}

void win32_mmaped_file_io::WriteIntoView( std::span<u8> buffView )
{
	size_t writeCount = std::size( buffView );
	if( writeCount == 0 )
	{
		return;
	}

	i64 nViewEndPos = this->nViewBegin + this->dwBytesInView;
	i64 nDataEndPos = this->nCurPos + writeCount;

	if( nDataEndPos > nViewEndPos )
	{
		if( nDataEndPos >= this->qwFileSize )
		{
			//Extend the end position by m_lExtendOnWriteLength bytes
			i64 nNewFileSize = nDataEndPos + this->lExtendOnWriteLength;

			if( SetLength( nNewFileSize ) )
			{
				this->bFileExtended = true;
			}
		}

		LPBYTE pBufWrite = LPBYTE( std::data( buffView ) );
		while( this->nCurPos < nDataEndPos )
		{
			int nWriteBytes = nViewEndPos - this->nCurPos;

			if( nViewEndPos > nDataEndPos )
			{
				nWriteBytes = nDataEndPos - this->nCurPos;
			}

			memcpy_s( &this->pbFile[ this->nCurPos - this->nViewBegin ], this->dwBytesInView, pBufWrite, nWriteBytes );
			pBufWrite += nWriteBytes;

			this->nCurPos += nWriteBytes;
			//seeking does view remapping if m_nCurPos crosses view boundary
			this->_Seek( this->nCurPos, SP_BEGIN );
			nViewEndPos = this->nViewBegin + this->dwBytesInView;
		}
	}
	else
	{
		memcpy_s( &this->pbFile[ this->nCurPos - this->nViewBegin ], writeCount, std::data( buffView ), writeCount );
		this->nCurPos += writeCount;
	}
}

void win32_mmaped_file_io::_Flush()
{
}

bool win32_mmaped_file_io::Seek( i64 lOffset, MMF_SEEKPOS eseekpos )
{
	this->_CheckFileExtended();
	bool bRet = this->_Seek( lOffset, eseekpos );
	return bRet;
}

bool win32_mmaped_file_io::_Seek( i64 lOffset, MMF_SEEKPOS eseekpos )
{
	if( SP_CUR == eseekpos )
	{
		lOffset = this->nCurPos + lOffset;
	}
	else if( SP_END == eseekpos )
	{
		if( lOffset >= 0 )
		{
			this->strErrMsg = MMF_ERR_WRONGSEEK;
			return false;
		}

		//lOffset in RHS is -ve
		lOffset = this->qwFileSize + lOffset;
	}
	//else means SP_BEGIN


	//lOffset must be less than the file size
	if( !( lOffset >= 0 && lOffset < this->qwFileSize ) )
	{
		this->strErrMsg = MMF_ERR_INVALIDSEEK;
		return false;
	}

	if( !( lOffset >= this->nViewBegin && lOffset < this->nViewBegin + this->dwBytesInView ) )
	{
		//lOffset lies outside the mapped view, remap the view
		i64 _N = floor( float( lOffset ) / float( this->dwAllocGranularity ) );
		this->nViewBegin = _N * this->dwAllocGranularity;
		this->dwBytesInView = this->dwAllocGranularity;

		//check if m_nViewBegin+m_dwBytesInView crosses filesize
		if( this->nViewBegin + this->dwBytesInView > this->qwFileSize )
		{
			this->dwBytesInView = this->qwFileSize - this->nViewBegin;
		}

		if( this->dwBytesInView != 0 && this->pbFile )
		{
			UnmapViewOfFile( this->pbFile );
			this->pbFile = PBYTE( MapViewOfFile(
				this->hFileMapping, this->dwflagsView, DWORD( this->nViewBegin >> 32 ),
				DWORD( this->nViewBegin & 0xFFFFFFFF ), this->dwBytesInView ) );

			//DWORD err = GetLastError();
		}
	}

	this->nCurPos = lOffset;
	return true;
}

u64 win32_mmaped_file_io::GetLength()
{
	this->_CheckFileExtended();
	return this->qwFileSize;
}

u64 win32_mmaped_file_io::GetPosition()
{
	return this->nCurPos;
}

/*
If file is extended in Write function then this must be called to re-adjust
the file to its actual length before Seek or Read or any such fuction.

bool win32_mmaped_file_io::_CheckFileExtended()
{
	bool bRet = true;
	if( this->bFileExtended )
	{
		//remove extra bytes
		bRet = SetLength( this->nCurPos );
	}
	this->bFileExtended = false;
	return bRet;
}
*/