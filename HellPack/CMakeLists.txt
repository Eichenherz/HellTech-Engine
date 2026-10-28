# ----------------------------------------------------------------------------
# HellPack3rdParty — every 3rdParty lib HellPack uses, one static .lib, MAX OPT
# in every config, symbols kept, NO ASan.
# The target is created under the root's directory-level add_compile_options /
# link_libraries, so it inherits those flags and CommonDeps at creation;
# both are wiped from its properties below and the root's RelWithDebInfo flag set
# ( HT_CL_* ) is applied unconditionally, in every config.
# ----------------------------------------------------------------------------
# NOTE: zstd's own CMake is never used — its lib .c files are unity-included in 3rdPartyTU2.cpp.
add_library(HellPack3rdParty STATIC
    3rdPartyTU.cpp
    3rdPartyTU2.cpp
)
set_target_properties(HellPack3rdParty PROPERTIES
    COMPILE_OPTIONS ""
    LINK_LIBRARIES  ""
    MSVC_DEBUG_INFORMATION_FORMAT ""
)
target_link_libraries(HellPack3rdParty PRIVATE ThirdPartyIncludes)
target_include_directories(HellPack3rdParty PRIVATE "${CMAKE_SOURCE_DIR}/HtLib")
if(MSVC)
    # NOTE: Debug STL ABI must match the exe's TUs — see HT_CL_DEBUG_ABI_DEFS in the root.
    target_compile_definitions(HellPack3rdParty PRIVATE
        "$<$<CONFIG:Debug>:${HT_CL_DEBUG_ABI_DEFS}>"
    )
    # Full optimization + full debug info ( /Zi /Zo ) in EVERY config — the root's RelWithDebInfo set.
    # NOTE: no /GL — LTCG objects can't link into a non-LTCG Debug exe.
    #       /O2 /Ot explicit: they must override the /Od that CMAKE_CXX_FLAGS_DEBUG carries.
    target_compile_options(HellPack3rdParty PRIVATE
        ${HT_CL_COMMON_FLAGS}
        /O2 /Ot ${HT_CL_OPT_FLAGS} /fp:precise
        ${HT_CL_OPT_DBGINFO_FLAGS}
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
target_compile_options(HellPack PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:/fp:precise>)
target_link_libraries(HellPack PRIVATE HellPack3rdParty)

if(MSVC)
    # ASan in Debug, only on HellPack.cpp — the HtLib .cpps stay uninstrumented.
    # Mixed TUs link thanks to HT_CL_DEBUG_ABI_DEFS ( via CommonDeps ).
    set_source_files_properties(HellPack.cpp
        PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:${HT_CL_ASAN_COMPILE_FLAGS}>"
    )
    target_link_options(HellPack PRIVATE "$<$<CONFIG:Debug>:${HT_CL_ASAN_LINK_FLAGS}>")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_custom_command(TARGET HellPack POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${HT_CLANG_RT_DIR}/clang_rt.asan_dynamic-x86_64.dll" "$<TARGET_FILE_DIR:HellPack>")
    endif()
endif()