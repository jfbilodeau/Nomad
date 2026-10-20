foreach(REQUIRED IN ITEMS NOMAD_CLI NOMAD_RUNTIME_SUFFIX NOMAD_SYSTEM_LIBRARY NOMAD_COLLECTIONS_LIBRARY NOMAD_GAME_LIBRARY NOMAD_OS_LIBRARY NOMAD_TEST_ROOT)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/TestProcess.cmake")

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
set(PROJECT "${NOMAD_TEST_ROOT}/system application")
nomadRunTestCommand("Create system application" COMMAND "${NOMAD_CLI}" init "${PROJECT}")
file(READ "${PROJECT}/nomad.toml" CONFIGURATION)
string(REGEX MATCH "executable = \"([^\"]+)\"" EXECUTABLE_FIELD "${CONFIGURATION}")
if(NOT EXECUTABLE_FIELD)
    message(FATAL_ERROR "Generated project has no executable name")
endif()
set(EXECUTABLE_NAME "${CMAKE_MATCH_1}")
string(REPLACE "modules = [\"nomad-game\"]" "modules = [\"nomad-system\"]" CONFIGURATION "${CONFIGURATION}")
string(REPLACE "entry = \"main\"\n" "" CONFIGURATION "${CONFIGURATION}")
file(WRITE "${PROJECT}/nomad.toml" "${CONFIGURATION}")
file(WRITE "${PROJECT}/res/scripts/main.nomad" "log.info \"SYSTEM_MODULE_COMPLETE\"\nsystem.exit\n")
nomadRunTestCommand("Check system-only application" COMMAND "${NOMAD_CLI}" check "${PROJECT}")
nomadRunTestCommand("Run system-only application" COMMAND "${NOMAD_CLI}" run "${PROJECT}"
    OUTPUT_MATCH "SYSTEM_MODULE_COMPLETE")
get_filename_component(SDK_DIRECTORY "${NOMAD_CLI}" DIRECTORY)
set(RUNTIME "${SDK_DIRECTORY}/nomad-runtime${NOMAD_RUNTIME_SUFFIX}")
nomadRunTestCommand("Select project directory from outside project"
    COMMAND "${RUNTIME}" --project-path "${PROJECT}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" OUTPUT_MATCH "SYSTEM_MODULE_COMPLETE")
