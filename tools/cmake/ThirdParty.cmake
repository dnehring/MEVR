# SPDX-License-Identifier: MIT
# The third-party libraries the build compiles (lane BLD, 2026-11-17): Khronos's OpenXR loader, FreeType and RmlUi in
# every build (group "base"), and LExSDKv2's per-game SDKs in a development build (group "lesdk"). Their settings are
# written here once, for the main project and for the prebuilt cache's own project (tools/cmake/thirdparty), so a cached
# library is the library the main build would have compiled.
#
# The prebuilt cache (MEVR_THIRDPARTY_CACHE; on by default, off in CI): every fresh worktree compiled the same 330
# third-party sources again, 35% of a cold build-trilogy's compiler time (LExSDKv2's generated SDKs alone 27%). An entry
# is one group's Release build, the nested project's own build tree, in <MEVR_THIRDPARTY_CACHE_DIR>/<group>-<key>. The
# key is a hash of everything that changes what the compiler makes of those sources: each submodule's commit (a
# submodule with any local change, untracked files included, is never cached), this file and the nested project, the
# compiler's path and version, the Windows SDK, the generator, platform and toolset, CMake's version, the global flags,
# the C++ standard and which games' SDKs. A configure that finds no complete entry builds it under a lock (two lanes
# configuring at once: one builds, the other waits and then uses it), and marks it complete only after the whole build
# succeeded; an entry without the mark is deleted and built again. The main build then imports the libraries with the
# usage requirements their own CMake files exported, the include folders moved to this tree's copy of each submodule
# (the key says they are the same files), so its targets compile and link as before. Without git, with a submodule
# changed, or if the nested build fails, that group is built from source in the build tree, as it always was.
#
# Only Release is cached (what lanes, CI and the release build build): configure with -DMEVR_THIRDPARTY_CACHE=OFF for
# another configuration. An entry is 0.2 GB (base) or 0.9 GB (three games' SDKs); building one deletes the group's
# entries no configure used for 14 days, and deleting the folder clears the cache.

