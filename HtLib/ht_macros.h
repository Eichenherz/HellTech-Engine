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

// NOTE: from https://www.foonathan.net/2020/09/move-forward/
// static_cast to rvalue reference
#define MOV(...) static_cast<std::remove_reference_t<decltype(__VA_ARGS__)>&&>(__VA_ARGS__)

// static_cast to identity
// The extra && aren't necessary as discussed above, but make it more robust in case it's used with a non-reference.
#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

#define BYTE_COUNT( buffer ) std::size( buffer ) * sizeof( buffer[ 0 ] )

#endif // !__HT_MACROS_H__