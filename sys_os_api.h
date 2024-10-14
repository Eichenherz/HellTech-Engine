#pragma once

#include "core_types.h"
#include <vector>
#include "hell_log.hpp"
#define BYTE_COUNT( buffer ) (u64) std::size( buffer ) * sizeof( buffer[ 0 ] )

#define GB (u64)( 1 << 30 )
#define KB (u64)( 1 << 10 )
#define MB (u64) ( 1 << 20 )

// TODO: move
struct gpu_data
{
	float timeMs;
};


struct frame_data;
namespace std
{
	template<class, class> class vector;
}

//////////////////////////////////////
// CONSTS
//////////////////////////////////////


// TODO: remove extern ? 
//////////////////////////////////////
// ENGINE -> PLATFORM
//////////////////////////////////////

//////////////////////////////////////
// PLATFORM -> ENGINE
//////////////////////////////////////

// FILE API--------------------------
extern u32		SysGetFileAbsPath( const char* fileName, char* buffer, u64 buffSize );
extern std::vector<u8> SysReadFile( const char* fileName );
extern u64		SysGetFileTimestamp( const char* filename );
extern bool		SysWriteToFile( const char* filename, const u8* data, u64 sizeInBytes );