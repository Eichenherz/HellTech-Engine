#include <cstddef>
#include <cstdlib>
#include "ht_utils.h"

#if defined(_MSC_VER)

[[noreturn]] static __forceinline void EA_terminate_oom() noexcept { std::abort(); }

#elif defined(__GNUC__) || defined(__clang__)

[[noreturn]] static __attribute__( ( always_inline ) ) inline void EA_terminate_oom() noexcept { __builtin_trap(); }

#else

[[noreturn]] static inline void EA_terminate_oom() noexcept { std::abort(); }

#endif

static inline void* EA_alloc( size_t n ) noexcept { return std::malloc( n ); }
static inline void  EA_free( void* p ) noexcept { std::free( p ); }
static inline void* EA_alloc_aligned_offset( size_t size, size_t alignment, size_t alignmentOffset ) noexcept
{
    const size_t header = sizeof( void* );
    const size_t extra = header + alignment + alignmentOffset;

    void* raw = EA_alloc( size + extra );
    if( !raw ) return nullptr;

    uintptr_t base = ( uintptr_t ) raw + header;
    uintptr_t aligned_plus = FwdAlign( base + alignmentOffset, alignment );
    uintptr_t aligned = aligned_plus - alignmentOffset;

    void** stored = ( void** ) aligned - 1;
    *stored = raw;

    return ( void* ) aligned;
}
static inline void EA_free_aligned_offset( void* p ) noexcept
{
    if( !p ) return;
    void* raw = *( ( void** ) p - 1 );
    EA_free( raw );
}

void* operator new( size_t size, const char*, int, unsigned, const char*, int )
{
    if( void* p = EA_alloc( size ) ) return p;
    EA_terminate_oom();
}
void operator delete( void* p, const char*, int, unsigned, const char*, int )
{
    EA_free( p );
}
void* operator new[]( size_t size, const char*, int, unsigned, const char*, int )
{
    if( void* p = EA_alloc( size ) ) return p;
    EA_terminate_oom();
}
void operator delete[]( void* p, const char*, int, unsigned, const char*, int )
{
    EA_free( p );
}
void* operator new[]( size_t size, size_t alignment, size_t alignmentOffset, const char*, int, unsigned, const char*, int )
{
    if( void* p = EA_alloc_aligned_offset( size, alignment, alignmentOffset ) ) return p;
    EA_terminate_oom();
}
void operator delete[]( void* p, size_t, size_t, const char*, int, unsigned, const char*, int )
{
    EA_free_aligned_offset( p );
}