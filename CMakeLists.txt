cmake_minimum_required(VERSION 3.30)

project(HellTech LANGUAGES CXX C)

# ============================================================================
# Guards
# ============================================================================

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Only 64-bit builds are supported.")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "HellTech is Windows-only.")
endif()

# ============================================================================
# Build configurations
# ============================================================================

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;RelWithDebInfo" CACHE STRING "" FORCE)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug;Release;RelWithDebInfo")
endif()

# ============================================================================
# Language standards
# ============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_DISABLE_PRECOMPILE_HEADERS ON)

# ============================================================================
# Output directories
#   bin/<Config>/  — executables + PDBs
#   bin/SpirV/     — compiled shaders (written by run_dxc_compile.bat / glslangValidator)
#   build/         — all intermediates (CMake binary dir; pass -B build at configure time)
# ============================================================================

foreach(_cfg Debug Release RelWithDebInfo)
    string(TOUPPER ${_cfg} _CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_CFG} "${CMAKE_SOURCE_DIR}/bin/${_cfg}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${_CFG}     "${CMAKE_SOURCE_DIR}/bin/${_cfg}")
endforeach()

# ============================================================================
# 3rdParty include dirs  (all 3rdParty dirs from CommonBuildSheet.props)
# ============================================================================

set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/3rdParty")
add_library(ThirdPartyIncludes INTERFACE)
target_include_directories(ThirdPartyIncludes INTERFACE
    ${THIRD_PARTY}/VulkanMemoryAllocator/include
    ${THIRD_PARTY}/imgui
    ${THIRD_PARTY}/ImGuiFileDialog
    ${THIRD_PARTY}/dds
    ${THIRD_PARTY}/unordered_dense/include
    ${THIRD_PARTY}/miniz
    ${THIRD_PARTY}/bc7enc_rdo
    ${THIRD_PARTY}/meshoptimizer/src
    ${THIRD_PARTY}/cgltf
    ${THIRD_PARTY}/minunit
    ${THIRD_PARTY}/OffsetAllocator
)

# ============================================================================
# Compiler & linker flags  (MSVC-specific)
# ============================================================================

if(MSVC)
    # Runtime library: /MDd Debug, /MT Release+RelWithDebInfo ( static CRT: LTCG inlines CRT calls,
    # no import thunks ). Directory-scoped, so HellPack3rdParty picks up the same one as the exes.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:DebugDLL>")

    # No /RTC1 in Debug: frame paint + stack verify on every call, and it's incompatible with /O2.
    # Uninit reads / overruns are covered by asserts, arena poisoning and ASan.
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG}")

    # --- flag sets, declared once; HellPack/CMakeLists.txt reuses them for HellPack3rdParty ---
    set(HT_MSVC_COMMON_FLAGS
        /W3             # Warning level 3
        /MP             # Multi-processor compilation
        /arch:AVX2      # AVX2 intrinsics
        /sdl-           # no SDL checks: strict /GS, warnings-as-errors bundle, ptr sentinels. Must precede /GS-
        /GS-            # no stack cookies: they land on every function with an inline_array on the stack
        /permissive-    # Conformance mode
        /diagnostics:caret
        /GR-            # No RTTI
        /EHs-c-         # No exceptions
    )
    # ---- Optimized configs: everything MSVC has, short of unsafe ( /GL is separate: LTCG objs
    #      can't link into a non-LTCG exe, so HellPack3rdParty must not take it ) ----
    # Release and RelWithDebInfo generate IDENTICAL code — same flags, same inlining, same
    # LTCG. RelWithDebInfo only adds the symbols to step through it, so a number measured in
    # one is valid in the other. Release is the shipped build: no PDB, no debug info at all.
    # CMake already supplies /O2 (/Ob2 in Release, /Zi /Ob1 in RelWithDebInfo) and /DNDEBUG;
    # the flags below add to or override that. /Ot and /Gy come free with /O2.
    set(HT_MSVC_OPT_FLAGS
        /Ob3            # more aggressive inlining than /Ob2, and it overrides RelWithDebInfo's /Ob1
        /Oi             # intrinsics instead of CRT calls
        /Gw             # global data COMDATs, so the linker can drop unused ones
        /Zc:inline      # strip unreferenced COMDATs, smaller I-cache footprint
        /fp:fast        # reassociation, FMA contraction, no strict IEEE
    )
    # Debug info for optimized code: /Zo tracks locals / inlined frames through /O2. PDB-only, no codegen change.
    set(HT_MSVC_OPT_DBGINFO_FLAGS /Zi /Zo)

    # Debug STL ABI — must match across EVERY TU in a binary, HellPack3rdParty included.
    # _ITERATOR_DEBUG_LEVEL=1: keeps range checks, drops the level 2 iterator ownership tracking ( + its lock ).
    # _ANNOTATE_*: ASan TUs stamp their objs detect_mismatch( "annotate_string/vector", "1" ); a non-ASan
    #   TU stamps "0" -> LNK2038 ( __msvc_sanitizer_annotate_container.hpp:101 ). _ANNOTATE_* defines
    #   _INSERT_*_ANNOTATION without _ACTIVATE_*: no stamp either way, so mixed TUs link. The inserted
    #   hooks resolve through the ASan runtime that the instrumented objs bring in.
    set(HT_MSVC_DEBUG_ABI_DEFS _ITERATOR_DEBUG_LEVEL=1 _ANNOTATE_STRING _ANNOTATE_VECTOR)

    # --- compiler ---
    add_compile_options(
        ${HT_MSVC_COMMON_FLAGS}
        $<$<CONFIG:Debug>:/Zi>
        "$<$<CONFIG:Release,RelWithDebInfo>:${HT_MSVC_OPT_FLAGS}>"
        $<$<CONFIG:Release,RelWithDebInfo>:/GL>        # whole-program optimization (pairs with /LTCG)
        "$<$<CONFIG:RelWithDebInfo>:${HT_MSVC_OPT_DBGINFO_FLAGS}>"
    )

    add_link_options(
        $<$<CONFIG:Debug,RelWithDebInfo>:/DEBUG>       # Linker PDB — never for the shipped Release
        $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF>
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:ICF>
        $<$<CONFIG:Release,RelWithDebInfo>:/INCREMENTAL:NO>   # incremental linking cannot coexist with LTCG
        $<$<CONFIG:Debug>:/INCREMENTAL:NO>                    # nor with ASan ( per-TU in Engine / HellPack )
    )
