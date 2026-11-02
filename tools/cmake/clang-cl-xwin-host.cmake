# Toolchain: clang-cl + lld-link against the MSVC CRT and Windows SDK that
# tools/setup_winsysroot.py splats into toolchain/winsysroot - DAORecomp's
# cmake/clang-cl-xwin.cmake, but for a build that runs on this computer.
#
#   -DCMAKE_TOOLCHAIN_FILE=tools/cmake/clang-cl-xwin-host.cmake
#   -DDAO_LLVM_BIN=<dir with clang-cl, lld-link, llvm-lib, llvm-rc>
#   -DDAO_WINSYSROOT=<toolchain/winsysroot>
#
# DAORecomp's file names the system (CMAKE_SYSTEM_NAME Windows), which makes CMake treat
# the build as a cross build; LLVM's build then sets up a second, native build of its own
# for its tablegen tools. This one leaves the system to CMake, so the build is native:
# tools/build_device_toolchain.py builds LLVM's tablegen tools with it (PLAN 12.1).

foreach(var DAO_LLVM_BIN DAO_WINSYSROOT)
    if(NOT ${var} AND DEFINED ENV{${var}})
        set(${var} "$ENV{${var}}")
    endif()
    if(NOT ${var})
        message(FATAL_ERROR "clang-cl-xwin-host.cmake: set ${var}")
    endif()
    file(TO_CMAKE_PATH "${${var}}" ${var})
endforeach()

# try_compile projects re-read this file; hand them the same settings.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES DAO_LLVM_BIN DAO_WINSYSROOT)

set(CMAKE_C_COMPILER "${DAO_LLVM_BIN}/clang-cl.exe")
set(CMAKE_CXX_COMPILER "${DAO_LLVM_BIN}/clang-cl.exe")
set(CMAKE_LINKER "${DAO_LLVM_BIN}/lld-link.exe")
set(CMAKE_AR "${DAO_LLVM_BIN}/llvm-lib.exe")
set(CMAKE_RC_COMPILER "${DAO_LLVM_BIN}/llvm-rc.exe")
set(CMAKE_MT "${DAO_LLVM_BIN}/llvm-mt.exe")
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# Only the splatted CRT/SDK: with a sysroot, clang-cl ignores %INCLUDE% and any
# installed Visual Studio.
set(_sysroot "-winsysroot \"${DAO_WINSYSROOT}\"")
set(CMAKE_C_FLAGS_INIT "${_sysroot}")
set(CMAKE_CXX_FLAGS_INIT "${_sysroot}")

file(GLOB _sdk_includes "${DAO_WINSYSROOT}/Windows Kits/10/Include/*")
list(SORT _sdk_includes)
list(GET _sdk_includes -1 _sdk_include)
set(CMAKE_RC_FLAGS_INIT "/I \"${_sdk_include}/um\" /I \"${_sdk_include}/shared\"")

# lld-link finds the libraries itself. No manifest step (llvm-mt may lack libxml2).
foreach(kind EXE SHARED MODULE)
    set(CMAKE_${kind}_LINKER_FLAGS_INIT "\"/winsysroot:${DAO_WINSYSROOT}\" /manifest:no")
endforeach()

# xwin leaves out the debug CRT; probe with the release one.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
