#include <cstddef>
#include <cstdlib>

#if defined(_MSC_VER)

[[noreturn]] static __forceinline void EA_terminate_oom() noexcept { std::abort(); }

#elif defined(__GNUC__) || defined(__clang__)

[[noreturn]] static __attribute__( ( always_inline ) ) inline void EA_terminate_oom() noexcept { __builtin_trap(); }

#else

[[noreturn]] static inline void EA_terminate_oom() noexcept { std::abort(); }

#endif

static inline void* EA_alloc( size_t n ) noexcept { return std::malloc( n ); }
static inline void  EA_free( void* p ) noexcept { std::free( p ); }

void* operator new( size_t size,
    const char*, int, unsigned,
    const char*, int ) noexcept
{
    if (void* p = EA_alloc(size)) return p;
    EA_terminate_oom();
}

void operator delete( void* p,
    const char*, int, unsigned,
    const char*, int ) noexcept
{
    EA_free(p);
}

void* operator new[]( size_t size,
    const char*, int, unsigned,
    const char*, int ) noexcept
{
    if (void* p = EA_alloc(size)) return p;
    EA_terminate_oom();
}

void operator delete[]( void* p,
    const char*, int, unsigned,
    const char*, int ) noexcept
{
    EA_free(p);
}