# Get the architecture name for BPF header include directory.
execute_process(COMMAND uname -m OUTPUT_VARIABLE BPF_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
set(BPF_INCLUDE_DIR "/usr/include/${BPF_ARCH}-linux-gnu/")

# ADD_BPF macro
macro(ADD_BPF Loader Input Output OutputSkel)

set(LOADER_SRC "${CMAKE_SOURCE_DIR}/${Loader}")
set(BPF_SRC "${CMAKE_SOURCE_DIR}/${Input}")
set(BPF_OBJ "${Output}")
set(SKEL_HDR "${OutputSkel}")

add_custom_command(OUTPUT ${BPF_OBJ}
		COMMAND clang -target bpf -g -O2 -c ${BPF_SRC} -o ${BPF_OBJ} -I${BPF_INCLUDE_DIR}
		VERBATIM
		DEPENDS ${BPF_SRC}
)

add_custom_command(OUTPUT ${SKEL_HDR}
		COMMAND bpftool gen skeleton ${BPF_OBJ} > ${SKEL_HDR}
		VERBATIM
		DEPENDS ${BPF_OBJ}
)

set_source_files_properties(${LOADER_SRC}
			PROPERTIES OBJECT_DEPENDS ${SKEL_HDR})

endmacro()
