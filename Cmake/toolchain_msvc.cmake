# MSVC Toolchain

# Architecture
set( ARCHITECTURE "x64" )
set( OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin/${ARCHITECTURE}" )

# Output directories
foreach( OUTPUT_TYPE RUNTIME ARCHIVE LIBRARY PDB )
    set( CMAKE_${OUTPUT_TYPE}_OUTPUT_DIRECTORY ${OUTPUT_DIR} )
endforeach()

# Compiler flags
set( MSVC_COMMON_CFLAGS
        /JMC
        /permissive-
        /MP
        /ifcOutput ${OUTPUT_DIR}
        /GS
        /W3
        /Zc:wchar_t
        /Gm-
        /sdl
        /Zc:inline
        /fp:precise
        /errorReport:prompt
        /WX-
        /Zc:forScope
        /GR-
        /arch:AVX2
        /Gd
        /MDd
        /std:c++20
        /FC
        /diagnostics:classic
)

# Linker flags and libraries
set( MSVC_COMMON_LINK_FLAGS
        /ALLOWBIND:NO
        /MANIFEST
        /NXCOMPAT
        /DYNAMICBASE
        /MACHINE:X64
        /INCREMENTAL:NO
        /SUBSYSTEM:WINDOWS
        /MANIFESTUAC:"level='asInvoker' uiAccess='false'"
        /ERRORREPORT:PROMPT
        /NOLOGO
)

set( MSVC_COMMON_LINK_LIBS
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

set( MSVC_COMMON_LINK_DIRS
        "D:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/NVTT"
        "D:/Program Files/NVIDIA Corporation/NVIDIA Texture Tools/lib/x64-v142"
        "$ENV{VULKAN_SDK}/Lib"
)