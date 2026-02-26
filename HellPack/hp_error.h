#ifndef __HP_ERROR_H__
#define __HP_ERROR_H__

#include <string>
#include <cstdio>
#include <cstdlib>

//////////////////////////////////////
// MACROS
//////////////////////////////////////
#define STRINGIZE(x) STRINGIZE2(x)
#define STRINGIZE2(x) #x
#define LINE_STR STRINGIZE(__LINE__)

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__

#define HP_ASSERT( boolExpr )														\
do{																					\
	constexpr char DEV_ERR_STR[] = RUNTIME_ERR_LINE_FILE_STR;						\
	if( !( boolExpr ) )                                                             \
	{															                    \
		std::fputs( DEV_ERR_STR, stderr );									        \
		std::abort();																\
	}																				\
}while( 0 )

#endif // !__HP_ERROR_H__