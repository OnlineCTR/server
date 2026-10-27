# SPDX-License-Identifier: AGPL-3.0-only
# Copyright (C) 2024-2026 OnlineCTR Team.
# Licensed under the GNU AGPL-3.0 (LICENSE), with the additional terms in
# ADDITIONAL-TERMS under its section 7. ABSOLUTELY NO WARRANTY.

cmake_minimum_required(VERSION 3.20)
project(OnlineCTR-Server C)

set(CMAKE_C_STANDARD 99)

# Everything position-independent, the bundled ENet too: the server links as
# a PIE (the hardening below).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ENet, bundled (enet/, 1.3.18): every platform builds the version the client
# uses, rather than whatever the system's package happens to be.
add_subdirectory(enet)

# Source files. global.h - the protocol header shared with the ROM and the PC
# client - and SV_config.h sit next to them. SV_auth.c is the signed join
# (1115), with Monocypher (monocypher/, vendored) for the Ed25519 check; on
# Windows its nonces come from bcrypt.
set(SOURCES SV_main.c SV_helper.c SV_heartbeat.c SV_auth.c monocypher/monocypher.c monocypher/monocypher-ed25519.c)

add_executable(ctr_srv ${SOURCES})
target_include_directories(ctr_srv PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/enet/include)
target_link_libraries(ctr_srv enet)
if (WIN32)
    target_link_libraries(ctr_srv winmm ws2_32 bcrypt)
endif()

# Outbound status heartbeat (optional).
# Without libcurl the module compiles to empty stubs and the server behaves
# exactly as it did before, so this is safe to leave on by default. What it
# posts, and where, is set at run time (octr-server.env), never here.
option(OCTR_HEARTBEAT "Build the outbound status heartbeat (requires libcurl)" ON)

