if(
    NOT DEFINED NOMAD_CLI OR
    NOT DEFINED NOMAD_RUNTIME_SUFFIX OR
    NOT DEFINED NOMAD_TEST_ROOT
)
    message(FATAL_ERROR "NOMAD_CLI, NOMAD_RUNTIME_SUFFIX, and NOMAD_TEST_ROOT are required")
endif()

function(run_success DESCRIPTION)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT
        ERROR_VARIABLE ERROR_OUTPUT
        ENCODING UTF-8
    )

    if(NOT RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "${DESCRIPTION} failed with exit code ${RESULT}\n"
            "stdout:\n${OUTPUT}\n"
            "stderr:\n${ERROR_OUTPUT}"
        )
    endif()
endfunction()

function(run_failure DESCRIPTION)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE RESULT
        OUTPUT_VARIABLE OUTPUT
        ERROR_VARIABLE ERROR_OUTPUT
        ENCODING UTF-8
    )

    if(RESULT EQUAL 0)
        message(
            FATAL_ERROR
            "${DESCRIPTION} unexpectedly succeeded\n"
            "stdout:\n${OUTPUT}\n"
            "stderr:\n${ERROR_OUTPUT}"
        )
    endif()
endfunction()

function(require_path PATH DESCRIPTION)
    if(NOT EXISTS "${PATH}")
        message(FATAL_ERROR "${DESCRIPTION} does not exist: ${PATH}")
    endif()
endfunction()

function(reject_path PATH DESCRIPTION)
    if(EXISTS "${PATH}")
        message(FATAL_ERROR "${DESCRIPTION} should not exist: ${PATH}")
    endif()
endfunction()

set(PROJECT_DIR "${NOMAD_TEST_ROOT}/Package 日本語 Game")
set(PACKAGE_DIR "${PROJECT_DIR}/dist")
set(PACKAGED_RUNTIME "${PACKAGE_DIR}/package-game${NOMAD_RUNTIME_SUFFIX}")

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")

run_success(
    "Project initialization"
    "${NOMAD_CLI}" init "${PROJECT_DIR}"
)

file(MAKE_DIRECTORY "${PROJECT_DIR}/res/development")
file(WRITE "${PROJECT_DIR}/res/development/notes.txt" "excluded")
file(WRITE "${PROJECT_DIR}/res/sprite.aseprite" "excluded")
file(WRITE "${PROJECT_DIR}/res/keep.txt" "included")

run_success(
    "Project packaging"
    "${NOMAD_CLI}" package "${PROJECT_DIR}"
)

require_path("${PACKAGE_DIR}/nomad.toml" "Release manifest")
require_path("${PACKAGE_DIR}/res/scripts/init.nomad" "Packaged entry script")
require_path("${PACKAGE_DIR}/res/keep.txt" "Included resource")
require_path("${PACKAGED_RUNTIME}" "Packaged runtime")
reject_path("${PACKAGE_DIR}/res/development/notes.txt" "Excluded development resource")
reject_path("${PACKAGE_DIR}/res/sprite.aseprite" "Excluded Aseprite resource")

file(READ "${PACKAGE_DIR}/nomad.toml" RELEASE_MANIFEST)

foreach(DEVELOPMENT_FIELD IN ITEMS "executable" "[resources]" "[package]")
    string(FIND "${RELEASE_MANIFEST}" "${DEVELOPMENT_FIELD}" FIELD_POSITION)

    if(NOT FIELD_POSITION EQUAL -1)
        message(FATAL_ERROR "Release manifest contains development field: ${DEVELOPMENT_FIELD}")
    endif()
endforeach()

run_success(
    "Release package validation"
    "${NOMAD_CLI}" check "${PACKAGE_DIR}"
)
run_success(
    "Packaged runtime startup"
    "${PACKAGED_RUNTIME}" --help
)

run_failure(
    "Package overwrite protection"
    "${NOMAD_CLI}" package "${PROJECT_DIR}"
)

file(WRITE "${PACKAGE_DIR}/stale.txt" "stale")
run_success(
    "Forced package replacement"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --force
)
reject_path("${PACKAGE_DIR}/stale.txt" "Replaced package file")

file(WRITE "${PACKAGE_DIR}/preserved.txt" "preserved")
file(WRITE "${PROJECT_DIR}/res/scripts/init.nomad" "missing.statement\n")
run_failure(
    "Package compilation gate"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --force
)
require_path("${PACKAGE_DIR}/preserved.txt" "Previous package after failed compilation")

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
