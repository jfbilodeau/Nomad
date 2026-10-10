if(NOT DEFINED NOMAD_PACKAGE_ARCHIVE)
    file(GLOB ARCHIVES "${CMAKE_CURRENT_LIST_DIR}/*.zip")
    list(LENGTH ARCHIVES ARCHIVE_COUNT)
    if(NOT ARCHIVE_COUNT EQUAL 1)
        message(FATAL_ERROR "Expected exactly one package ZIP, got: ${ARCHIVES}")
    endif()
    list(GET ARCHIVES 0 NOMAD_PACKAGE_ARCHIVE)
endif()
if(NOT DEFINED NOMAD_PACKAGE_DIRECTORY)
    set(NOMAD_PACKAGE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/extracted")
endif()
if(EXISTS "${NOMAD_PACKAGE_DIRECTORY}")
    message(FATAL_ERROR "Smoke test requires a new extraction directory: ${NOMAD_PACKAGE_DIRECTORY}")
endif()

file(ARCHIVE_EXTRACT INPUT "${NOMAD_PACKAGE_ARCHIVE}" DESTINATION "${NOMAD_PACKAGE_DIRECTORY}")
set(RUNTIME "${NOMAD_PACKAGE_DIRECTORY}/package-game")
if(CMAKE_HOST_WIN32)
    string(APPEND RUNTIME ".exe")
    set(SYSTEM_PATH "$ENV{SystemRoot}/System32;$ENV{SystemRoot}")
else()
    set(SYSTEM_PATH "/usr/bin:/bin")
    execute_process(COMMAND /usr/bin/test -x "${RUNTIME}" RESULT_VARIABLE EXECUTABLE_RESULT)
    if(NOT EXECUTABLE_RESULT STREQUAL "0")
        message(FATAL_ERROR "Extracted runtime is not executable: ${RUNTIME}")
    endif()
    execute_process(COMMAND /usr/bin/test -x "${NOMAD_PACKAGE_DIRECTORY}/res/keep.txt"
        RESULT_VARIABLE RESOURCE_RESULT)
    if(NOT RESOURCE_RESULT STREQUAL "1")
        message(FATAL_ERROR "Extracted regular resource must not be executable")
    endif()
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        foreach(FILE_MODE IN ITEMS "${RUNTIME}|755" "${NOMAD_PACKAGE_DIRECTORY}/res/keep.txt|644")
            string(REPLACE "|" ";" PARTS "${FILE_MODE}")
            list(GET PARTS 0 FILE_PATH)
            list(GET PARTS 1 EXPECTED_MODE)
            execute_process(COMMAND /usr/bin/stat -c %a "${FILE_PATH}"
                RESULT_VARIABLE MODE_RESULT OUTPUT_VARIABLE MODE OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT MODE_RESULT STREQUAL "0" OR NOT MODE STREQUAL EXPECTED_MODE)
                message(FATAL_ERROR "Expected mode ${EXPECTED_MODE} for ${FILE_PATH}, got: ${MODE}")
            endif()
        endforeach()
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/TestProcess.cmake")
nomadRunTestCommand("Packaged runtime startup"
    COMMAND "${CMAKE_COMMAND}" -E env
        --unset=LD_LIBRARY_PATH --unset=LD_PRELOAD --unset=DYLD_LIBRARY_PATH
        --unset=DYLD_FALLBACK_LIBRARY_PATH
        "PATH=${SYSTEM_PATH}"
        "${RUNTIME}" --help
    WORKING_DIRECTORY "${NOMAD_PACKAGE_DIRECTORY}"
    OUTPUT_MATCH "Allowed options"
)
message(STATUS "Packaged runtime starts with only package-local and system dependency search paths")
