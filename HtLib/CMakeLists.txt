# ============================================================================
# HtLib — headers + its own translation units.
#
# No library is built. Consumers link the INTERFACE target
#
# ============================================================================

add_library(HtLib INTERFACE)

target_include_directories(HtLib INTERFACE "${CMAKE_CURRENT_LIST_DIR}")

# Sources are globbed, never listed by name — drop a .cpp in HtLib/ and it is
# picked up. CONFIGURE_DEPENDS re-globs at build time, so no manual reconfigure.
file(GLOB_RECURSE HTLIB_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
)

# Tests/ builds its own exes and must never leak into consumers.
# System/ is per platform, so it is dropped wholesale and re-added below.
list(FILTER HTLIB_SRC EXCLUDE REGEX "/(Tests|System)/")

if(WIN32)
    set(HTLIB_PLATFORM_DIR "Win32")
else()
    set(HTLIB_PLATFORM_DIR "Linux")
endif()

file(GLOB_RECURSE HTLIB_PLATFORM_SRC CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/System/${HTLIB_PLATFORM_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/System/${HTLIB_PLATFORM_DIR}/*.c"
)
list(APPEND HTLIB_SRC ${HTLIB_PLATFORM_SRC})

# Absolute paths are required: entries in INTERFACE_SOURCES are resolved
# against the *consuming* target's directory, not this one.
target_sources(HtLib INTERFACE ${HTLIB_SRC})