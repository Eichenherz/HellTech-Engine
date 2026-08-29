#pragma once

#ifndef __HT_MACROS_H__
#define __HT_MACROS_H__

#if defined(_MSC_VER)
#define HT_FORCEINLINE __forceinline

#define EMBED_TYPE [[msvc::no_unique_address]]

#elif defined(__clang__)
#define HT_FORCEINLINE __attribute__((always_inline))

#else
#define EMBED_TYPE [[no_unique_address]]

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

// defer — block-scoped, immovable, zero-overhead. Runs at end of enclosing scope.
// Usage: defer { Foo(); };
template<typename F>
struct __ht_defer_guard
{
    F f;

    explicit __ht_defer_guard( F&& fn ) : f{ MOV( fn ) } {}
            ~__ht_defer_guard() noexcept { f(); }

            __ht_defer_guard( const __ht_defer_guard& )            = delete;
            __ht_defer_guard( __ht_defer_guard&& )                 = delete;
            __ht_defer_guard& operator=( const __ht_defer_guard& ) = delete;
            __ht_defer_guard& operator=( __ht_defer_guard&& )      = delete;
};

struct __ht_defer_tag {};

template<typename F>
__ht_defer_guard<F> operator->*( __ht_defer_tag, F&& f )
{
    return __ht_defer_guard<F>( MOV( f ) );
}

#define HT_CONCAT_( a, b ) a##b
#define HT_CONCAT( a, b )  HT_CONCAT_( a, b )
#define defer auto HT_CONCAT( __ht_defer_, __COUNTER__ ) = __ht_defer_tag{} ->* [ & ]() noexcept

struct ht_no_copy
{
    ht_no_copy() = default;
    ht_no_copy( const ht_no_copy& ) = delete;
    ht_no_copy& operator=( const ht_no_copy& ) = delete;
};

struct ht_no_move
{
    ht_no_move() = default;
    ht_no_move( ht_no_move&& ) = delete;
    ht_no_move& operator=( ht_no_move&& ) = delete;
};

// NOTE: use composition to avoid fucking the designated initialization
#define NO_COPY()  [[no_unique_address]] ht_no_copy _noCopy = {}
#define NO_MOVE()  [[no_unique_address]] ht_no_move _noMove = {}

#endif // !__HT_MACROS_H__