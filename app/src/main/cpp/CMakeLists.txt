# The app's native library. Phase 4.1.
#
# The grafted Xenia runtime is not checked in - it is assembled from vendored upstream
# by tools/assemble_runtime.py - so this file cannot glob for sources. It asks
# tools/gen_cmake_sources.py instead, which derives the list from
# tools/compile_runtime.py: the one place that knows which of the 187 translation units
# in the assembled tree belong in an Android build.
#
# Prerequisites, all run from the repository root:
#   python tools/assemble_runtime.py
#   python tools/build_xenia_shaders.py
#   python tools/prepare_deps.py
#
# CMake 3.31.6 from the Android SDK is used, not the 4.x on PATH - see
# app/build.gradle.kts for why.

cmake_minimum_required(VERSION 3.22)
# C as well as CXX: tiny-AES-c is a .c file, and with only CXX enabled CMake compiles
# it as C++ - which mangles its symbols and leaves the extern "C" declarations in
# warden_xex.cc unresolved at link time.
project(warden C CXX)

# PLAN 11.3: the release build (AGP's RelWithDebInfo) runs the code the debug build was
# tested as - its assertions kept. Without this the build type's -DNDEBUG would compile
# Xenia's assert()s out of the release alone.
foreach(lang C CXX)
    string(REPLACE "-DNDEBUG" "" CMAKE_${lang}_FLAGS_RELWITHDEBINFO
        "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "-DNDEBUG" "" CMAKE_${lang}_FLAGS_RELEASE "${CMAKE_${lang}_FLAGS_RELEASE}")
endforeach()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(DAO_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# The owner (2026-11-02): no path of the build machine in what is built - the debug
# sections and __FILE__ held the NDK's (under the user's home, so the user's name) and the
# repository's. Each is written as a short stand-in instead: the NDK as "ndk", the
# repository as "src", anything else under the home folder as "home". The longest first:
# the compiler takes the first map that matches.
if(DEFINED ENV{USERPROFILE})
    file(TO_CMAKE_PATH "$ENV{USERPROFILE}" DAO_HOME_DIR)
else()
    file(TO_CMAKE_PATH "$ENV{HOME}" DAO_HOME_DIR)
endif()
set(DAO_PREFIX_MAPS
    "-ffile-prefix-map=${ANDROID_NDK}=ndk"
    "-ffile-prefix-map=${DAO_REPO_ROOT}=src")
if(DAO_HOME_DIR)
    list(APPEND DAO_PREFIX_MAPS "-ffile-prefix-map=${DAO_HOME_DIR}=home")
endif()
foreach(map ${DAO_PREFIX_MAPS})
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:${map}>")
    # FFmpeg's assembly (below): the assembler takes the debug half of the map.
    string(REPLACE "-ffile-prefix-map=" "-fdebug-prefix-map=" debug_map "${map}")
    add_compile_options("$<$<COMPILE_LANGUAGE:ASM>:${debug_map}>")
endforeach()
message(STATUS "warden: build paths mapped: ${DAO_PREFIX_MAPS}")
# tiny-AES-c, from XenonRecomp's vendored copy. The XEX image is AES-128-CBC
# encrypted, and using the same implementation XenonRecomp used to read it keeps one
# fewer thing able to disagree. Defaults are already CBC + AES128; no configuration.
set(DAO_TINY_AES "${DAO_REPO_ROOT}/third_party/XenonRecomp/thirdparty/tiny-AES-c")
# XenonRecomp's XEX patcher, for applying the title update, plus the LZX decompressor
# its delta patches need. Used rather than reimplemented because it is what produced
# recomp/generated/ppc_tu6 - an image patched any other way could disagree with the
# code translated from it. tools/gen_xex_patcher.py trims the one Win32-only overload.
set(DAO_XENON_UTILS "${DAO_REPO_ROOT}/third_party/XenonRecomp/XenonUtils")
set(DAO_TINY_SHA1 "${DAO_REPO_ROOT}/third_party/XenonRecomp/thirdparty/TinySHA1")
set(DAO_MSPACK "${DAO_REPO_ROOT}/third_party/XenonRecomp/thirdparty/libmspack/libmspack/mspack")
set(DAO_XEX_PATCHER "${DAO_REPO_ROOT}/build/gen/xex_patcher_android.cpp")
set(DAO_RUNTIME_TREE "${DAO_REPO_ROOT}/build/runtime-tree/src")
set(DAO_GENERATED "${DAO_REPO_ROOT}/build/gen/runtime-sources.cmake")

if(NOT EXISTS "${DAO_RUNTIME_TREE}")
    message(FATAL_ERROR
        "The runtime tree is missing: ${DAO_RUNTIME_TREE}\n"
        "It is generated, not checked in. From the repository root run:\n"
        "    python tools/assemble_runtime.py\n"
        "    python tools/build_xenia_shaders.py\n"
        "    python tools/prepare_deps.py")
endif()

# --------------------------------------------------------------- the source list
# Regenerated every configure, so adding a file to the tree or changing an exclusion
# in compile_runtime.py cannot leave a stale list behind.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${DAO_REPO_ROOT}/tools/gen_cmake_sources.py"
            --scope android --out "${DAO_GENERATED}"
    WORKING_DIRECTORY "${DAO_REPO_ROOT}"
    RESULT_VARIABLE DAO_GEN_RESULT
    OUTPUT_VARIABLE DAO_GEN_OUTPUT
    ERROR_VARIABLE DAO_GEN_ERROR)
if(NOT DAO_GEN_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_cmake_sources.py failed:\n${DAO_GEN_OUTPUT}${DAO_GEN_ERROR}")
endif()
message(STATUS "runtime sources: ${DAO_GEN_OUTPUT}")

# The kernel import stubs, from the same recompiled source the guest units come from.
set(DAO_IMPORT_STUBS "${DAO_REPO_ROOT}/build/gen/guest_import_stubs.cc")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${DAO_REPO_ROOT}/tools/gen_import_stubs.py"
            --out "${DAO_IMPORT_STUBS}"
    WORKING_DIRECTORY "${DAO_REPO_ROOT}"
    RESULT_VARIABLE DAO_STUB_RESULT
    OUTPUT_VARIABLE DAO_STUB_OUTPUT
    ERROR_VARIABLE DAO_STUB_ERROR)
if(NOT DAO_STUB_RESULT EQUAL 0)
    message(WARNING "gen_import_stubs.py failed:\n${DAO_STUB_OUTPUT}${DAO_STUB_ERROR}")
    set(DAO_IMPORT_STUBS "")
else()
    message(STATUS "import stubs: ${DAO_STUB_OUTPUT}")
endif()

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${DAO_REPO_ROOT}/tools/gen_xex_patcher.py"
            --out "${DAO_XEX_PATCHER}"
    WORKING_DIRECTORY "${DAO_REPO_ROOT}"
    RESULT_VARIABLE DAO_PATCHER_RESULT
    OUTPUT_VARIABLE DAO_PATCHER_OUTPUT
    ERROR_VARIABLE DAO_PATCHER_ERROR)
