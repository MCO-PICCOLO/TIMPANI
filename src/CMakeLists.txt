# Timpani-O executable
add_executable(timpani-o
  main.cpp
  piccolo_client.cpp
)
target_link_libraries(timpani-o
  PRIVATE
    schedinfo_proto
)

install(TARGETS timpani-o
  RUNTIME DESTINATION bin
)

file(CREATE_LINK
  ${CMAKE_CURRENT_BINARY_DIR}/timpani-o
  ${CMAKE_BINARY_DIR}/timpani-o
  SYMBOLIC
  COPY_ON_ERROR
)
