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
# client - and SV_config.h sit next to them.
set(SOURCES SV_main.c SV_helper.c SV_heartbeat.c)

add_executable(ctr_srv ${SOURCES})
target_include_directories(ctr_srv PRIVATE ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/enet/include)
target_link_libraries(ctr_srv enet)
if (WIN32)
    target_link_libraries(ctr_srv winmm ws2_32)
endif()

# Outbound status heartbeat (optional).
# Without libcurl the module compiles to empty stubs and the server behaves
# exactly as it did before, so this is safe to leave on by default. What it
# posts, and where, is set at run time (octr-server.env), never here.
option(OCTR_HEARTBEAT "Build the outbound status heartbeat (requires libcurl)" ON)

if(OCTR_HEARTBEAT)
    find_package(CURL)
    if(CURL_FOUND)
        find_package(Threads REQUIRED)
        target_compile_definitions(ctr_srv PRIVATE OCTR_HEARTBEAT)
        target_include_directories(ctr_srv PRIVATE ${CURL_INCLUDE_DIRS})
        target_link_libraries(ctr_srv ${CURL_LIBRARIES} Threads::Threads)
        message(STATUS "Heartbeat: enabled (libcurl found)")
    else()
        message(STATUS "Heartbeat: disabled (libcurl not found)")
    endif()
endif()

# Compiler options (GCC and Clang; Windows builds use Server.sln). A plain
# char is signed everywhere, as on x86 and in MSVC: ARM's default is unsigned,
# and the server is built and run on ARM hosts too.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ctr_srv PRIVATE -fsigned-char)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(ctr_srv PRIVATE -Wno-int-conversion -Wno-incompatible-function-pointer-types -Wno-implicit-function-declaration -Wno-return-type)
elseif(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ctr_srv PRIVATE -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-implicit-int)
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
    target_link_options(ctr_srv PRIVATE -pie -Wl,-z,relro,-z,now)
endif()
