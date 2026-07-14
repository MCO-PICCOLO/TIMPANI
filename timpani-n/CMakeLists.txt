## SPDX header
# SPDX-FileCopyrightText: Copyright 2026 LG Electronics Inc.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.5)

# project name and version
project(timpani-n VERSION 2.0.0 DESCRIPTION "Timpani-n")

# Set cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

# BPF tracing build options
option(CONFIG_TRACE_BPF "Enable eBPF support" ON)
option(CONFIG_TRACE_BPF_EVENT "Enable eBPF-based event tracing" OFF)

if(CONFIG_TRACE_BPF)
    message("CONFIG_TRACE_BPF is ON")

    include(ExternalProject)

    # Path to the shared libbpf in the TIMPANI root
    set(SHARED_LIBBPF_PATH "${CMAKE_SOURCE_DIR}/../libbpf")

    # Manually define libbpf project to point to the shared root
    ExternalProject_Add(libbpf
        SOURCE_DIR "${SHARED_LIBBPF_PATH}"
        CONFIGURE_COMMAND ""
        BUILD_COMMAND make -C <SOURCE_DIR>/src BUILD_STATIC_ONLY=1 DESTDIR=<BINARY_DIR> OBJDIR=<BINARY_DIR>/obj install
        INSTALL_COMMAND ""
        BINARY_DIR "${CMAKE_BINARY_DIR}/libbpf"
    )

    # Load bpf.cmake - we must help clang find headers in the shared root
    include(bpf)

    # We pass the shared path to Clang so #include <bpf/bpf_helpers.h> works
    # We use -I.../src because the 'bpf' folder is inside 'src'
    set(BPF_EXTRA_CFLAGS "-I${SHARED_LIBBPF_PATH}/src")

    add_compile_definitions(CONFIG_TRACE_BPF=1)

    # IMPORTANT: We need to make sure the ADD_BPF macro uses our new path
    # If sigwait.bpf.c fails to find headers, we might need to edit bpf.cmake
    include_directories(${CMAKE_BINARY_DIR}/libbpf/usr/include)
    ADD_BPF(src/trace_bpf.c src/sigwait.bpf.c sigwait.bpf.o sigwait.skel.h)

    if(CONFIG_TRACE_BPF_EVENT)
        message("CONFIG_TRACE_BPF_EVENT is ON")
        add_compile_definitions(CONFIG_TRACE_BPF_EVENT=1)
        # Add this right before ADD_BPF in CMakeLists.txt
        ADD_BPF(src/trace_bpf.c src/schedstat.bpf.c schedstat.bpf.o schedstat.skel.h)
    endif()
endif()

# timpani-n sources
set(TIMPANI_N_SOURCES
    src/main.c
	src/core.c
	src/hyperperiod.c
	src/task.c
    src/config.c
	src/trpc.c
	src/signal.c
	src/cleanup.c
    src/sched.c
	src/globals.c
	src/apex_monitor.c
)

if(CONFIG_TRACE_BPF)
    set(TIMPANI_N_SOURCES ${TIMPANI_N_SOURCES} src/trace_bpf.c)
endif()

add_executable(timpani-n ${TIMPANI_N_SOURCES})

# BPF dependencies
if(CONFIG_TRACE_BPF)
    add_dependencies(timpani-n libbpf)
    target_include_directories(timpani-n PRIVATE
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/libbpf/usr/include
    )
    target_link_libraries(timpani-n
        ${CMAKE_BINARY_DIR}/libbpf/obj/libbpf.a
        pthread elf z
    )
endif()

# Optimization
target_compile_options(timpani-n PRIVATE -O3 -flto -funroll-loops -ffast-math -fomit-frame-pointer)
set_target_properties(timpani-n PROPERTIES LINK_FLAGS "-flto -O3")

# Handle libtrpc (Sibling folder)
if(EXISTS "${CMAKE_SOURCE_DIR}/../libtrpc")
    add_subdirectory("${CMAKE_SOURCE_DIR}/../libtrpc" "${CMAKE_BINARY_DIR}/libtrpc" EXCLUDE_FROM_ALL)
endif()

target_link_libraries(timpani-n trpc systemd rt)
target_include_directories(timpani-n
    PUBLIC ${CMAKE_SOURCE_DIR}/src
    PUBLIC ${CMAKE_SOURCE_DIR}/../libtrpc/src
)

install(TARGETS timpani-n DESTINATION bin)

# libtrpc is pulled in via add_subdirectory(... EXCLUDE_FROM_ALL), so its own
# install(TARGETS trpc ...) rule is NOT run by CPack/make install. timpani-n
# links against it as a shared library (libtrpc.so.<SOVERSION>), so it must be
# installed alongside the binary or the package fails at runtime with
# "error while loading shared libraries: libtrpc.so.2: cannot open shared object file".
install(TARGETS trpc LIBRARY DESTINATION lib)
add_executable(exprocs EXCLUDE_FROM_ALL test/exprocs.c)

install(FILES systemd/timpani-n.service
    DESTINATION lib/systemd/system
)

# User-editable arguments file for the systemd service (see EnvironmentFile=
# in timpani-n.service). Declared as a Debian "conffile" below so dpkg
# preserves local edits across package upgrades instead of overwriting them.
install(FILES pkg/timpani-n.default
    DESTINATION /etc/default
    RENAME timpani-n
)

# ---------------------------------------------------------------------------
# Packaging (.deb / .rpm) — timpani-n is released as a native package because
# it needs direct root/eBPF/scheduler access and cannot run reliably inside a
# container.
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "timpani-n")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Timpani Developers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Timpani-N real-time scheduler")
set(CPACK_GENERATOR "DEB;RPM")

# .deb (Ubuntu) specific settings
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Timpani Developers")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsystemd0, libelf1, zlib1g")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/pkg/postinst"
    "${CMAKE_SOURCE_DIR}/pkg/prerm"
    "${CMAKE_SOURCE_DIR}/pkg/conffiles"
)

# .rpm (CentOS) specific settings
set(CPACK_RPM_PACKAGE_REQUIRES "systemd-libs, elfutils-libelf, zlib")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/pkg/postinst")
set(CPACK_RPM_PRE_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/pkg/prerm")
# Mark the arguments file as a config file so rpm preserves local edits
# across package upgrades (equivalent to the .deb conffiles entry above).
set(CPACK_RPM_USER_FILELIST "%config(noreplace) /etc/default/timpani-n")
# These parent directories are already owned (with their own attributes) by
# the base "filesystem" and "systemd" RPM packages. Without this, CPack's RPM
# generator auto-adds %dir entries for them with its own attributes, which
# rpm then refuses to install due to a "file ... conflicts with file from
# package ..." ownership/attribute mismatch.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    "/etc/default"
    "/usr/lib/systemd"
    "/usr/lib/systemd/system"
)

include(CPack)