nomadRunTestCommand("Select project configuration from outside project"
    COMMAND "${RUNTIME}" --project-path "${PROJECT}/nomad.toml"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" OUTPUT_MATCH "SYSTEM_MODULE_COMPLETE")
nomadRunTestCommand("Reject missing project path argument"
    EXPECT_FAILURE COMMAND "${RUNTIME}" --project-path
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}")
nomadRunTestCommand("Reject option in place of project path"
    EXPECT_FAILURE COMMAND "${RUNTIME}" --project-path --debug
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}")
nomadRunTestCommand("Package system-only application" COMMAND "${NOMAD_CLI}" package "${PROJECT}")
file(GLOB ARCHIVES "${PROJECT}/dist/*.zip")
list(LENGTH ARCHIVES COUNT)
if(NOT COUNT EQUAL 1)
    message(FATAL_ERROR "Expected one system application package: ${ARCHIVES}")
endif()
list(GET ARCHIVES 0 ARCHIVE)
set(EXTRACTED "${NOMAD_TEST_ROOT}/extracted")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${EXTRACTED}")
if(NOT EXISTS "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}" OR EXISTS "${EXTRACTED}/${NOMAD_GAME_LIBRARY}")
    message(FATAL_ERROR "System application did not include exactly the required Nomad modules")
endif()
file(GLOB GRAPHICAL_LIBRARIES "${EXTRACTED}/*SDL*" "${EXTRACTED}/*box2d*")
if(GRAPHICAL_LIBRARIES)
    message(FATAL_ERROR "System-only package contains graphical dependencies: ${GRAPHICAL_LIBRARIES}")
endif()
file(READ "${EXTRACTED}/nomad.toml" RELEASE)
if(RELEASE MATCHES "nomad-game" OR NOT RELEASE MATCHES "nomad-system")
    message(FATAL_ERROR "Release module list does not match the resolved system-only modules")
endif()
set(PROGRAM "${EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}")
nomadRunTestCommand("Run packaged system application" COMMAND "${PROGRAM}"
    WORKING_DIRECTORY "${EXTRACTED}" OUTPUT_MATCH "SYSTEM_MODULE_COMPLETE")
file(RENAME "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}" "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}.missing")
nomadRunTestCommand("Reject missing packaged module without SDK fallback"
    EXPECT_FAILURE COMMAND "${PROGRAM}" WORKING_DIRECTORY "${EXTRACTED}")

string(REPLACE "modules = [\"nomad-system\"]" "modules = [\"nomad-system\", \"nomad-collections\"]"
    CONFIGURATION "${CONFIGURATION}")
file(WRITE "${PROJECT}/nomad.toml" "${CONFIGURATION}")
nomadRunTestCommand("Check application with collections module" COMMAND "${NOMAD_CLI}" check "${PROJECT}")
nomadRunTestCommand("Run application with collections module" COMMAND "${NOMAD_CLI}" run "${PROJECT}"
    OUTPUT_MATCH "SYSTEM_MODULE_COMPLETE")
nomadRunTestCommand("Package application with collections module" COMMAND "${NOMAD_CLI}" package "${PROJECT}" --force)
file(GLOB ARCHIVES "${PROJECT}/dist/*.zip")
list(LENGTH ARCHIVES COUNT)
if(NOT COUNT EQUAL 1)
    message(FATAL_ERROR "Expected one collections application package: ${ARCHIVES}")
endif()
list(GET ARCHIVES 0 ARCHIVE)
set(COLLECTIONS_EXTRACTED "${NOMAD_TEST_ROOT}/collections-extracted")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${COLLECTIONS_EXTRACTED}")
if(NOT EXISTS "${COLLECTIONS_EXTRACTED}/${NOMAD_COLLECTIONS_LIBRARY}")
    message(FATAL_ERROR "Collections application package is missing the collections module")
endif()
nomadRunTestCommand("Run packaged application with collections module"
    COMMAND "${COLLECTIONS_EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}"
    WORKING_DIRECTORY "${COLLECTIONS_EXTRACTED}" OUTPUT_MATCH "SYSTEM_MODULE_COMPLETE")

string(REPLACE "modules = [\"nomad-system\", \"nomad-collections\"]"
    "modules = [\"nomad-system\", \"nomad-os\"]" CONFIGURATION "${CONFIGURATION}")
file(WRITE "${PROJECT}/nomad.toml" "${CONFIGURATION}")
file(WRITE "${PROJECT}/res/scripts/main.nomad"
    "assert os.argv.count == 0\nlog.info \"OS_EMPTY_ARGUMENTS_COMPLETE\"\n")
nomadRunTestCommand("Run OS application without separator"
    COMMAND "${NOMAD_CLI}" run "${PROJECT}" OUTPUT_MATCH "OS_EMPTY_ARGUMENTS_COMPLETE")
nomadRunTestCommand("Run OS application with empty argument list"
    COMMAND "${NOMAD_CLI}" run "${PROJECT}" -- OUTPUT_MATCH "OS_EMPTY_ARGUMENTS_COMPLETE")
file(WRITE "${PROJECT}/res/scripts/main.nomad" [=[
assert os.argv.count == 6
assert os.argv[0] == "Player 日本語"
assert os.argv[1] == "--help"
assert os.argv[2] == "--project-path"
assert os.argv[3] == "--debug"
assert os.argv[4] == "--no-debug"
assert os.argv[5] == "--"
assert os.currentDirectory.length > 0
assert os.executablePath.length > 0
assert os.tempDirectory.length > 0
os.argv[0] = "updated"
os.env["NOMAD_OS_LOCAL"] = "snapshot"
assert os.argv[0] == "updated"
assert os.env["NOMAD_OS_LOCAL"] == "snapshot"
log.info "OS_MODULE_COMPLETE"
]=])
set(APPLICATION_ARGUMENTS "Player 日本語" --help --project-path --debug --no-debug --)
nomadRunTestCommand("Check OS application without capturing host information"
    COMMAND "${NOMAD_CLI}" check "${PROJECT}")
nomadRunTestCommand("Forward application arguments through project manager"
    COMMAND "${NOMAD_CLI}" run "${PROJECT}" --debug -- ${APPLICATION_ARGUMENTS}
    OUTPUT_MATCH "OS_MODULE_COMPLETE")
nomadRunTestCommand("Direct runtime arguments stop at separator"
    COMMAND "${RUNTIME}" --project-path "${PROJECT}" -- ${APPLICATION_ARGUMENTS}
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" OUTPUT_MATCH "OS_MODULE_COMPLETE")
nomadRunTestCommand("Package OS application"
    COMMAND "${NOMAD_CLI}" package "${PROJECT}" --force)
file(GLOB ARCHIVES "${PROJECT}/dist/*.zip")
list(GET ARCHIVES 0 ARCHIVE)
set(OS_EXTRACTED "${NOMAD_TEST_ROOT}/os-extracted")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${OS_EXTRACTED}")
if(NOT EXISTS "${OS_EXTRACTED}/${NOMAD_OS_LIBRARY}" OR
    NOT EXISTS "${OS_EXTRACTED}/${NOMAD_COLLECTIONS_LIBRARY}" OR
    EXISTS "${OS_EXTRACTED}/${NOMAD_GAME_LIBRARY}")
    message(FATAL_ERROR "OS package did not include exactly its required Nomad modules")
endif()
file(READ "${OS_EXTRACTED}/nomad.toml" RELEASE)
if(NOT RELEASE MATCHES "nomad-os" OR NOT RELEASE MATCHES "nomad-collections")
    message(FATAL_ERROR "OS package configuration does not include resolved module dependencies")
endif()
nomadRunTestCommand("Packaged OS application receives application arguments"
    COMMAND "${OS_EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}" -- ${APPLICATION_ARGUMENTS}
    WORKING_DIRECTORY "${OS_EXTRACTED}" OUTPUT_MATCH "OS_MODULE_COMPLETE")
file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
