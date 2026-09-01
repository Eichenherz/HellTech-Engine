#pragma once

#ifndef __SYS_STD_STREAMS_H__
#define __SYS_STD_STREAMS_H__

enum class sys_stream_t { OUTPUT, ERR };

#if defined(_WIN32)

void SysWriteToStdStream( const char* str, sys_stream_t streamType );
void SysErrMsgBox( const char* str );

#elif !defined(_CONSOLE)

#include <iostream>
inline void SysWriteToStdStream( const char* str, sys_stream_t streamType ) { std::cout << str; }
inline void SysErrMsgBox( const char* str ) { return SysWriteToStdStream( str, sys_stream_t::OUTPUT ); }

#endif

#endif //!__SYS_STD_STREAMS_H__