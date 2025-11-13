cmake_minimum_required( VERSION 3.20 )
project( HellTechEngine LANGUAGES CXX )

# TODO: must use same OUTDIR which is generated here for everything !
# ----------------------
# Output
# ----------------------
set( ARCHITECTURE "x64" )  

set( OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin/${ARCHITECTURE}" ) #/${CMAKE_BUILD_TYPE}" )
foreach( OUTPUT_TYPE RUNTIME ARCHIVE LIBRARY PDB )
    set( CMAKE_${OUTPUT_TYPE}_OUTPUT_DIRECTORY ${OUTPUT_DIR} )
endforeach()


set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

#set( CMAKE_C_STANDARD 17 )
#set( CMAKE_C_STANDARD_REQUIRED ON )

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
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/fastgltf/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/meshoptimizer/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/unordered_dense/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vfspp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# ----------------------
# Preprocessor definitions
# ----------------------
target_compile_definitions( ${PROJECT_NAME} PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _DEBUG
    _WIN32
    _VK_DEBUG_
    _MBCS
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING
    _SILENCE_CXX20_OLD_SHARED_PTR_DEPRECATION_WARNING
    _SILENCE_CXX20_ATOMIC_INIT_DEPRECATION_WARNING
)

# ----------------------
# Compiler options
# ----------------------
target_compile_options( ${PROJECT_NAME} PRIVATE
    /JMC
    /permissive-
    /MP
    /ifcOutput ${OUTPUT_DIR}
    /GS
    /W3
    /Zc:wchar_t
    /ZI
    /Gm-
    /Od
    /sdl
    /Zc:inline
    /fp:precise
    /errorReport:prompt
    /WX-
    /Zc:forScope
    /RTC1
    /std:c17
    /GR-
    /arch:AVX2
    /Gd
    /MDd
    /std:c++20
    /FC
    #/Fa"x64/Debug/"
    #/Fo"x64/Debug/"
    /diagnostics:classic
)

# ----------------------
# Linker settings
# ----------------------
set_target_properties( ${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}/Debug"
    PDB_OUTPUT_DIRECTORY_DEBUG "${OUTPUT_DIR}/Debug"
)

# Link directories 
target_link_directories( ${PROJECT_NAME} PRIVATE
    "D:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/NVTT"                 
    "D:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/lib/x64-v142"   
    "$ENV{VULKAN_SDK}/Lib"
)

# Linked libraries
target_link_libraries( ${PROJECT_NAME} PRIVATE
    nvtt30205
    kernel32
    user32
    gdi32
    winspool
    comdlg32
    advapi32
    shell32
    ole32
    oleaut32
    uuid
    odbc32
    odbccp32
)

# Additional linker flags
target_link_options( ${PROJECT_NAME} PRIVATE
    /ALLOWBIND:NO               # disable automatic binding
    /MANIFEST                   # generate manifest
    /NXCOMPAT                   # NX compatibility
    /DYNAMICBASE                # ASLR enabled
    /DEBUG                      # generate debug info (Debug builds)
    /MACHINE:X64                # target x64
    /INCREMENTAL:NO             # disable incremental linking
    /SUBSYSTEM:WINDOWS          # Windows GUI subsystem
    /MANIFESTUAC:"level='asInvoker' uiAccess='false'" # UAC manifest
    #/ManifestFile: ${OUTPUT_DIR}/${PROJECT_NAME}.exe.intermediate.manifest # intermediate manifest
    /LTCGOUT:"${OUTPUT_DIR}/${PROJECT_NAME}.iobj"  # LTCG output object
    /ERRORREPORT:PROMPT
    /ILK:"${OUTPUT_DIR}/${PROJECT_NAME}.ilk"       # incremental link file (unused with /INCREMENTAL:NO)
    /NOLOGO
)


# ----------------------
# 3rd Party
# ----------------------
add_subdirectory( "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/fastgltf" fastgltf_build )
target_link_libraries( ${PROJECT_NAME} PRIVATE fastgltf )

set( BUILD_EXAMPLES OFF CACHE BOOL "Do not build examples" FORCE )
add_subdirectory( "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vfspp" vfspp_build )
target_link_libraries( ${PROJECT_NAME} PRIVATE vfspp )