set(MEVR_THIRDPARTY_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# ---- Each library's settings and how it is added (the build from source, and the nested project) ------------------
# Macros, not functions: the libraries' CMake files read these variables from the calling scope, as before. The
# settings are made in the main project whether the libraries come from source or from the cache, so its variables and
# cache are the same either way. MEVR_TP_DIR_<module> is the folder a submodule is compiled from.

macro(mevr_third_party_lesdk_settings)
    # LExSDKv2 honours these as normal variables (CMP0077); LESDK_WITH_LE1/2/3 are the caller's.
    set(LESDK_WITH_LEL OFF)
    set(LESDK_WITH_SAMPLES OFF)
    set(LESDK_WITH_TESTS OFF)
endmacro()

macro(mevr_third_party_add_lesdk)
    mevr_third_party_lesdk_settings()
    add_subdirectory("${MEVR_TP_DIR_LExSDKv2}" external/LExSDKv2)
endmacro()

# Khronos OpenXR loader (Apache-2.0), linked statically into the mod. With the main menu, its UI library (research/28):
# RmlUi (MIT) with its FreeType font engine (FreeType: FTL), both static, built only for what links them (the hosts, the
# menu preview, MEVR Setup). Their own warning levels.
macro(mevr_third_party_base_settings with_menu)
    set(DYNAMIC_LOADER OFF)
    set(BUILD_API_LAYERS OFF)
    set(BUILD_TESTS OFF)
    set(BUILD_CONFORMANCE_TESTS OFF)
    set(BUILD_SDK_TESTS OFF)
    set(BUILD_ALL_EXTENSIONS OFF)
    if (${with_menu})
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        # RmlUi looks for FreeType with find_package; the freetype target is the one it links.
        set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)
        set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
        set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
        set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
        set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
        set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
        set(RMLUI_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    endif ()
endmacro()

macro(mevr_third_party_add_base with_menu)
    mevr_third_party_base_settings(${with_menu})
    add_subdirectory("${MEVR_TP_DIR_OpenXR-SDK}" external/OpenXR-SDK EXCLUDE_FROM_ALL)
    if (${with_menu})
        add_subdirectory("${MEVR_TP_DIR_freetype}" external/freetype EXCLUDE_FROM_ALL)
        add_library(Freetype::Freetype ALIAS freetype)
        add_subdirectory("${MEVR_TP_DIR_RmlUi}" external/RmlUi EXCLUDE_FROM_ALL)
    endif ()
endmacro()

# The submodules a group compiles, and the targets it provides.
function(mevr_third_party_group_info group with_menu out_modules out_targets)
    if (group STREQUAL "lesdk")
        set(modules LExSDKv2)
        set(targets "")
        foreach (game LE1 LE2 LE3)
            if (LESDK_WITH_${game})
                list(APPEND targets LESDK-${game})
            endif ()
        endforeach ()
    else ()
        set(modules OpenXR-SDK)
        set(targets openxr_loader)
        if (${with_menu})
            list(APPEND modules freetype RmlUi)
            list(APPEND targets freetype rmlui_core)
        endif ()
    endif ()
    set(${out_modules} "${modules}" PARENT_SCOPE)
    set(${out_targets} "${targets}" PARENT_SCOPE)
endfunction()

# ---- The cache ---------------------------------------------------------------------------------------------------

# A group's key (out_key, with the text it hashes in MEVR_TP_KEY_TEXT), or "" with the reason in out_why.
function(mevr_third_party_key group with_menu out_key out_why)
    set(${out_key} "" PARENT_SCOPE)
    mevr_third_party_group_info(${group} ${with_menu} modules targets)
    find_package(Git QUIET)
    if (NOT GIT_FOUND)
        set(${out_why} "no git" PARENT_SCOPE)
        return()
    endif ()
    set(text "group=${group}\ntargets=${targets}\n")
    foreach (module IN LISTS modules)
        # Git from the folder the submodule really is: in a lane's worktree external\<module> is a junction to the main
        # checkout's, whose .git file points at ../../.git/modules, which only resolves from there.
        file(REAL_PATH "${CMAKE_SOURCE_DIR}/external/${module}" dir)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" rev-parse HEAD
            OUTPUT_VARIABLE head RESULT_VARIABLE result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if (NOT result EQUAL 0 OR NOT head MATCHES "^[0-9a-f]+$")
            set(${out_why} "external/${module} is not a git checkout" PARENT_SCOPE)
            return()
        endif ()
        # Any change to the files, untracked ones included. Not the libraries' own submodules (FreeType's dlg, which only
        # its debug logging compiles, is never checked out).
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${dir}" status --porcelain --untracked-files=all --ignore-submodules=all
            OUTPUT_VARIABLE changes RESULT_VARIABLE result ERROR_QUIET)
        if (NOT result EQUAL 0 OR NOT changes STREQUAL "")
            set(${out_why} "external/${module} has local changes" PARENT_SCOPE)
            return()
        endif ()
        string(APPEND text "${module}=${head}\n")
    endforeach ()
    file(SHA256 "${MEVR_THIRDPARTY_CMAKE_DIR}/ThirdParty.cmake" own)
    file(SHA256 "${MEVR_THIRDPARTY_CMAKE_DIR}/thirdparty/CMakeLists.txt" nested)
    string(APPEND text "ThirdParty.cmake=${own}\nthirdparty/CMakeLists.txt=${nested}\n")
    foreach (var CMAKE_VERSION CMAKE_GENERATOR CMAKE_GENERATOR_PLATFORM CMAKE_GENERATOR_TOOLSET CMAKE_VS_PLATFORM_TOOLSET
            CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION CMAKE_C_COMPILER CMAKE_C_COMPILER_VERSION CMAKE_CXX_COMPILER
            CMAKE_CXX_COMPILER_VERSION ${MEVR_TP_FORWARDED})
        string(APPEND text "${var}=${${var}}\n")
    endforeach ()
    string(SHA256 key "${text}")
    string(SUBSTRING "${key}" 0 16 key)
    set(${out_key} "${key}" PARENT_SCOPE)
    set(MEVR_TP_KEY_TEXT "${text}" PARENT_SCOPE)
endfunction()

