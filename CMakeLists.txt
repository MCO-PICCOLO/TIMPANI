cmake_minimum_required(VERSION 3.5)

# project name and version
project(timetrigger VERSION 2.0.0 DESCRIPTION "Timpani Time Trigger")

# Set cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

# libtttrace build options
option(CONFIG_TRACE_EVENT "Enable event tracing" OFF)
option(CONFIG_TRACE_BPF "Enable eBPF support" ON)
option(CONFIG_TRACE_BPF_EVENT "Enable eBPF-based event tracing" OFF)

if(CONFIG_TRACE_EVENT)
	message("CONFIG_TRACE_EVENT is ON")
	add_compile_definitions(CONFIG_TRACE_EVENT=1)
	set(TTTRACE_SRC ${TTTRACE_SRC} src/trace.c)
endif()

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
	set(TTTRACE_SRC ${TTTRACE_SRC} src/trace_bpf.c)

if(CONFIG_TRACE_BPF_EVENT)
	message("CONFIG_TRACE_BPF_EVENT is ON")
	add_compile_definitions(CONFIG_TRACE_BPF_EVENT=1)
	ADD_BPF(src/trace_bpf.c src/schedstat.bpf.c schedstat.bpf.o schedstat.skel.h)
endif()

endif()

# libtttrace library
add_library(tttrace SHARED
	${TTTRACE_SRC}
)
if(CONFIG_TRACE_BPF)
	add_dependencies(tttrace libbpf)
endif()
target_include_directories(tttrace
		PRIVATE $<$<BOOL:${CONFIG_TRACE_BPF}>:${CMAKE_BINARY_DIR}>
)
target_link_libraries(tttrace
		$<$<BOOL:${CONFIG_TRACE_BPF}>:bpf>
		$<$<BOOL:${CONFIG_TRACE_BPF}>:pthread>
)

# set libtttrace so version
set_target_properties(tttrace PROPERTIES
		VERSION ${PROJECT_VERSION}
		SOVERSION ${PROJECT_VERSION_MAJOR}
)

set_target_properties(tttrace PROPERTIES
		PUBLIC_HEADER src/libtttrace.h
)

# library install config
install(TARGETS tttrace
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# libttsched library
add_library(ttsched SHARED
	src/sched.c
)

# set libttsched so version
set_target_properties(ttsched PROPERTIES
		VERSION ${PROJECT_VERSION}
		SOVERSION ${PROJECT_VERSION_MAJOR}
)

set_target_properties(ttsched PROPERTIES
		PUBLIC_HEADER src/libttsched.h
)

# library install config
install(TARGETS ttsched
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# timetrigger test program
set(TIMETRIGGER_SOURCES
	src/main.c
	src/core.c
	src/hyperperiod.c
	src/task.c
	src/config.c
	src/trpc.c
	src/signal.c
	src/cleanup.c
)

add_executable(timetrigger ${TIMETRIGGER_SOURCES})

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
		tttrace
		ttsched
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
