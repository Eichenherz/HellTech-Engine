#pragma once

#ifndef __HT_FILE_H__
#define __HT_FILE_H__

#include <vector>
#include <span>

#include <stdio.h>

#include "ht_core_types.h"
#include "ht_error.h"

// NOTE: we use C lib here bc we're lazy and there's no point in using platform specific stuff for this
inline void WriteFileBinary( const char* path, std::span<const u8> bytes )
{
    FILE* f = nullptr;
    HT_ASSERT( ::fopen_s( &f, path, "wb" ) == 0 );

    u64 written = ::fwrite( std::data( bytes ), 1, std::size( bytes ), f );
    HT_ASSERT( std::size( bytes ) == written );

    i32 rc = ::fclose( f );
    HT_ASSERT( rc == 0 );
}

inline std::vector<u8> ReadFileBinary( const char* path )
{
    FILE* f = nullptr;
    HT_ASSERT( ::fopen_s( &f, path, "rb" ) == 0 );
    HT_ASSERT( f );

    HT_ASSERT( ::fseek( f, 0, SEEK_END ) == 0 );
    i32 sz = ::ftell( f );
    HT_ASSERT( sz >= 0 );
    HT_ASSERT( ::fseek( f, 0, SEEK_SET ) == 0 );

    std::vector<u8> out( sz );
    u64 read = ::fread( std::data( out ), 1, std::size( out ), f );
    HT_ASSERT( std::size( out ) == read );

    HT_ASSERT( ::fclose( f ) == 0 );
    return out;
}

#endif //!__HT_FILE_H__