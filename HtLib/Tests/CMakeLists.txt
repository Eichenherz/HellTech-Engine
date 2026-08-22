set(HTLIB_DIR   "..")
set(MINUNIT_DIR "../../3rdParty/minunit")

# Shared include dirs for all test exes
set(HTLIB_TEST_INCLUDES
    ${HTLIB_DIR}           # <ht_memory.h>, <System/sys_sync.h>, etc.
    ${MINUNIT_DIR}         # "minunit.h"
)

# HtLib's own translation units, read straight off the HtLib target so this can
# never drift from what the exes compile.
# Tests do NOT link HtLib: they rebuild the same sources with HT_TESTS, which
# swaps HT_ASSERT for the setjmp trap MU_ASSERT_FIRES needs.
get_target_property(HTLIB_SRC HtLib INTERFACE_SOURCES)

function(add_ht_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN} ${HTLIB_SRC})
    target_include_directories(${TARGET_NAME} PRIVATE ${HTLIB_TEST_INCLUDES})
    target_compile_definitions(${TARGET_NAME} PRIVATE HT_TESTS _CONSOLE)
endfunction()

# ============================================================================
# Test executables  (1 per test file, matching VS projects)
# ============================================================================

#add_ht_test(TestSlotBuffer     test_slot_buffer.cpp)
#add_ht_test(TestStretchyBuffer test_stretchy_buffer.cpp)
add_ht_test(TestHtMemory       test_ht_memory.cpp)
add_ht_test(TestFixedVector    test_fixed_vector.cpp)