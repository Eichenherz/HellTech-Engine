set(THIRD_PARTY "${CMAKE_SOURCE_DIR}/3rdParty")

# ----------------------------------------------------------------------------
# HellPack3rdParty — every 3rdParty lib HellPack uses, compiled MAX OPT in every
# config, symbols kept. /O2 is incompatible with /RTC1, so /RTC1 is stripped
# from this directory's Debug flags and re-added to the HellPack exe below.
# ----------------------------------------------------------------------------
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/RTC1" "" CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG}")
endif()

add_library(HellPack3rdParty STATIC
    3rdPartyTU.cpp
    3rdPartyTU2.cpp
    ${THIRD_PARTY}/miniz/miniz.c
)
target_link_libraries(HellPack3rdParty PRIVATE ThirdPartyIncludes)

if(MSVC)
    # NOTE: no /GL — LTCG objects can't link into a non-LTCG Debug exe
    target_compile_options(HellPack3rdParty PRIVATE
        /O2 /Ob3 /Oi /Ot /Gw /Zc:inline /fp:fast /GS- /sdl- /analyze- /Zi
    )
endif()

# ----------------------------------------------------------------------------
# HellPack exe — our code, normal per-config flags
# NOTE: HtLib's own .cpps are not listed here — they arrive as INTERFACE sources
#       through CommonDeps -> HtLib and are compiled into this exe.
# ----------------------------------------------------------------------------
add_executable(HellPack
    HellPack.cpp
)

target_include_directories(HellPack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(HellPack PRIVATE _CONSOLE)
target_link_libraries(HellPack PRIVATE HellPack3rdParty)

if(MSVC)
    target_compile_options(HellPack PRIVATE $<$<CONFIG:Debug>:/RTC1>)
    # NOTE: level 1 keeps range checks, drops the level 2 iterator ownership tracking ( + its lock ).
    #       Must match across every TU in the binary, so the 3rdParty lib gets it too.
    target_compile_definitions(HellPack         PRIVATE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=1>)
    target_compile_definitions(HellPack3rdParty PRIVATE $<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=1>)
endif()