if(NOT DAO_PATCHER_RESULT EQUAL 0)
    message(FATAL_ERROR "gen_xex_patcher.py failed:
${DAO_PATCHER_OUTPUT}${DAO_PATCHER_ERROR}")
endif()

include("${DAO_GENERATED}")

# Reconfigure if the generator or its source of truth changes.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DAO_REPO_ROOT}/tools/gen_cmake_sources.py"
    "${DAO_REPO_ROOT}/tools/compile_runtime.py"
    "${DAO_REPO_ROOT}/tools/gen_import_stubs.py"
    "${DAO_REPO_ROOT}/tools/gen_xex_patcher.py")

# --------------------------------------------------------------- the runtime
# Vendored upstream, compiled as an object library so that its warnings can be
# silenced without silencing ours. Xenia is warning-noisy under NDK clang and none of
# it is actionable here: the code is upstream's and is modified only through the
# asserted edits in tools/assemble_runtime.py.
add_library(warden_runtime OBJECT ${DAO_RUNTIME_SOURCES})

target_include_directories(warden_runtime PUBLIC ${DAO_RUNTIME_INCLUDES})

target_compile_options(warden_runtime PRIVATE
    -w
    # Xenia needs both. Neither can be turned off - see tools/compile_runtime.py.
    -fexceptions
    -frtti
    # Optimised in the debug APK too (after the build type's -O0, so this one wins;
    # -g and the assertions stay). At -O0 the GPU command processor was the limit at
    # Ostagar: about 8 frames a second with about 3,275 draws a frame, the title's
    # Render thread spinning while the host worked through its command buffers.
    -O2)

# xenia/base/platform.h detects __ANDROID__ and sets XE_PLATFORM_ANDROID itself, and
# vulkan_api.h derives VK_USE_PLATFORM_ANDROID_KHR from that - so no platform defines
# are needed here. Stated because their absence looks like an oversight.

