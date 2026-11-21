foreach(CASE IN ITEMS deferred-free unfinalized-free unfinalized-size unfinalized-fields failed-layout-fields)
    execute_process(
        COMMAND "${NOMAD_VALUE_CLEANUP_TEST}" "${CASE}"
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT
        ERROR_VARIABLE ERROR
        TIMEOUT 10)
    if(NOT "${RESULT}" STREQUAL "86")
        message(FATAL_ERROR "${CASE}: expected terminate exit 86, got '${RESULT}'\n${OUTPUT}\n${ERROR}")
    endif()
endforeach()
