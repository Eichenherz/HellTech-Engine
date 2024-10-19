#ifndef __MACROS__
#define __MACROS__

//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STR STRINGIZE(__LINE__)

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__

#include "core_types.h"

#define BYTE_COUNT( buffer ) (u64) std::size( buffer ) * sizeof( buffer[ 0 ] )

#define GB (u64)( 1 << 30 )
#define KB (u64)( 1 << 10 )
#define MB (u64) ( 1 << 20 )

#endif // !__MACROS__
