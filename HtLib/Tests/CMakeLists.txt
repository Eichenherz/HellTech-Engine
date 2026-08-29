set(HTLIB_DIR     "..")
set(MINUNIT_DIR   "../../3rdParty/minunit")
set(HASH_MAP      "../../3rdParty/unordered_dense/include")

# Shared include dirs for all test exes.
# The tests do not link HtLib, so its INTERFACE include dirs never reach them —
# every path HtLib's headers need has to be repeated here.
set(HTLIB_TEST_INCLUDES
    ${HTLIB_DIR}           # <ht_memory.h>, <System/sys_sync.h>, etc.
    ${MINUNIT_DIR}         # "minunit.h"
    ${HASH_MAP}            # <ankerl/unordered_dense.h>, pulled in by ht_memory.h
)

# HtLib's own translation units, read straight off the HtLib target so this can
# never drift from what the exes compile.
# Tests do NOT link HtLib: they rebuild the same sources with HT_TESTS, which
# swaps HT_ASSERT for the setjmp trap MU_ASSERT_FIRES needs.
get_target_property(HTLIB_SRC HtLib INTERFACE_SOURCES)

# INLINED_TU names HtLib .cpp files the test #includes itself to reach the helpers that TU keeps
# file local. Those have to be dropped here or every symbol in them lands in the exe twice.
function(add_ht_test TARGET_NAME)
    cmake_parse_arguments(HT_TEST "" "" "INLINED_TU" ${ARGN})

    set(TEST_HTLIB_SRC ${HTLIB_SRC})
    foreach(INLINED_TU IN LISTS HT_TEST_INLINED_TU)
        list(FILTER TEST_HTLIB_SRC EXCLUDE REGEX "/${INLINED_TU}$")
    endforeach()

    add_executable(${TARGET_NAME} ${HT_TEST_UNPARSED_ARGUMENTS} ${TEST_HTLIB_SRC})
    target_include_directories(${TARGET_NAME} PRIVATE ${HTLIB_TEST_INCLUDES})
    target_compile_definitions(${TARGET_NAME} PRIVATE HT_TESTS _CONSOLE)
endfunction()

# Stress exes are not unit tests, so no HT_TESTS: HT_ASSERT has to abort into the debugger instead of
function(add_ht_stress TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN} ${HTLIB_SRC})
    target_include_directories(${TARGET_NAME} PRIVATE ${HTLIB_TEST_INCLUDES})
    target_compile_definitions(${TARGET_NAME} PRIVATE _CONSOLE HT_BLOCK_SZ_IN_BYTES=65536)
endfunction()

# ============================================================================
# Test executables  (1 per test file, matching VS projects)
# ============================================================================

#add_ht_test(TestSlotBuffer             test_slot_buffer.cpp)
#add_ht_test(TestStretchyBuffer         test_stretchy_buffer.cpp)
add_ht_test(TestHtMemory                test_ht_memory.cpp INLINED_TU ht_allocator.cpp)
add_ht_test(TestFixedVector             test_fixed_vector.cpp)

add_ht_stress(StressTestHtAllocator    stress_test_ht_allocator.cpp )