endif()

# ============================================================================
# Preprocessor defines
# ============================================================================

add_compile_definitions(
    _WIN32
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    $<$<CONFIG:RelWithDebInfo>:NDEBUG>
)

# ============================================================================
# Include-only libs  (Lib; ThirdPartyIncludes is declared at the top)
# HtLib is NOT include-only — it declares its own target in HtLib/CMakeLists.txt
# and hands consumers its .cpps as well. Added under Subdirectories below.
# ============================================================================

add_library(Lib INTERFACE)
target_include_directories(Lib INTERFACE "${CMAKE_SOURCE_DIR}/Lib")

# Declares the HtLib target (includes + its .cpps). Must come before CommonDeps
# links it, and before link_libraries() so HtLib itself stays dependency-free.
add_subdirectory(HtLib)

# Single bundle — Engine + HellPack inherit this automatically via link_libraries() below
add_library(CommonDeps INTERFACE)
target_link_libraries(CommonDeps INTERFACE Lib HtLib ThirdPartyIncludes)
# NOTE: arena borders / poison-on-rewind in ht_mem_arena.h; scoped here rather than
#       add_compile_definitions so the /O2 3rdParty lib ( no ASan ) never sees it.
target_compile_definitions(CommonDeps INTERFACE
    $<$<CONFIG:Debug>:__HT_USE_ASAN__>
    "$<$<CONFIG:Debug>:${HT_MSVC_DEBUG_ABI_DEFS}>"
)

# ============================================================================
# Standalone make-style targets (no build dependency)
# ============================================================================

add_custom_target(prepare
    COMMAND cmd /c "\"${CMAKE_SOURCE_DIR}/install_deps.bat\""
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Cloning 3rdParty deps and fetching miniz..."
    VERBATIM
)

# ============================================================================
# shaders — compile HLSL to SPIR-V via run_dxc_compile.bat  ( -> bin/SpirV/)
# ============================================================================

add_custom_target(shaders
    COMMAND cmd /c "\"${CMAKE_SOURCE_DIR}/run_dxc_compile.bat\""
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Compiling HLSL shaders to bin/SpirV/..."
    VERBATIM
)

# ============================================================================
# Subdirectories
# HtLib/Tests is added before link_libraries so its test targets don't inherit
# CommonDeps — they rebuild HtLib's sources themselves under HT_TESTS.
# Engine + HellPack come after and get CommonDeps automatically.
# ============================================================================

add_subdirectory(HtLib/Tests)

link_libraries(CommonDeps)   # everything added after this inherits CommonDeps

add_subdirectory(HellPack)
add_subdirectory(Engine)