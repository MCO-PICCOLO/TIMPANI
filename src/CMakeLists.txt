# Timpani-O executable
add_executable(timpani-o
  main.cpp
  schedinfo_service.cpp
  fault_client.cpp
)
target_link_libraries(timpani-o
  PRIVATE
    schedinfo_proto
)

install(TARGETS timpani-o
  RUNTIME DESTINATION bin
)

set_target_properties(timpani-o
  PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
