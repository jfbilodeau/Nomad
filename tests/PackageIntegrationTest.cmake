if(
    NOT DEFINED NOMAD_CLI OR
    NOT DEFINED NOMAD_COMPILER OR
    NOT DEFINED NOMAD_RUNTIME_SUFFIX OR
    NOT DEFINED NOMAD_TEST_ROOT
)
    message(FATAL_ERROR "NOMAD_CLI, NOMAD_COMPILER, NOMAD_RUNTIME_SUFFIX, and NOMAD_TEST_ROOT are required")
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
set(PACKAGE_DIR "${NOMAD_TEST_ROOT}/extracted")
set(OUTPUT_DIR "${PROJECT_DIR}/dist")
set(PACKAGED_RUNTIME "${PACKAGE_DIR}/package-game${NOMAD_RUNTIME_SUFFIX}")

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")

foreach(PROGRAM IN ITEMS "${NOMAD_CLI}" "${NOMAD_COMPILER}")
    foreach(ACTION IN ITEMS --help -h --version -v help version)
        run_success("Standalone ${ACTION}" "${PROGRAM}" "${ACTION}")
    endforeach()
    run_failure("Global help must stand alone" "${PROGRAM}" --help check)
    run_failure("Global version must stand alone" "${PROGRAM}" -v check)
    run_failure("Version is not a command option" "${PROGRAM}" check -v)
    run_failure("Version rejects positional arguments" "${PROGRAM}" version extra)
    run_success("Help verb" "${PROGRAM}" help check)
    run_success("Command help" "${PROGRAM}" check -h)
    run_failure("Unknown command" "${PROGRAM}" missing)
endforeach()
run_success("Package help" "${NOMAD_CLI}" package --help)
run_failure("Package flag rejected by run" "${NOMAD_CLI}" run --force)
run_success("Compiler docs help without required output" "${NOMAD_COMPILER}" docs -h)
run_failure("Compiler docs requires output" "${NOMAD_COMPILER}" docs)
run_failure("Compiler dump rejects docs options" "${NOMAD_COMPILER}" dump --output ignored.md)

run_success(
    "Project initialization"
    "${NOMAD_CLI}" init "${PROJECT_DIR}"
)

file(MAKE_DIRECTORY "${PROJECT_DIR}/res/development")
file(WRITE "${PROJECT_DIR}/res/development/notes.txt" "excluded")
file(WRITE "${PROJECT_DIR}/res/sprite.aseprite" "excluded")
file(WRITE "${PROJECT_DIR}/res/keep.txt" "included")
file(WRITE "${PROJECT_DIR}/res/日本語.txt" "Unicode resource")

run_success(
    "Package dry run"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --dry-run
)
reject_path("${OUTPUT_DIR}" "Package output after dry run")
reject_path("${PROJECT_DIR}/dist.tmp" "Package staging directory after dry run")
reject_path("${PROJECT_DIR}/dist.backup" "Package backup directory after dry run")

run_success(
    "Project packaging"
    "${NOMAD_CLI}" package "${PROJECT_DIR}"
)

file(GLOB ARCHIVES "${OUTPUT_DIR}/package-game-*-0.1.0.zip")
list(LENGTH ARCHIVES ARCHIVE_COUNT)
if(NOT ARCHIVE_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected exactly one named game ZIP, got: ${ARCHIVES}")
endif()
list(GET ARCHIVES 0 PACKAGE_ARCHIVE)
file(ARCHIVE_EXTRACT INPUT "${PACKAGE_ARCHIVE}" DESTINATION "${PACKAGE_DIR}")
file(GLOB OUTPUT_FILES "${OUTPUT_DIR}/*")
list(LENGTH OUTPUT_FILES OUTPUT_COUNT)
if(NOT OUTPUT_COUNT EQUAL 1)
    message(FATAL_ERROR "Packaging left files other than its ZIP: ${OUTPUT_FILES}")
endif()

require_path("${PACKAGE_DIR}/nomad.toml" "Release manifest")
require_path("${PACKAGE_DIR}/res/scripts/init.nomad" "Packaged entry script")
require_path("${PACKAGE_DIR}/res/keep.txt" "Included resource")
require_path("${PACKAGE_DIR}/res/日本語.txt" "Unicode resource")
file(READ "${PACKAGE_DIR}/res/日本語.txt" UNICODE_RESOURCE)
if(NOT UNICODE_RESOURCE STREQUAL "Unicode resource")
    message(FATAL_ERROR "Unicode resource contents changed during packaging")
endif()
require_path("${PACKAGED_RUNTIME}" "Packaged runtime")
require_path("${PACKAGE_DIR}/licenses/Nomad.txt" "Nomad license")
require_path("${PACKAGE_DIR}/licenses/SDL.txt" "SDL license")
require_path("${PACKAGE_DIR}/licenses/FreeType.txt" "FreeType license")
reject_path("${PACKAGE_DIR}/runtime.json" "Internal runtime bundle manifest")
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

file(SHA256 "${PACKAGE_ARCHIVE}" FIRST_CHECKSUM)
run_success(
    "Forced package replacement"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --force
)
file(SHA256 "${PACKAGE_ARCHIVE}" REPEATED_CHECKSUM)
if(NOT FIRST_CHECKSUM STREQUAL REPEATED_CHECKSUM)
    message(FATAL_ERROR "Repeated packaging produced different archive bytes")
endif()

file(WRITE "${PROJECT_DIR}/res/scripts/init.nomad" "missing.statement\n")
run_failure(
    "Package compilation gate"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --force
)
file(SHA256 "${PACKAGE_ARCHIVE}" PRESERVED_CHECKSUM)
if(NOT FIRST_CHECKSUM STREQUAL PRESERVED_CHECKSUM)
    message(FATAL_ERROR "Failed compilation changed the previous archive")
endif()

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
