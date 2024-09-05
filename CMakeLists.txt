cmake_minimum_required(VERSION 3.0)

# project name and version
project(timetrigger VERSION 0.1.0 DESCRIPTION "Timpani Time Trigger")

# Set cmake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}")

# Load bpf.cmake
include(bpf)

# libtttrace build options
option(CONFIG_TRACE_EVENT "Enable event tracing" ON)
option(CONFIG_TRACE_BPF "Enable eBPF support" ON)
option(CONFIG_TRACE_BPF_EVENT "Enable eBPF-based event tracing" OFF)

if(CONFIG_TRACE_EVENT)
	message("CONFIG_TRACE_EVENT is ON")
	add_compile_definitions(CONFIG_TRACE_EVENT=1)
	set(TTTRACE_SRC ${TTTRACE_SRC} src/trace.c)
endif()

if(CONFIG_TRACE_BPF)
	message("CONFIG_TRACE_BPF is ON")
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
add_executable(timetrigger src/timetrigger.c)
target_link_libraries(timetrigger
		tttrace
		ttsched
)
target_include_directories(timetrigger
			PUBLIC ${CMAKE_SOURCE_DIR}/src
)
target_compile_options(timetrigger
		PUBLIC ${LIBSYSTEMD_CFLAGS_OTHER}
)

# exprocs test program
add_executable(exprocs test/exprocs.c)
target_link_libraries(exprocs
		ttsched
)
target_include_directories(exprocs
			PUBLIC ${CMAKE_SOURCE_DIR}/src
)
target_compile_options(exprocs
		PUBLIC ${LIBSYSTEMD_CFLAGS_OTHER}
)
#add_executable(exprocs test/exprocs.c)