# --------------------------------------------------------------- the game's code
# 208 translation units, 265 MB of generated C++: every function XenonRecomp
# translated out of the Xbox 360 executable - a library of its own, libdaogame.so, and
# never in the APK (PLAN 12.2). The published app makes it on the device from the
# player's own disc (PLAN 12.5); this target makes the same library here, for
# development (python tools/push_gamecode.py puts it on the device), from the same
# recipe: app/src/main/assets/gamecode/ - hooks.h force-included into every file,
# shim.cpp compiled with them (which runtime it is for, the image's layout, a forwarder
# for each of the runtime's overrides), the flags recipe.json lists. All three are
# written by tools/gen_gamecode_recipe.py, and configuring stops if they are stale.
#
# Compiled with exactly what Phase 2 differentially tested - C++17, -O2, warnings off -
# and NOT with the build type's flags. Three reasons, in order of how much they matter:
# the behaviour of this code was validated at these settings and 6,635 functions were
# compared against the x86 reference to prove it; nobody steps through generated code
# in a debugger; and a -O0 -g build of 265 MB of it would be slow to produce and
# enormous to ship. -g0 explicitly, because AGP adds -g for debug builds and debug info
# for this much generated code runs to gigabytes.
#
# The hooks (hooks.h) are force-included (-include) rather than shadowed on the include
# path: the generated sources include "ppc_context.h" with quotes, and the recompile step
# puts a copy of that header beside them, which a quoted include finds before any -I
# directory (BUG-037). They define the trace of every guest function entered
# (warden_trace.cc: the fault report's and the thread report's call chains) and the GPU
# register hook - XenonRecomp emits PPC_MM_STORE_U32 for stores the title fences with
# eieio, and the ones into the GPU register window at 0x7FC80000, the ring buffer's write
# pointer above all, are forwarded to warden_gpu.cc, as DAORecomp's mmio_hook.h forwards
# them to its runtime.
set(DAO_GAMECODE_ASSETS "${CMAKE_CURRENT_LIST_DIR}/../assets/gamecode")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${DAO_REPO_ROOT}/tools/gen_gamecode_recipe.py" --check
    WORKING_DIRECTORY "${DAO_REPO_ROOT}"
    RESULT_VARIABLE DAO_RECIPE_RESULT
    OUTPUT_VARIABLE DAO_RECIPE_OUTPUT
    ERROR_VARIABLE DAO_RECIPE_ERROR)
if(NOT DAO_RECIPE_RESULT EQUAL 0)
    message(FATAL_ERROR "The game code's recipe is stale - the runtime's overrides "
        "changed:\n${DAO_RECIPE_OUTPUT}${DAO_RECIPE_ERROR}")
endif()
string(STRIP "${DAO_RECIPE_OUTPUT}" DAO_RECIPE_OUTPUT)
message(STATUS "warden: ${DAO_RECIPE_OUTPUT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${DAO_REPO_ROOT}/tools/gen_gamecode_recipe.py"
    "${DAO_GAMECODE_ASSETS}/recipe.json")

# The kernel import stubs: in the runtime, beside the imports they stand in for - the
# game's code calls every import in libwarden.so.
if(DAO_IMPORT_STUBS)
    add_library(warden_import_stubs OBJECT ${DAO_IMPORT_STUBS})
    target_include_directories(warden_import_stubs PRIVATE
        ${DAO_GUEST_INCLUDES}
        # for warden_imports.h, which the stubs call into
        ${CMAKE_CURRENT_LIST_DIR})
    target_compile_options(warden_import_stubs PRIVATE
        "-include${DAO_GAMECODE_ASSETS}/hooks.h" -std=c++17 -O2 -g0 -w)
endif()

if(DAO_GUEST_SOURCE_COUNT GREATER 0)
    add_library(daogame SHARED ${DAO_GUEST_SOURCES} "${DAO_GAMECODE_ASSETS}/shim.cpp")
    target_include_directories(daogame PRIVATE ${DAO_GUEST_INCLUDES})
    target_compile_options(daogame PRIVATE
        "-include${DAO_GAMECODE_ASSETS}/hooks.h" -std=c++17 -O2 -g0 -w)
    # Its own calls bound inside it - the game's calls to its functions direct, and to
    # the runtime's overrides through shim.cpp's forwarders - and what it calls in the
    # runtime from libwarden.so, as the device links it (recipe.json).
    target_link_options(daogame PRIVATE "-Wl,-Bsymbolic")
    target_link_libraries(daogame PRIVATE warden m dl)
    message(STATUS "warden: + ${DAO_GUEST_SOURCE_COUNT} recompiled guest sources, as "
        "libdaogame.so")
