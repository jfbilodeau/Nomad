foreach(REQUIRED IN ITEMS NOMAD_CLI NOMAD_SMOKE_PROJECT NOMAD_TEST_ROOT NOMAD_RUNTIME_SUFFIX NOMAD_PACKAGE_ARTIFACT_DIR)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

function(run_success DESCRIPTION)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR_OUTPUT
        ENCODING UTF-8 TIMEOUT 30)
    if(NOT RESULT STREQUAL "0")
        message(FATAL_ERROR "${DESCRIPTION} failed (${RESULT})\nstdout:\n${OUTPUT}\nstderr:\n${ERROR_OUTPUT}")
    endif()
endfunction()

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
file(MAKE_DIRECTORY "${NOMAD_TEST_ROOT}")
set(PROJECT_DIRECTORY "${NOMAD_TEST_ROOT}/project")
file(COPY "${NOMAD_SMOKE_PROJECT}/" DESTINATION "${PROJECT_DIRECTORY}")
run_success("Graphical smoke project compilation" "${NOMAD_CLI}" check "${PROJECT_DIRECTORY}")
run_success("Graphical smoke project packaging" "${NOMAD_CLI}" package "${PROJECT_DIRECTORY}" --force)
file(GLOB ARCHIVES "${PROJECT_DIRECTORY}/dist/nomad-package-smoke-*-0.1.0.zip")
list(LENGTH ARCHIVES ARCHIVE_COUNT)
if(NOT ARCHIVE_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one graphical smoke ZIP, got: ${ARCHIVES}")
endif()
list(GET ARCHIVES 0 ARCHIVE)
set(EXTRACTED "${NOMAD_TEST_ROOT}/extracted")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${EXTRACTED}")
file(READ "${EXTRACTED}/nomad.toml" MANIFEST)
string(REPLACE "entry = \"init\"" "entry = \"automated\"" AUTOMATED_MANIFEST "${MANIFEST}")
if(MANIFEST STREQUAL AUTOMATED_MANIFEST)
    message(FATAL_ERROR "Could not select automated smoke entry")
endif()
file(WRITE "${EXTRACTED}/nomad.toml" "${AUTOMATED_MANIFEST}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
        "${EXTRACTED}/nomad-package-smoke${NOMAD_RUNTIME_SUFFIX}"
    WORKING_DIRECTORY "${EXTRACTED}"
    RESULT_VARIABLE RESULT OUTPUT_VARIABLE OUTPUT ERROR_VARIABLE ERROR_OUTPUT
    ENCODING UTF-8 TIMEOUT 30
)
if(NOT RESULT STREQUAL "0" OR NOT OUTPUT MATCHES "PACKAGING_SMOKE_COMPLETE"
    OR "${OUTPUT}\n${ERROR_OUTPUT}" MATCHES "\\[(ERROR|FATAL)\\]")
    message(FATAL_ERROR "Graphical fixture initialization failed (${RESULT})\nstdout:\n${OUTPUT}\nstderr:\n${ERROR_OUTPUT}")
endif()
file(MAKE_DIRECTORY "${NOMAD_PACKAGE_ARTIFACT_DIR}")
get_filename_component(ARCHIVE_NAME "${ARCHIVE}" NAME)
file(COPY_FILE "${ARCHIVE}" "${NOMAD_PACKAGE_ARTIFACT_DIR}/${ARCHIVE_NAME}")
file(SHA256 "${ARCHIVE}" CHECKSUM)
file(WRITE "${NOMAD_PACKAGE_ARTIFACT_DIR}/SHA256SUMS.txt" "${CHECKSUM}  ${ARCHIVE_NAME}\n")
file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
