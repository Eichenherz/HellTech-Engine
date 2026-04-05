#ifndef __HT_MACROS_H__
#define __HT_MACROS_H__

#if defined(_MSC_VER)
#define HT_FORCEINLINE __forceinline

#elif defined(__clang__)
#define HT_FORCEINLINE __attribute__((always_inline))

#endif

#define STRINGIZE( x ) STRINGIZE2( x )
#define STRINGIZE2( x ) #x
#define LINE_STR STRINGIZE( __LINE__ )

#define RUNTIME_ERR_LINE_FILE_STR ">>>RUNTIME_ERROR<<<\nLine: " LINE_STR", File: " __FILE__

#endif // !__HT_MACROS_H__