else()
    message(WARNING
        "No recompiled title found: the app builds, and makes the game's code on the "
        "device; tools/recompile.py makes it here.")
endif()

# --------------------------------------------------------------- the XMA decoder
# DAORecomp's real XMA decoder (DAORecomp/apu/xma_decoder.cpp, a port of Xenia's
# XmaContextNew) over FFmpeg's xmaframes codec (task 4.5). FFmpeg is Xenia's fork -
# has207's, xenia-canary's third_party/FFmpeg submodule - built as Xenia and DAORecomp
# build it: the libavutil and libavcodec subset in DAORecomp's xenia/ffmpeg.cmake,
# configured by xenia-canary's third_party/ffmpeg-xenia, which already covers Android on
# AArch64 with NEON. That file is DAORecomp's and targets x86-64 Windows, so it is read
# here, its two target switches are flipped for AArch64 - asserted, so an upstream change
# fails the configure instead of building the wrong platform's files - and the result
# included. Its NEON sources are GAS-syntax .S files, which the NDK's clang assembles.
set(XE_TP "${DAO_REPO_ROOT}/third_party/xenia-canary/third_party")
if(NOT EXISTS "${XE_TP}/FFmpeg/libavcodec/wmaprodec.c")
    message(FATAL_ERROR
        "FFmpeg is missing: ${XE_TP}/FFmpeg\n"
        "It is a xenia-canary submodule; python tools/bootstrap.py fetches it, or by hand:\n"
        "    cd third_party/xenia-canary && git submodule update --init --depth 1 third_party/FFmpeg")
endif()
enable_language(ASM)
set(XE_HAS_GAS_ASM TRUE)
function(xe_add_gas_sources target)
    target_sources(${target} PRIVATE ${ARGN})
endfunction()
set(DAO_FFMPEG_CMAKE_IN "${DAO_REPO_ROOT}/third_party/DAORecomp/xenia/ffmpeg.cmake")
file(READ "${DAO_FFMPEG_CMAKE_IN}" DAO_FFMPEG_TEXT)
foreach(DAO_SWITCH "X86_64 TRUE|X86_64 FALSE" "AARCH64 FALSE|AARCH64 TRUE")
    string(REPLACE "|" ";" DAO_SWITCH "${DAO_SWITCH}")
    list(GET DAO_SWITCH 0 DAO_FROM)
    list(GET DAO_SWITCH 1 DAO_TO)
    string(FIND "${DAO_FFMPEG_TEXT}" "set(XE_TARGET_${DAO_FROM})" DAO_AT)
    if(DAO_AT EQUAL -1)
        message(FATAL_ERROR "${DAO_FFMPEG_CMAKE_IN} no longer says set(XE_TARGET_${DAO_FROM}); "
                            "read it again before building FFmpeg from it")
    endif()
    string(REPLACE "set(XE_TARGET_${DAO_FROM})" "set(XE_TARGET_${DAO_TO})"
           DAO_FFMPEG_TEXT "${DAO_FFMPEG_TEXT}")
endforeach()
# Written only when its text changes, so that a configure does not rebuild FFmpeg.
file(WRITE "${CMAKE_BINARY_DIR}/ffmpeg-android.cmake.in" "${DAO_FFMPEG_TEXT}")
configure_file("${CMAKE_BINARY_DIR}/ffmpeg-android.cmake.in"
               "${CMAKE_BINARY_DIR}/ffmpeg-android.cmake" COPYONLY)
