cmake_minimum_required(VERSION 3.25)
project(HtLib LANGUAGES CXX C)

set(HTLIB_DIR  "${CMAKE_CURRENT_SOURCE_DIR}")
# TODO: fetch minunit ourselves (FetchContent or git submodule) instead of relying on root 3rdParty
set(MINUNIT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdParty/minunit")

# Shared include dirs for all test exes
set(HTLIB_TEST_INCLUDES
    ${HTLIB_DIR}           # <ht_slot_buffer.h>, <ht_mem_arena.h>, etc.
    ${HTLIB_DIR}/..        # "HtLib/ht_mem_arena.h" used by win32_impl.cpp
    ${HTLIB_DIR}/Win32     # "DEFS_WIN32_NO_BS.h" used by test_common.h
    ${MINUNIT_DIR}         # "minunit.h"
    ${HTLIB_DIR}/Tests     # "test_common.h"
)

# Win32 platform implementation — compiled into every test that needs OS services
set(HTLIB_WIN32_IMPL ${HTLIB_DIR}/Win32/win32_impl.cpp)

function(add_ht_test TARGET_NAME)
    add_executable(${TARGET_NAME} ${ARGN})
    target_include_directories(${TARGET_NAME} PRIVATE ${HTLIB_TEST_INCLUDES})
    target_compile_definitions(${TARGET_NAME} PRIVATE HT_TESTS _CONSOLE)
endfunction()

# ============================================================================
# Test executables  (1 per test file, matching VS projects)
# ============================================================================

add_ht_test(TestSlotBuffer     Tests/test_slot_buffer.cpp     ${HTLIB_WIN32_IMPL})
add_ht_test(TestStretchyBuffer Tests/test_stretchy_buffer.cpp ${HTLIB_WIN32_IMPL})
add_ht_test(TestMemArena       Tests/test_mem_arena.cpp       ${HTLIB_WIN32_IMPL})
