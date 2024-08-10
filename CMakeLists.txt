cmake_minimum_required(VERSION 3.0)

# project name and version
project(timetrigger VERSION 0.1.0 DESCRIPTION "Timpani Time Trigger")

# libtttrace library
add_library(tttrace SHARED
	src/trace.c
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
