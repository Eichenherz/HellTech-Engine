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
# Compiler & linker flags  (MSVC-specific)
# ============================================================================

if(MSVC)
    # Runtime library: /MDd Debug, /MD Release+RelWithDebInfo  (UseDebugLibraries in every vcxproj)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

    # --- compiler ---
    add_compile_options(
        /W3             # Warning level 3
        /MP             # Multi-processor compilation
        /arch:AVX2      # AVX2 intrinsics
        /sdl            # SDL security checks
        /permissive-    # Conformance mode
        /diagnostics:caret
        /GR-            # No RTTI
        /EHs-c-         # No exceptions

        # Compiler-level PDB for Debug. Release/RelWithDebInfo get theirs below.
        $<$<CONFIG:Debug>:/Zi>

        # ---- Optimized configs: everything MSVC has, short of unsafe ----
        # Release and RelWithDebInfo generate IDENTICAL code — same flags, same inlining, same
        # LTCG. RelWithDebInfo only adds the symbols to step through it, so a number measured in
        # one is valid in the other.
        # CMake already supplies /O2 (/Ob2 in Release, /Zi /Ob1 in RelWithDebInfo) and /DNDEBUG;
        # the flags below add to or override that. /Ot and /Gy come free with /O2.
        $<$<CONFIG:Release,RelWithDebInfo>:/Ob3>       # more aggressive inlining than /Ob2, and it overrides RelWithDebInfo's /Ob1
        $<$<CONFIG:Release,RelWithDebInfo>:/Oi>        # intrinsics instead of CRT calls
        $<$<CONFIG:Release,RelWithDebInfo>:/GL>        # whole-program optimization (pairs with /LTCG)
        $<$<CONFIG:Release,RelWithDebInfo>:/Gw>        # global data COMDATs, so the linker can drop unused ones
        $<$<CONFIG:Release,RelWithDebInfo>:/Zc:inline> # strip unreferenced COMDATs, smaller I-cache footprint
        $<$<CONFIG:Release,RelWithDebInfo>:/fp:fast>   # reassociation, FMA contraction, no strict IEEE
        $<$<CONFIG:Release,RelWithDebInfo>:/sdl->      # must precede /GS-, else the global /sdl silently wins
        $<$<CONFIG:Release,RelWithDebInfo>:/GS->       # no stack cookies in hot loops

        # Debuggable optimized code: /Zi in Release too, so a crash in a benchmark run is
        # readable. Symbols live in the PDB, not the exe — zero runtime cost.
        $<$<CONFIG:Release>:/Zi>
    )

    add_link_options(
        /DEBUG                          # Linker PDB — all configs
        $<$<CONFIG:Release,RelWithDebInfo>:/LTCG>
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:REF>
        $<$<CONFIG:Release,RelWithDebInfo>:/OPT:ICF>
        $<$<CONFIG:Release,RelWithDebInfo>:/INCREMENTAL:NO>   # incremental linking cannot coexist with LTCG
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
# Include-only libs  (Lib, all 3rdParty dirs from CommonBuildSheet.props)
# HtLib is NOT include-only — it declares its own target in HtLib/CMakeLists.txt
# and hands consumers its .cpps as well. Added under Subdirectories below.
# ============================================================================

add_library(Lib INTERFACE)
target_include_directories(Lib INTERFACE "${CMAKE_SOURCE_DIR}/Lib")

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

# Declares the HtLib target (includes + its .cpps). Must come before CommonDeps
# links it, and before link_libraries() so HtLib itself stays dependency-free.
add_subdirectory(HtLib)

# Single bundle — Engine + HellPack inherit this automatically via link_libraries() below
add_library(CommonDeps INTERFACE)
target_link_libraries(CommonDeps INTERFACE Lib HtLib ThirdPartyIncludes)

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