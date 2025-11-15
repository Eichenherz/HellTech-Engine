# MSVC debug-specific flags and defines

# Preprocessor definitions
set( MSVC_DEBUG_DEFINES
        _CRT_SECURE_NO_WARNINGS
        _DEBUG
        _WIN32
        _VK_DEBUG_
        _MBCS
        _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
        _SILENCE_CXX20_OLD_SHARED_PTR_DEPRECATION_WARNING
        _SILENCE_CXX20_ATOMIC_INIT_DEPRECATION_WARNING
)

# Debug compiler options
set( MSVC_DEBUG_CFLAGS
        /ZI
        /Od
        /RTC1
)
