if(ENABLE_TESTING)
  # Include the test subdirectory.
  message("-- Adding p4testgen bmv2 test suite")
  include(test/P4Tests.cmake)
endif()

# Source files for p4testgen.
set(
  TESTGEN_SOURCES
  ${TESTGEN_SOURCES}
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/protobuf/protobuf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/ptf/ptf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/stf/stf.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/bmv2.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/cmd_stepper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/concolic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/constants.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/contrib/bmv2_hash/calculations.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/expr_stepper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/program_info.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/table_stepper.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/target.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test_backend.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/test_spec.cpp
  PARENT_SCOPE
)

# Link the run-bmv2-test binary
execute_process(COMMAND ln -sfn ${P4C_SOURCE_DIR}/backends/bmv2/run-bmv2-test.py ${CMAKE_BINARY_DIR}/run-bmv2-test.py)

set(
  TESTGEN_LIBS ${TESTGEN_LIBS}
  ${P4C_LIBRARIES}
  ${P4C_LIB_DEPS}
  ${CMAKE_THREAD_LIBS_INIT}
  PARENT_SCOPE
)