include("${CMAKE_BINARY_DIR}/ffmpeg-android.cmake")
# FFmpeg's DSP is the hot loop of every sound the title plays: optimised in the debug
# APK too. Position-independent with hidden symbols, as FFmpeg's own shared builds are:
# its NEON assembly reaches its C tables (ff_tx_tab_*) with adrp, which cannot address a
# symbol a shared library could have preempted - the link fails on exactly that otherwise.
# Linked statically into libwarden.so, so nothing outside it needs FFmpeg's symbols.
set_target_properties(libavutil libavcodec PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(libavutil PRIVATE -O2 -fvisibility=hidden)
target_compile_options(libavcodec PRIVATE -O2 -fvisibility=hidden)

# The decoder, compiled from DAORecomp's sources unmodified: dao_compat/ comes first on
# the include path and stands in for the parts of DAORecomp's runtime they include
# (stdafx.h, log.h, kernel/memory.h, gpu/xenia_host.h); its apu/ and kernel/types.h come
# from DAORecomp itself. warden_xma_real.cc is the plain interface warden_xma.cc calls.
add_library(warden_xma_decoder OBJECT
    "${DAO_REPO_ROOT}/third_party/DAORecomp/DAORecomp/apu/xma_decoder.cpp"
    "${DAO_REPO_ROOT}/third_party/DAORecomp/DAORecomp/apu/xma_trace.cpp"
    warden_xma_real.cc)
target_include_directories(warden_xma_decoder PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/dao_compat"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DAO_REPO_ROOT}/third_party/DAORecomp/DAORecomp"
    "${DAO_XENON_UTILS}"
    "${XE_TP}/FFmpeg"
    "${XE_TP}/ffmpeg-xenia"
    ${DAO_RUNTIME_INCLUDES})
target_compile_options(warden_xma_decoder PRIVATE -w -fexceptions -frtti -O2)

# --------------------------------------------------------------- mods (PLAN 10.13)
# DAORecomp's PC-to-360 mod converter: five of its files from the pinned checkout as they
# are, three with tools/assemble_dao_convert.py's edits (build/dao-convert), and the
# stand-ins in dao_compat/ and dao_convert_android.cc. Run that script after a pull.
set(DAO_CONVERT_TREE "${DAO_REPO_ROOT}/build/dao-convert")
if(NOT EXISTS "${DAO_CONVERT_TREE}/mods/convert/convert.cpp")
    message(FATAL_ERROR "build/dao-convert is missing: run python tools/assemble_dao_convert.py")
endif()
set(DAO_UPSTREAM "${DAO_REPO_ROOT}/third_party/DAORecomp/DAORecomp")
add_library(warden_dao_convert OBJECT
    "${DAO_CONVERT_TREE}/mods/convert/convert.cpp"
    "${DAO_CONVERT_TREE}/mods/mod_files.cpp"
    "${DAO_CONVERT_TREE}/files.cpp"
    "${DAO_CONVERT_TREE}/mods/convert/gff.cpp"
    "${DAO_UPSTREAM}/mods/convert/mesh.cpp"
    "${DAO_UPSTREAM}/mods/convert/texture.cpp"
    "${DAO_UPSTREAM}/mods/game_files.cpp"
    "${DAO_UPSTREAM}/ui/erf.cpp"
    dao_convert_android.cc
    dao_addin_android.cc)
target_include_directories(warden_dao_convert PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/dao_compat"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${DAO_UPSTREAM}"
    "${DAO_XENON_UTILS}"
    ${DAO_RUNTIME_INCLUDES})
# std::stop_token is behind libc++'s experimental-library switch in the NDK.
target_compile_options(warden_dao_convert PRIVATE -w -fexceptions -frtti -O2
    -fexperimental-library)

# PLAN 10.19: .7z and .rar imports (dao_archive_android.cc). 7-Zip's C decoder
# (third_party/7zip/C, tag 26.05, public domain) and libarchive's RAR 4 and RAR 5 readers
# (third_party/libarchive, v3.8.11, BSD 2-Clause - PLAN 11.3: UnRAR's library, used
# first, carries a restriction the app's GPL-3.0 cannot take), both fetched by
# tools/bootstrap.py.
set(SEVENZIP_C "${DAO_REPO_ROOT}/third_party/7zip/C")
set(LIBARCHIVE_SRC "${DAO_REPO_ROOT}/third_party/libarchive")
if(NOT EXISTS "${SEVENZIP_C}/7zDec.c" OR NOT EXISTS "${LIBARCHIVE_SRC}/libarchive/archive_read.c")
    message(FATAL_ERROR "third_party/7zip or third_party/libarchive is missing: run python tools/bootstrap.py")
endif()
# libarchive's read side, as its own Android build (contrib/android) configures it; a
# static library, so only what the two RAR readers reach is linked in.
add_library(warden_libarchive STATIC)
foreach(la_unit acl blake2s_ref blake2sp_ref check_magic crc32 cryptor digest entry
        entry_copy_stat entry_sparse entry_stat entry_strmode entry_xattr hmac options
        ppmd7 random rb read read_add_passphrase read_open_fd read_open_filename
        read_set_options read_support_filter_none read_support_format_rar
        read_support_format_rar5 string string_sprintf time util virtual)
    target_sources(warden_libarchive PRIVATE "${LIBARCHIVE_SRC}/libarchive/archive_${la_unit}.c")
endforeach()
target_include_directories(warden_libarchive PRIVATE "${LIBARCHIVE_SRC}/contrib/android/include")
target_include_directories(warden_libarchive PUBLIC "${LIBARCHIVE_SRC}/libarchive")
target_compile_definitions(warden_libarchive PRIVATE
    "PLATFORM_CONFIG_H=\"${LIBARCHIVE_SRC}/contrib/android/config/android.h\""
    LIBARCHIVE_STATIC)
target_compile_definitions(warden_libarchive PUBLIC LIBARCHIVE_STATIC)
target_compile_options(warden_libarchive PRIVATE -w -O2)
add_library(warden_archives OBJECT
    "${SEVENZIP_C}/7zAlloc.c"
    "${SEVENZIP_C}/7zArcIn.c"
    "${SEVENZIP_C}/7zBuf.c"
    "${SEVENZIP_C}/7zBuf2.c"
    "${SEVENZIP_C}/7zCrc.c"
    "${SEVENZIP_C}/7zCrcOpt.c"
    "${SEVENZIP_C}/7zDec.c"
    "${SEVENZIP_C}/7zFile.c"
    "${SEVENZIP_C}/7zStream.c"
    "${SEVENZIP_C}/Bcj2.c"
    "${SEVENZIP_C}/Bra.c"
    "${SEVENZIP_C}/Bra86.c"
    "${SEVENZIP_C}/BraIA64.c"
    "${SEVENZIP_C}/CpuArch.c"
    "${SEVENZIP_C}/Delta.c"
    "${SEVENZIP_C}/Lzma2Dec.c"
    "${SEVENZIP_C}/LzmaDec.c"
    "${SEVENZIP_C}/Ppmd7.c"
    "${SEVENZIP_C}/Ppmd7Dec.c")
target_sources(warden_archives PRIVATE dao_archive_android.cc)
target_include_directories(warden_archives PRIVATE "${SEVENZIP_C}"
    "${LIBARCHIVE_SRC}/libarchive" ${DAO_RUNTIME_INCLUDES})
target_compile_definitions(warden_archives PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY
    LIBARCHIVE_STATIC)
target_compile_options(warden_archives PRIVATE -w -O2
    $<$<COMPILE_LANGUAGE:CXX>:-fexceptions -frtti>)

# --------------------------------------------------------------- FSR 3.1 (PLAN 9.4)
# The FidelityFX SDK's FSR 3.1 upscaler and its Vulkan backend (third_party/FidelityFX-SDK,
# v1.1.4, its sdk folder - THIRD_PARTY.md), with the shader permutation headers
# tools/build_fsr3_shaders.py makes in build/fsr3-shaders (the SDK's own FidelityFX_SC
# step, done with the NDK's glslc). ffx_compat/warden_ffx_compat.h is forced into its
# sources: the Microsoft C runtime calls they make, and their device extension query
# (warden_fsr3.cc).
set(FFX_SDK "${DAO_REPO_ROOT}/third_party/FidelityFX-SDK/sdk")
set(FFX_SHADERS "${DAO_REPO_ROOT}/build/fsr3-shaders")
if(NOT EXISTS "${FFX_SDK}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp")
    message(FATAL_ERROR
        "The FidelityFX SDK is missing: ${FFX_SDK}\n"
        "Fetch it as THIRD_PARTY.md says (python tools/bootstrap.py does).")
endif()
if(NOT EXISTS "${FFX_SHADERS}/ffx_fsr3upscaler_accumulate_pass_permutations.h")
    message(FATAL_ERROR
        "FSR 3.1's shader headers are missing: ${FFX_SHADERS}\n"
        "Run: python tools/build_fsr3_shaders.py")
endif()
# PLAN 9.14: and its frame generation's two components, frame interpolation and optical
# flow (FSR 3.1's frame generation, from the upscaler's motion vectors and depth).
add_library(warden_ffx OBJECT
    "${FFX_SDK}/src/components/fsr3upscaler/ffx_fsr3upscaler.cpp"
    "${FFX_SHADERS}/frameinterpolation/ffx_frameinterpolation.cpp"
    "${FFX_SDK}/src/components/opticalflow/ffx_opticalflow.cpp"
    "${FFX_SDK}/src/backends/vk/ffx_vk.cpp"
    "${FFX_SDK}/src/backends/shared/ffx_shader_blobs.cpp"
    "${FFX_SDK}/src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp"
    "${FFX_SDK}/src/backends/shared/blob_accessors/ffx_frameinterpolation_shaderblobs.cpp"
    "${FFX_SDK}/src/backends/shared/blob_accessors/ffx_opticalflow_shaderblobs.cpp"
    "${FFX_SDK}/src/shared/ffx_assert.cpp"
    "${FFX_SDK}/src/shared/ffx_message.cpp"
    "${FFX_SDK}/src/shared/ffx_object_management.cpp"
    "${FFX_SDK}/src/shared/ffx_breadcrumbs_list.cpp")
# build/fsr3-shaders first: its FidelityFX/host/ffx_types.h, the context size doubled for
# 4-byte wchar_t, is the one the SDK and the app both take.
target_include_directories(warden_ffx PRIVATE
    "${FFX_SHADERS}"
    "${FFX_SDK}/include"
    "${FFX_SDK}/src/components"
    "${FFX_SDK}/src/components/frameinterpolation"
    "${FFX_SDK}/src/shared"
    "${FFX_SDK}/src/backends/shared"
    "${XE_TP}/Vulkan-Headers/include")
target_compile_definitions(warden_ffx PRIVATE FFX_FSR3UPSCALER=1 FFX_FI=1 FFX_OF=1 FFX_GCC=1)
target_compile_options(warden_ffx PRIVATE -w -O2 -fexceptions -frtti
    "-include${CMAKE_CURRENT_LIST_DIR}/ffx_compat/warden_ffx_compat.h")

# --------------------------------------------------------------- frame generation (PLAN 9.14)
# lsfg-vk's shared Vulkan layer and backend (third_party/lsfg-vk, v2.0.0-dev, GPL-3.0 -
# THIRD_PARTY.md) as tools/prepare_lsfg.py makes them in build/lsfg-vk, on the game's own
# device: warden_lsfg.cc takes the place of the backend's src/lsfgvk.cpp. The shaders come
# at run time from the owner's own Lossless.dll, never from here.
set(LSFG "${DAO_REPO_ROOT}/build/lsfg-vk")
if(NOT EXISTS "${LSFG}/lsfg-vk-backend/src/shaderchains/generate.cpp")
    message(FATAL_ERROR
        "lsfg-vk is not prepared: ${LSFG}\n"
        "Run: python tools/prepare_lsfg.py (after tools/bootstrap.py fetches third_party/lsfg-vk)")
endif()
add_library(warden_lsfg OBJECT
    "${LSFG}/lsfg-vk-common/src/helpers/errors.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/buffer.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/command_buffer.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/descriptor_pool.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/descriptor_set.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/fence.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/image.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/sampler.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/semaphore.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/shader.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/timeline_semaphore.cpp"
    "${LSFG}/lsfg-vk-common/src/vulkan/vulkan.cpp"
    "${LSFG}/lsfg-vk-backend/src/extraction/dll_reader.cpp"
    "${LSFG}/lsfg-vk-backend/src/extraction/shader_registry.cpp"
    "${LSFG}/lsfg-vk-backend/src/helpers/limits.cpp"
    "${LSFG}/lsfg-vk-backend/src/helpers/managed_shader.cpp"
    "${LSFG}/lsfg-vk-backend/src/helpers/utils.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/alpha0.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/alpha1.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/beta0.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/beta1.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/delta0.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/delta1.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/gamma0.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/gamma1.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/generate.cpp"
    "${LSFG}/lsfg-vk-backend/src/shaderchains/mipmaps.cpp"
    warden_lsfg.cc)
target_include_directories(warden_lsfg PRIVATE
    "${LSFG}/lsfg-vk-common/include"
    "${LSFG}/lsfg-vk-backend/include"
    "${LSFG}/lsfg-vk-backend/src"
    "${XE_TP}/Vulkan-Headers/include")
target_compile_options(warden_lsfg PRIVATE -O2 -fexceptions -frtti -Wno-unused-parameter)

# --------------------------------------------------------------- the app's own code
add_library(warden SHARED
    jni_bridge.cc
    warden_imports.cc
    warden_guest_info.cc
    warden_xex.cc
    warden_stfs.cc
    warden_boot.cc
    warden_guest.cc
    warden_guest_heap.cc
    warden_imports_kernel.cc
    warden_objects.cc
    warden_fs.cc
    warden_glyphs.cc
    warden_glyphs_apply.cc
    warden_threads.cc
    warden_suspend.cc
    warden_video.cc
    warden_net.cc
    warden_gpu.cc
    warden_gpu_memory.cc
    warden_frame_times.cc
    warden_frame_pacing.cc
    warden_thermal.cc
    warden_frame_rate.cc
    warden_fsr3.cc
    warden_upscale.cc
    warden_frame_generation.cc
    warden_deck.cc
    warden_camera_fov.cc
    warden_photo_mode.cc
    warden_priority_probe.cc
    warden_debug_mode.cc
    warden_dlc.cc
    warden_lifecycle.cc
    warden_kernel_variables.cc
    warden_audio.cc
    warden_audio_mix.cc
    warden_xma.cc
    warden_xam.cc
    warden_fmod.cc
    warden_game.cc
    warden_game_library.cc
    warden_trace.cc
    warden_guest_registers.cc
    ${DAO_TINY_AES}/aes.c
    ${DAO_XEX_PATCHER}
    ${DAO_MSPACK}/lzxd.c
    ${DAO_MSPACK}/system.c
    warden_runtime.cc
    warden_app_context.cc
    warden_window.cc
    warden_input.cc
    warden_xam_input.cc
    warden_affinity.cc
    warden_xdvdfs.cc
    warden_title.cc
    warden_memory.cc
    warden_memory_check.cc
    warden_dao_host.cc
    warden_extract.cc
    dao_frame_pacer_android.cc)

target_include_directories(warden PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(warden PRIVATE "${FFX_SHADERS}" "${FFX_SDK}/include")
# PLAN 12.3: the game's code decides the image's layout (Title Update 6's or 7's), so
# every runtime file looks the indirect-call table up as warden_game_library.h has it,
# never as this build's ppc_config.h would.
target_compile_options(warden PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/warden_game_library.h>")
# The guest headers, for warden_imports.cc's PPC_FUNC signatures. SYSTEM for the same
# reason as the runtime below - these are generated and their warnings are not ours.
target_include_directories(warden SYSTEM PRIVATE
    ${DAO_GUEST_INCLUDES}
    ${DAO_TINY_AES}
    ${DAO_XENON_UTILS}
    ${DAO_TINY_SHA1}
    ${DAO_MSPACK}
    ${DAO_MSPACK}/..)
# SYSTEM, so that -Wall -Wextra applies to our four files and not to the thousands of
# lines of upstream header they include. Without this the only warnings visible are
# Xenia's, which is the same as having none.
target_include_directories(warden SYSTEM PRIVATE ${DAO_RUNTIME_INCLUDES})

target_compile_options(warden PRIVATE
    -fexceptions
    -frtti
    -Wall
    -Wextra
    # Optimised in the debug APK too, as the runtime is: warden_guest_trace runs on
    # every guest function call, and at -O0 its relaxed atomics and the thread-local
    # ring lookup were out-of-line calls - with the rest of the -O0 runtime, about a
    # fifth of the title's two busiest threads at Ostagar (simpleperf, run 177).
    -O2)

# lld stops after 20 errors by default, which during bring-up means every link
# failure looks like it has exactly 20 causes and the list changes between runs
# depending on link order. The limit only matters when the link is already failing.
target_link_options(warden PRIVATE "-Wl,--error-limit=0")

target_link_libraries(warden PRIVATE
    warden_runtime
    # PLAN 12.2: the import stubs; the game's code is libdaogame.so (above).
    $<TARGET_NAME_IF_EXISTS:warden_import_stubs>
    # The XMA decoder and the FFmpeg it decodes with (above).
    warden_xma_decoder
    # PLAN 10.13: the mod converter (above); 10.19: .7z and .rar imports.
    warden_dao_convert
    warden_archives
    warden_libarchive
    libavcodec
    libavutil
    # libvulkan is shared-only on Android; the loader is part of the platform.
    vulkan
    # log for __android_log_*, android for ANativeWindow, aaudio for the sound output
    # (warden_audio.cc), z for the UI archive's atlas descriptions (warden_glyphs_apply.cc),
    # nativewindow for tagging the swapchain's buffers HDR10 (warden_gpu.cc, PLAN 9.2).
    warden_ffx
    warden_lsfg
    log
    android
    aaudio
    nativewindow
    z)

message(STATUS "warden: ${DAO_RUNTIME_SOURCE_COUNT} runtime sources + 22 app sources")
