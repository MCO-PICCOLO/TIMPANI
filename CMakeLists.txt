cmake_minimum_required(VERSION 3.0)

# project name and version
project(sample_apps VERSION 0.1.0 DESCRIPTION "Timpani Sample Apps")

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

# sample_apps program
add_executable(sample_apps src/sample_apps.c)
target_link_libraries(sample_apps
		ttsched
)
target_include_directories(sample_apps
			PUBLIC ${CMAKE_SOURCE_DIR}/src
)
target_compile_options(sample_apps
		PUBLIC ${LIBSYSTEMD_CFLAGS_OTHER}
)
