cmake_minimum_required(VERSION 3.25)
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
        /diagnostics:classic
        /GR-            # No RTTI

        # Compiler-level PDB only for Debug  (/Zi; Release/RelWithDebInfo rely on linker PDB)
        $<$<CONFIG:Debug>:/Zi>

        # Release: whole-program opt + function-level linking + intrinsics
        # (/Ot and /Gy are already implied by /O2 that CMake adds for Release)
        $<$<CONFIG:Release>:/GL>
    )

    add_link_options(
        /DEBUG                          # Linker PDB — all configs
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
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
# Include-only libs  (Lib, HtLib, all 3rdParty dirs from CommonBuildSheet.props)
# ============================================================================

add_library(Lib INTERFACE)
target_include_directories(Lib INTERFACE "${CMAKE_SOURCE_DIR}/Lib")

add_library(HtLib INTERFACE)
target_include_directories(HtLib INTERFACE "${CMAKE_SOURCE_DIR}/HtLib")

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
    ${THIRD_PARTY}/MikkTSpace
    ${THIRD_PARTY}/meshoptimizer/src
    ${THIRD_PARTY}/cgltf
    ${THIRD_PARTY}/minunit
    ${THIRD_PARTY}/OffsetAllocator
)

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
# shaders — compile HLSL to SPIR-V via run_dxc_compile.bat  (→ bin/SpirV/)
# ============================================================================

add_custom_target(shaders
    COMMAND cmd /c "\"${CMAKE_SOURCE_DIR}/run_dxc_compile.bat\""
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Compiling HLSL shaders to bin/SpirV/..."
    VERBATIM
)

# ============================================================================
# Subdirectories
# HtLib is an independent sub-project — added before link_libraries so its
# test targets don't inherit CommonDeps.
# Engine + HellPack come after and get CommonDeps automatically.
# ============================================================================

add_subdirectory(HtLib)

link_libraries(CommonDeps)   # everything added after this inherits CommonDeps

add_subdirectory(HellPack)
add_subdirectory(Engine)