cmake_minimum_required( VERSION 3.20 )
project( HellTechEngine LANGUAGES CXX )

# -------------------------------
# Include MSVC toolchain
# -------------------------------
include( CMake/toolchain_msvc.cmake )

# -------------------------------
# Include debug-only flags if Debug
# -------------------------------
if( CMAKE_BUILD_TYPE STREQUAL "Debug" )
    include( CMake/debug_msvc.cmake )
endif()

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

file( GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# Remove specific files
list( REMOVE_ITEM ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/dxc.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/r_dx12_backend.cpp"
)

file( GLOB 3RD_PARTY
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/meshoptimizer/src/*.cpp"
)
list( APPEND ENGINE_SOURCES ${3RD_PARTY} )

add_executable( ${PROJECT_NAME} ${ENGINE_SOURCES} )

# ----------------------
# Include directories
# ----------------------
target_include_directories( ${PROJECT_NAME} PRIVATE
    "$ENV{VULKAN_SDK}/Include"
    "$ENV{VULKAN_SDK}/Include/vulkan" # so we can #include <vulkan.h>
    "$ENV{VULKAN_SDK}/Source"
    "D:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/tinygltf"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/meshoptimizer/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/unordered_dense/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vfspp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# -------------------------------
# Apply compiler flags
# -------------------------------
target_compile_options( ${PROJECT_NAME} PRIVATE
        ${MSVC_COMMON_CFLAGS}
        ${MSVC_DEBUG_CFLAGS}
)

target_compile_definitions( ${PROJECT_NAME} PRIVATE ${MSVC_DEBUG_DEFINES} )

# -------------------------------
# Apply linker settings
# -------------------------------
target_link_directories( ${PROJECT_NAME} PRIVATE ${MSVC_COMMON_LINK_DIRS} )
target_link_libraries( ${PROJECT_NAME} PRIVATE ${MSVC_COMMON_LINK_LIBS} )
target_link_options( ${PROJECT_NAME} PRIVATE ${MSVC_COMMON_LINK_FLAGS} )

# Debug output directories
set_target_properties( ${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}/Debug"
        PDB_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}/Debug"
)

# ----------------------
# 3rd Party
# ----------------------
set( BUILD_EXAMPLES OFF CACHE BOOL "Do not build examples" FORCE )
add_subdirectory( "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vfspp" vfspp )
target_link_libraries( ${PROJECT_NAME} PRIVATE vfspp )
