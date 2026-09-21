# ----------------------------------------------------------------------------
# HellPack3rdParty — every 3rdParty lib HellPack uses, one static .lib, MAX OPT
# in every config, symbols kept, NO ASan.
# The target is created under the root's directory-level add_compile_options /
# link_libraries, so it inherits those flags and CommonDeps at creation;
# both are wiped from its properties below and the root's RelWithDebInfo flag set
# ( HT_MSVC_* ) is applied unconditionally, in every config.
# ----------------------------------------------------------------------------
add_library(HellPack3rdParty STATIC
    3rdPartyTU.cpp
    3rdPartyTU2.cpp
    ${THIRD_PARTY}/miniz/miniz.c
    # NOTE: NvGDeflate is C and gdeflate_*.c each #include the plain deflate_*.c under renamed
    #       symbols, so they can't share a unity TU — one .c per TU, like miniz.
    ${THIRD_PARTY}/NvGDeflate/lib/deflate_compress.c
    ${THIRD_PARTY}/NvGDeflate/lib/gdeflate_compress.c
    ${THIRD_PARTY}/NvGDeflate/lib/gdeflate_decompress.c
    ${THIRD_PARTY}/NvGDeflate/lib/utils.c
    ${THIRD_PARTY}/NvGDeflate/lib/x86/cpu_features.c
)
set_target_properties(HellPack3rdParty PROPERTIES
    COMPILE_OPTIONS ""
    LINK_LIBRARIES  ""
)
set_source_files_properties(${THIRD_PARTY}/NvGDeflate/lib/deflate_compress.c
    PROPERTIES COMPILE_DEFINITIONS WITH_GDEFLATE
)
target_link_libraries(HellPack3rdParty PRIVATE ThirdPartyIncludes)
if(MSVC)
    # NOTE: Debug STL ABI must match the exe's TUs — see HT_MSVC_DEBUG_ABI_DEFS in the root.
    target_compile_definitions(HellPack3rdParty PRIVATE
        "$<$<CONFIG:Debug>:${HT_MSVC_DEBUG_ABI_DEFS}>"
    )
    # Full optimization + full debug info ( /Zi /Zo ) in EVERY config — the root's RelWithDebInfo set.
    # NOTE: no /GL — LTCG objects can't link into a non-LTCG Debug exe.
    #       /O2 /Ot explicit: they must override the /Od that CMAKE_CXX_FLAGS_DEBUG carries.
    target_compile_options(HellPack3rdParty PRIVATE
        ${HT_MSVC_COMMON_FLAGS}
        /O2 /Ot ${HT_MSVC_OPT_FLAGS}
        ${HT_MSVC_OPT_DBGINFO_FLAGS}
        /analyze-
    )
endif()

# ----------------------------------------------------------------------------
# HellPack exe — our code, normal per-config flags.
# NOTE: HtLib's own .cpps are not listed here — they arrive as INTERFACE sources
#       through CommonDeps -> HtLib and are compiled into this exe.
# ----------------------------------------------------------------------------
if(MSVC)
    # NOTE: /Ob1 inlines everything marked inline / __forceinline ( the whole STL ranges layer ),
    #       /Od stays so our own logic is still steppable.
    string(REPLACE "/Ob0" "/Ob1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

add_executable(HellPack
    HellPack.cpp
)

target_include_directories(HellPack PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(HellPack PRIVATE _CONSOLE)
target_link_libraries(HellPack PRIVATE HellPack3rdParty)

if(MSVC)
    # ASan in Debug, only on HellPack.cpp — the HtLib .cpps stay uninstrumented.
    # Mixed TUs link thanks to HT_MSVC_DEBUG_ABI_DEFS ( via CommonDeps ).
    set_source_files_properties(HellPack.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:/fsanitize=address>"
    )
endif()