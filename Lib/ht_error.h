#pragma once

#ifndef __HT_ERROR_H__
#define __HT_ERROR_H__

#include "core_types.h"

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

#ifdef HT_TESTS

#include <setjmp.h>

extern jmp_buf  gHtAssertJmpbuf;
extern i32      gHtAssertFired;

#define HT_ASSERT( boolExpr )                                     \
 do{                                                              \
     if( !( boolExpr ) )                                          \
	 {                                                            \
		 gHtAssertFired = 1;                                      \
         longjmp( gHtAssertJmpbuf, 1 );                           \
	 }                                                            \
 }while( 0 )

#else // !HT_TESTS

#if defined(_WIN32) && !defined(_CONSOLE)
void SysErrMsgBox( const char* str );
#else
#include <iostream>
__forceinline void SysErrMsgBox( const char* str )
{
	std::cout << str << "\n";
}
#endif

template<u64 BUFFER_SIZE = HT_LOG_BUFFER_SIZE, typename... Args>
__forceinline void HtPrintErrAndDie( std::format_string<Args...> fmt, Args&&... args )
{
	char dbgStr[ BUFFER_SIZE ] = {};
	std::format_to_n( dbgStr, std::size( dbgStr ) - 1, fmt, std::forward<Args>( args )... );
	SysErrMsgBox( dbgStr );
	std::abort();
}

#define HT_ASSERT( boolExpr )														\
do{																					\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR;						\
	if( !( boolExpr ) ) HtPrintErrAndDie( "{}", DEV_ERR_STR );						\
}while( 0 )

#endif // HT_TESTS

#endif // !__HT_ERROR_H__