# Can a program use the libcurl found, for this target? Sets OCTR_CURL_USABLE.
# In a function, so what it sets for the test stays here: the test project
# must link (a toolchain may ask try_compile for static libraries only, which
# compiles and links nothing - a libcurl for another architecture would pass),
# and curl's own CURLConfig.cmake gives CURL::libcurl, an ALIAS, which the test
# project cannot see before CMake 3.29, so the target it names is passed.
function(_octr_check_curl)
    include(CheckCSourceCompiles)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    set(CMAKE_REQUIRED_INCLUDES ${CURL_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_LIBRARIES)
    foreach(_octr_lib IN LISTS CURL_LIBRARIES)
        if(TARGET "${_octr_lib}")
            get_target_property(_octr_real "${_octr_lib}" ALIASED_TARGET)
            if(_octr_real)
                set(_octr_lib "${_octr_real}")
            endif()
        endif()
        list(APPEND CMAKE_REQUIRED_LIBRARIES "${_octr_lib}")
    endforeach()
    check_c_source_compiles("#include <curl/curl.h>
int main(void) { return curl_easy_init() == 0; }" OCTR_CURL_USABLE)
endfunction()

if(OCTR_HEARTBEAT)
    find_package(CURL)
    # A libcurl found is used only if a program builds with it for this
    # target: a cross build (MinGW from Linux) would otherwise take the build
    # machine's own Linux libcurl, which pkg-config hands FindCURL, and then
    # fail to compile against its headers.
    if(CURL_FOUND)
        # Checked again on every configure: the libcurl found can change in
        # the same build dir (-DCURL_LIBRARY=... with -DCURL_INCLUDE_DIR=...,
        # or a -DCMAKE_PREFIX_PATH holding curl's CURLConfig.cmake). A plain
        # prefix such as curl.se's zip is not searched again once a libcurl
        # is cached: that takes a fresh build dir (README).
        unset(OCTR_CURL_USABLE CACHE)
        _octr_check_curl()
    endif()
    if(CURL_FOUND AND OCTR_CURL_USABLE)
        target_compile_definitions(ctr_srv PRIVATE OCTR_HEARTBEAT)
        target_include_directories(ctr_srv PRIVATE ${CURL_INCLUDE_DIRS})
        target_link_libraries(ctr_srv ${CURL_LIBRARIES})
        # The worker thread: pthreads, or on Windows (MinGW) the system's own
        # threads (SV_heartbeat.c, _WIN32), which need no library
        if(NOT WIN32)
            find_package(Threads REQUIRED)
            target_link_libraries(ctr_srv Threads::Threads)
        endif()
        message(STATUS "Heartbeat: enabled (libcurl found)")
    elseif(CURL_FOUND)
        message(STATUS "Heartbeat: disabled (the libcurl found, ${CURL_LIBRARIES}, does not build for this target)")
    else()
        message(STATUS "Heartbeat: disabled (libcurl not found)")
    endif()
endif()

# Compiler options (GCC and Clang, MinGW's included; Visual Studio builds use
# Server.sln). A plain
# char is signed everywhere, as on x86 and in MSVC: ARM's default is unsigned,
# and the server is built and run on ARM hosts too.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ctr_srv PRIVATE -fsigned-char)
endif()
# Every warning on. The suppressions that used to be here (implicit function
# declarations, incompatible pointers, implicit int) hid the class of mistake
# a 64-bit host turns into a crash: a function used before its declaration is
# taken to return int, and a pointer it returns is cut to 32 bits. The
# server's own sources build clean with them on; ENet's are its own target.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ctr_srv PRIVATE -Wall)
endif()

# Debug options
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(NOT MSVC)
        target_compile_options(ctr_srv PRIVATE -g -gdwarf-2 -O0)
    endif()
elseif(NOT MSVC)
    # Standard hardening for a binary that listens on the internet: stack
    # canaries, fortified libc calls, position-independent (so ASLR applies
    # to the executable too) and a read-only GOT after relocation.
    target_compile_options(ctr_srv PRIVATE -O2 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE)
    # The link half depends on the executable format.
    if(APPLE)
        # Mach-O: nothing. Apple's linker has no -z options and stops on
        # them; every macOS executable is position-independent already, and
        # its lazy binding table is read-only after start-up.
    elseif(WIN32)
        # PE, a MinGW build: ASLR and DEP, and for a 64-bit image high-entropy
        # (64-bit) ASLR - binutils sets them by default since 2.36; said here
        # for older ones. 32-bit images have no high-entropy ASLR, and GNU
        # ld's 32-bit PE linker rejects the option.
        target_link_options(ctr_srv PRIVATE -Wl,--dynamicbase,--nxcompat)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            target_link_options(ctr_srv PRIVATE -Wl,--high-entropy-va)
        endif()
        # The helpers the stack protector and the fortified calls need
        # (__stack_chk_fail, __memcpy_chk, ...) are libssp's on a mingw-w64
        # runtime before version 11. Whatever libssp.a the compiler has is
        # linked in by full path, statically, so the exe needs no libssp-0.dll
        # beside it: GCC's real one, where it is required (Debian's
        # gcc-mingw-w64) or merely redundant (later runtimes have the helpers
        # in libmingwex too); llvm-mingw's empty stub, harmlessly. GCC and
        # llvm-mingw's clang name their copy (-print-file-name). A clang whose
        # target comes from CMAKE_C_COMPILER_TARGET is asked without it here,
        # and a distro clang on GCC's mingw-w64 runtime names none even with
        # it: then it is looked for in the directories CMake saw the compiler
        # link from. No libssp.a at all (MSYS2's GCC): the runtime has the
        # helpers, nothing is added.
        execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=libssp.a
            OUTPUT_VARIABLE OCTR_LIBSSP OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT (IS_ABSOLUTE "${OCTR_LIBSSP}" AND EXISTS "${OCTR_LIBSSP}"))
            find_file(OCTR_LIBSSP_FILE libssp.a PATHS ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES}
                NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
            set(OCTR_LIBSSP "${OCTR_LIBSSP_FILE}")
        endif()
        if(IS_ABSOLUTE "${OCTR_LIBSSP}" AND EXISTS "${OCTR_LIBSSP}")
            target_link_libraries(ctr_srv "${OCTR_LIBSSP}")
        endif()
    elseif(CYGWIN)
        # Cygwin, and MSYS2's MSYS shell (CMake sets CYGWIN there too): PE,
        # but its linker has no -z options, and binutils leaves ASLR off for
        # Cygwin on purpose (fork() needs every process mapped alike). The
        # compile options above still apply; cygwin1.dll has the stack
        # protector's and fortified calls' helpers. Nothing to add.
    else()
        # ELF (GNU ld, gold, lld): a position-independent executable, and its
        # relocations read-only after start-up (full RELRO).
        target_link_options(ctr_srv PRIVATE -pie -Wl,-z,relro,-z,now)
    endif()
endif()
