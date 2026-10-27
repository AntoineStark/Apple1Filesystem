execute_process(
  COMMAND "${TERMINAL}" --turbo --cycles 12000000 --input "IPRINT 2+3\r"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  INPUT_FILE /dev/null
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors
  TIMEOUT 20)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Terminal returned ${result}: ${errors}")
endif()
string(REPLACE "\r" "" output "${output}")
if(NOT output MATCHES "\n5\n")
  message(FATAL_ERROR "Terminal BASIC did not print 5:\n${output}")
endif()
message(STATUS "Terminal boot, paced input, BASIC arithmetic, and stdin EOF passed")