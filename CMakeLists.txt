cmake_minimum_required(VERSION 3.5)

# project name and version
project(timetrigger VERSION 2.0.0 DESCRIPTION "Timpani Time Trigger")

# Set cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

# BPF tracing build options
option(CONFIG_TRACE_BPF "Enable eBPF support" ON)
option(CONFIG_TRACE_BPF_EVENT "Enable eBPF-based event tracing" OFF)

if(CONFIG_TRACE_BPF)
	message("CONFIG_TRACE_BPF is ON")

	# for ExternalProject_Add
	include(ExternalProject)

	# load libbpf.cmake
	include(libbpf)

	# Load bpf.cmake
	include(bpf)

	add_compile_definitions(CONFIG_TRACE_BPF=1)
	ADD_BPF(src/trace_bpf.c src/sigwait.bpf.c sigwait.bpf.o sigwait.skel.h)

if(CONFIG_TRACE_BPF_EVENT)
	message("CONFIG_TRACE_BPF_EVENT is ON")
	add_compile_definitions(CONFIG_TRACE_BPF_EVENT=1)
	ADD_BPF(src/trace_bpf.c src/schedstat.bpf.c schedstat.bpf.o schedstat.skel.h)
endif()

endif()

# timetrigger sources - integrated scheduling and tracing
set(TIMETRIGGER_SOURCES
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

# Add BPF tracing sources based on configuration
if(CONFIG_TRACE_BPF)
	set(TIMETRIGGER_SOURCES ${TIMETRIGGER_SOURCES} src/trace_bpf.c)
endif()

add_executable(timetrigger ${TIMETRIGGER_SOURCES})

# BPF dependencies for timetrigger
if(CONFIG_TRACE_BPF)
	add_dependencies(timetrigger libbpf)
	target_include_directories(timetrigger PRIVATE ${CMAKE_BINARY_DIR})
	target_link_libraries(timetrigger bpf pthread)
endif()

# 성능 최적화 플래그 추가
target_compile_options(timetrigger PRIVATE
	-O3                    # 최고 수준 최적화
	-march=native          # 네이티브 아키텍처 최적화
	-mtune=native          # 네이티브 튜닝
	-flto                  # Link Time Optimization
	-funroll-loops         # 루프 언롤링
	-finline-functions     # 함수 인라이닝
	-ffast-math            # 빠른 수학 연산
	-fomit-frame-pointer   # 프레임 포인터 생략
)

# 링크 시 최적화
set_target_properties(timetrigger PROPERTIES
	LINK_FLAGS "-flto -O3"
)

target_link_libraries(timetrigger
		trpc
		systemd
		rt
)
target_include_directories(timetrigger
			PUBLIC ${CMAKE_SOURCE_DIR}/src
			PUBLIC ${CMAKE_SOURCE_DIR}/libtrpc/src
)
target_compile_options(timetrigger
		PUBLIC ${LIBSYSTEMD_CFLAGS_OTHER}
)
install(TARGETS timetrigger DESTINATION ${CMAKE_INSTALL_BINDIR})

# exprocs test program
add_executable(exprocs test/exprocs.c)
install(TARGETS exprocs DESTINATION ${CMAKE_INSTALL_BINDIR})

# libtrpc library
add_subdirectory(libtrpc)