# The variables the nested project gets from the main one (so they are the same) and the key records.
set(MEVR_TP_FORWARDED CMAKE_C_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_RELEASE
    CMAKE_STATIC_LINKER_FLAGS CMAKE_STATIC_LINKER_FLAGS_RELEASE CMAKE_MSVC_RUNTIME_LIBRARY
    CMAKE_MSVC_DEBUG_INFORMATION_FORMAT CMAKE_CXX_STANDARD CMAKE_CXX_STANDARD_REQUIRED CMAKE_VS_GLOBALS)

# Builds a group's entry with the nested project; out_ok TRUE when the entry is complete.
function(mevr_third_party_build_entry group with_menu entry key_text out_ok)
    set(${out_ok} FALSE PARENT_SCOPE)
    mevr_third_party_group_info(${group} ${with_menu} modules targets)
    file(REMOVE_RECURSE "${entry}")
    file(MAKE_DIRECTORY "${entry}")
    set(games "")
    foreach (game LE1 LE2 LE3)
        if (LESDK_WITH_${game})
            list(APPEND games ${game})
        endif ()
    endforeach ()
    string(REPLACE ";" "," games "${games}")
    set(args -S "${MEVR_THIRDPARTY_CMAKE_DIR}/thirdparty" -B "${entry}" -G "${CMAKE_GENERATOR}")
    if (CMAKE_GENERATOR_PLATFORM)
        list(APPEND args -A "${CMAKE_GENERATOR_PLATFORM}")
    endif ()
    if (CMAKE_GENERATOR_TOOLSET)
        list(APPEND args -T "${CMAKE_GENERATOR_TOOLSET}")
    endif ()
    list(APPEND args "-DMEVR_TP_ROOT=${CMAKE_SOURCE_DIR}" "-DMEVR_TP_GROUP=${group}" "-DMEVR_TP_MENU=${with_menu}"
        "-DMEVR_TP_GAMES=${games}" "-DCMAKE_CONFIGURATION_TYPES=Release")
    foreach (var IN LISTS MEVR_TP_FORWARDED)
        if (DEFINED ${var})
            string(REPLACE ";" "\\;" value "${${var}}")
            list(APPEND args "-D${var}=${value}")
        endif ()
    endforeach ()
    message(STATUS "Third-party cache: building ${group} (${targets}) once for this key in ${entry}")
    string(TIMESTAMP start "%s")
    execute_process(COMMAND "${CMAKE_COMMAND}" ${args}
        OUTPUT_FILE "${entry}/mevr-configure.log" ERROR_FILE "${entry}/mevr-configure.err" RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        message(WARNING "Third-party cache: configuring ${group} failed (${entry}/mevr-configure.*); building it from source")
        return()
    endif ()
    # The nested project found the main one's compiler and Windows SDK by itself: the same, or no entry.
    include("${entry}/mevr-sources.cmake")
    set(toolchain "${CMAKE_C_COMPILER}|${CMAKE_CXX_COMPILER}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}|${CMAKE_VS_PLATFORM_TOOLSET}")
    if (NOT MEVR_TP_TOOLCHAIN STREQUAL toolchain)
        message(WARNING "Third-party cache: the nested project got another toolchain (${MEVR_TP_TOOLCHAIN}, not ${toolchain}); building ${group} from source")
        return()
    endif ()
    # As many jobs as the caller's build would take: CMAKE_BUILD_PARALLEL_LEVEL, else all; CL_MPCount, if the caller set
    # it, caps the compilers as in the main build (both inherited, as is an Idle priority class).
    set(jobs --parallel)
    if (NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
        list(APPEND jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
    endif ()
    execute_process(COMMAND "${CMAKE_COMMAND}" --build "${entry}" --config Release ${jobs} --target mevr-thirdparty
        OUTPUT_FILE "${entry}/mevr-build.log" ERROR_FILE "${entry}/mevr-build.err" RESULT_VARIABLE result)
    if (NOT result EQUAL 0)
        message(WARNING "Third-party cache: building ${group} failed (${entry}/mevr-build.*); building it from source")
        return()
    endif ()
    file(STRINGS "${entry}/mevr-files.txt" files)
    list(LENGTH files count)
    list(LENGTH targets wanted)
    if (NOT count EQUAL wanted)
        message(WARNING "Third-party cache: ${entry}/mevr-files.txt lists ${count} libraries, not ${wanted}; building ${group} from source")
        return()
    endif ()
    foreach (file IN LISTS files)
        if (NOT EXISTS "${file}")
            message(WARNING "Third-party cache: the build left no ${file}; building ${group} from source")
            return()
        endif ()
    endforeach ()
    string(TIMESTAMP stop "%s")
    math(EXPR seconds "${stop} - ${start}")
    file(WRITE "${entry}/mevr-key.txt" "${key_text}")
    file(WRITE "${entry}/mevr-complete" "built in ${seconds} s\n")
    message(STATUS "Third-party cache: ${group} built in ${seconds} s")
    set(${out_ok} TRUE PARENT_SCOPE)
    mevr_third_party_prune(${group} "${entry}")
endfunction()

# Deletes the group's other entries that no configure used for 14 days (a submodule bump or a new compiler leaves the
# old one behind), unless another configure holds one's lock.
function(mevr_third_party_prune group keep)
    get_filename_component(root "${keep}" DIRECTORY)
    string(TIMESTAMP now "%s")
    file(GLOB entries LIST_DIRECTORIES true "${root}/${group}-*")
    foreach (entry IN LISTS entries)
        if (entry STREQUAL keep OR NOT IS_DIRECTORY "${entry}")
            continue()
        endif ()
        set(used "${entry}/mevr-last-used")
        if (NOT EXISTS "${used}")
            set(used "${entry}")
        endif ()
        file(TIMESTAMP "${used}" when "%s")
        if (when STREQUAL "")
            continue()
        endif ()
        math(EXPR age "${now} - ${when}")
        if (age GREATER 1209600)
            file(LOCK "${entry}.lock" TIMEOUT 0 RESULT_VARIABLE locked)
            if (locked EQUAL 0)
                file(REMOVE_RECURSE "${entry}")
                file(LOCK "${entry}.lock" RELEASE)
                message(STATUS "Third-party cache: removed ${entry} (unused for 14 days)")
            endif ()
        endif ()
    endforeach ()
endfunction()

# Imports a complete entry's libraries into this project: the nested project's exports, their submodule folders moved
# to this tree's copies.
macro(mevr_third_party_import group entry)
    include("${entry}/mevr-sources.cmake")  # MEVR_TP_SOURCES: <module>=<the folder the entry was compiled from>
    file(GLOB _mevr_tp_exports "${entry}/mevr-targets-*.cmake")
    foreach (_mevr_tp_file IN LISTS _mevr_tp_exports)
        file(READ "${_mevr_tp_file}" _mevr_tp_text)
        foreach (_mevr_tp_pair IN LISTS MEVR_TP_SOURCES)
            string(REGEX MATCH "^([^=]+)=(.*)$" _mevr_tp_match "${_mevr_tp_pair}")
            string(REPLACE "${CMAKE_MATCH_2}" "${CMAKE_SOURCE_DIR}/external/${CMAKE_MATCH_1}" _mevr_tp_text "${_mevr_tp_text}")
        endforeach ()
        get_filename_component(_mevr_tp_name "${_mevr_tp_file}" NAME)
        file(WRITE "${CMAKE_BINARY_DIR}/thirdparty/${_mevr_tp_name}" "${_mevr_tp_text}")
        include("${CMAKE_BINARY_DIR}/thirdparty/${_mevr_tp_name}")
    endforeach ()
    file(TOUCH "${entry}/mevr-last-used")
endmacro()

# The main project's entry point: LExSDKv2 for the games listed (none: not at all), then the base group, each from the
# cache or from source. Defines LESDK-<game>, OpenXR::openxr_loader, OpenXR::headers and, with the menu, freetype,
# Freetype::Freetype and rmlui_core, as add_subdirectory did.
macro(mevr_third_party lesdk_games with_menu)
    set(_mevr_tp_default ON)
    if (DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS} OR "$ENV{LOCALAPPDATA}" STREQUAL "")
        set(_mevr_tp_default OFF)  # CI builds everything from source
    endif ()
    option(MEVR_THIRDPARTY_CACHE "Use (and fill) the prebuilt cache of the third-party libraries (Release only)" ${_mevr_tp_default})
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _mevr_tp_local)
    set(MEVR_THIRDPARTY_CACHE_DIR "${_mevr_tp_local}/MEVR/build-cache" CACHE PATH "The prebuilt cache's folder")
    file(TO_CMAKE_PATH "${MEVR_THIRDPARTY_CACHE_DIR}" MEVR_THIRDPARTY_CACHE_DIR)
    foreach (_mevr_tp_module LExSDKv2 OpenXR-SDK freetype RmlUi)
        set(MEVR_TP_DIR_${_mevr_tp_module} "${CMAKE_SOURCE_DIR}/external/${_mevr_tp_module}")
    endforeach ()
    set(_mevr_tp_games "${lesdk_games}")
    foreach (_mevr_tp_game LE1 LE2 LE3)
        if (_mevr_tp_game IN_LIST _mevr_tp_games)
            set(LESDK_WITH_${_mevr_tp_game} ON)
        else ()
            set(LESDK_WITH_${_mevr_tp_game} OFF)
        endif ()
    endforeach ()
    set(_mevr_tp_groups base)
    if (NOT "${lesdk_games}" STREQUAL "")
        set(_mevr_tp_groups lesdk base)  # LExSDKv2 first, as before
    endif ()
    foreach (_mevr_tp_group IN LISTS _mevr_tp_groups)
        set(_mevr_tp_entry "")
        if (MEVR_THIRDPARTY_CACHE AND NOT MEVR_THIRDPARTY_CACHE_DIR STREQUAL "")
            mevr_third_party_key(${_mevr_tp_group} ${with_menu} _mevr_tp_key _mevr_tp_why)
            if (_mevr_tp_key STREQUAL "")
                message(STATUS "Third-party cache: ${_mevr_tp_why}; building ${_mevr_tp_group} from source")
            else ()
                set(_mevr_tp_entry "${MEVR_THIRDPARTY_CACHE_DIR}/${_mevr_tp_group}-${_mevr_tp_key}")
                if (NOT EXISTS "${_mevr_tp_entry}/mevr-complete")
                    file(MAKE_DIRECTORY "${MEVR_THIRDPARTY_CACHE_DIR}")
                    set(_mevr_tp_lockfile "${_mevr_tp_entry}.lock")
                    file(LOCK "${_mevr_tp_lockfile}" TIMEOUT 3600 RESULT_VARIABLE _mevr_tp_lock)
                    if (NOT _mevr_tp_lock EQUAL 0)
                        message(WARNING "Third-party cache: ${_mevr_tp_lockfile}: ${_mevr_tp_lock}; building from source")
                        set(_mevr_tp_entry "")
                    else ()
                        if (NOT EXISTS "${_mevr_tp_entry}/mevr-complete")
                            mevr_third_party_build_entry(${_mevr_tp_group} ${with_menu} "${_mevr_tp_entry}"
                                "${MEVR_TP_KEY_TEXT}" _mevr_tp_ok)
                            if (NOT _mevr_tp_ok)
                                set(_mevr_tp_entry "")
                            endif ()
                        endif ()
                        file(LOCK "${_mevr_tp_lockfile}" RELEASE)
                    endif ()
                endif ()
            endif ()
        endif ()
        if (_mevr_tp_group STREQUAL "lesdk")
            if (_mevr_tp_entry STREQUAL "")
                mevr_third_party_add_lesdk()
            else ()
                mevr_third_party_lesdk_settings()
                message(STATUS "Third-party cache: lesdk from ${_mevr_tp_entry}")
                mevr_third_party_import(lesdk "${_mevr_tp_entry}")
            endif ()
        else ()
            if (_mevr_tp_entry STREQUAL "")
                mevr_third_party_add_base(${with_menu})
            else ()
                mevr_third_party_base_settings(${with_menu})
                message(STATUS "Third-party cache: base from ${_mevr_tp_entry}")
                find_package(Threads REQUIRED)  # the loader's link interface names Threads::Threads
                mevr_third_party_import(base "${_mevr_tp_entry}")
                if (${with_menu})
                    add_library(Freetype::Freetype ALIAS freetype)
                endif ()
            endif ()
        endif ()
    endforeach ()
endmacro()
