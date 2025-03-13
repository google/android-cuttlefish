#
#
# compiling/installing sources for cn-cbor

set ( cbor_srcs
      cn-cbor.c
      cn-create.c
      cn-encoder.c
      cn-error.c
      cn-get.c
)

if (align_reads)
  add_definitions(-DCBOR_ALIGN_READS)
endif()
if (use_context)
  add_definitions(-DUSE_CBOR_CONTEXT)
endif()
add_library ( cn-cbor SHARED ${cbor_srcs} )
target_include_directories ( cn-cbor PUBLIC ../include )
target_include_directories ( cn-cbor PRIVATE ../src )

install ( TARGETS cn-cbor
          LIBRARY DESTINATION lib
          ARCHIVE DESTINATION lib
          RUNTIME DESTINATION bin)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
if (coveralls)
    include(Coveralls)
    coveralls_turn_on_coverage()

    set(COVERAGE_SRCS "")
    foreach (S ${cbor_srcs})
      get_filename_component(S_ABS ${S} ABSOLUTE)
      list (APPEND COVERAGE_SRCS ${S_ABS})
    endforeach()

    # Create the coveralls target.
    coveralls_setup(
        "${COVERAGE_SRCS}"
        ${coveralls_send}                 # If we should upload.
    )

    #add_dependencies(coveralls, all)
endif()

add_custom_target(size
  COMMAND echo "${CMAKE_BINARY_DIR}/src/CMakeFiles/cn-cbor.dir/cn-cbor.c.o"
  COMMAND size "${CMAKE_BINARY_DIR}/src/CMakeFiles/cn-cbor.dir/cn-cbor.c.o"
  COMMAND size -m "${CMAKE_BINARY_DIR}/src/CMakeFiles/cn-cbor.dir/cn-cbor.c.o"
  DEPENDS cn-cbor
COMMENT "Output the size of the parse routine")
