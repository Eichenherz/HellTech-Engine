#pragma once

#ifndef __HT_ERROR_H__
#define __HT_ERROR_H__

#include "sys_os_api.h"
#include <format>
#include <cstdlib>

//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE( x ) STRINGIZE2( x )
#define STRINGIZE2( x ) #x
#define LINE_STR STRINGIZE( __LINE__ )

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__

constexpr u64 HT_LOG_BUFFER_SIZE = 2048;

template<typename... Args>
__forceinline void HtPrintErrAndDie( std::format_string<Args...> fmt, Args&&... args )
{
	char dbgStr[ HT_LOG_BUFFER_SIZE ] = {};
	std::format_to_n( dbgStr, std::size( dbgStr ) - 1, fmt, std::forward<Args>( args )... );
	SysErrMsgBox( dbgStr );
	std::abort();
}

#define HT_ASSERT( boolExpr )														\
do{																					\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR;						\
	if( !( boolExpr ) ) HtPrintErrAndDie( "{}", DEV_ERR_STR );						\
}while( 0 )

#endif // !__HT_ERROR_H__
