foreach(REQUIRED IN ITEMS NOMAD_CLI NOMAD_RUNTIME_SUFFIX NOMAD_SYSTEM_LIBRARY NOMAD_COLLECTIONS_LIBRARY NOMAD_GAME_LIBRARY NOMAD_OS_LIBRARY NOMAD_LOG_LIBRARY NOMAD_TEST_ROOT)
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
file(WRITE "${PROJECT}/res/scripts/main.nomad" "print \"SYSTEM_MODULE_COMPLETE\"\nsystem.exit\n")
nomadRunTestCommand("Check system-only application" COMMAND "${NOMAD_CLI}" check "${PROJECT}")
nomadRunTestCommand("Reject running an API-only application" EXPECT_FAILURE
    COMMAND "${NOMAD_CLI}" run "${PROJECT}")
get_filename_component(SDK_DIRECTORY "${NOMAD_CLI}" DIRECTORY)
set(RUNTIME "${SDK_DIRECTORY}/nomad-runtime${NOMAD_RUNTIME_SUFFIX}")
nomadRunTestCommand("Select project directory from outside project"
    COMMAND "${RUNTIME}" --project-path "${PROJECT}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" EXPECT_FAILURE)
nomadRunTestCommand("Select project configuration from outside project"
    COMMAND "${RUNTIME}" --project-path "${PROJECT}/nomad.toml"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" EXPECT_FAILURE)
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
if(NOT EXISTS "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}" OR EXISTS "${EXTRACTED}/${NOMAD_GAME_LIBRARY}"
    OR EXISTS "${EXTRACTED}/${NOMAD_LOG_LIBRARY}")
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
nomadRunTestCommand("Reject running a packaged API-only application" COMMAND "${PROGRAM}"
    WORKING_DIRECTORY "${EXTRACTED}" EXPECT_FAILURE)
file(RENAME "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}" "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}.missing")
nomadRunTestCommand("Reject missing packaged module without SDK fallback"
    EXPECT_FAILURE COMMAND "${PROGRAM}" WORKING_DIRECTORY "${EXTRACTED}")

string(REPLACE "modules = [\"nomad-system\"]" "modules = [\"nomad-system\", \"nomad-collections\"]"
    CONFIGURATION "${CONFIGURATION}")
file(WRITE "${PROJECT}/nomad.toml" "${CONFIGURATION}")
nomadRunTestCommand("Check application with collections module" COMMAND "${NOMAD_CLI}" check "${PROJECT}")
nomadRunTestCommand("Reject running API-only collections modules" COMMAND "${NOMAD_CLI}" run "${PROJECT}"
    EXPECT_FAILURE)
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
nomadRunTestCommand("Reject running packaged API-only collections modules"
    COMMAND "${COLLECTIONS_EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}"
    WORKING_DIRECTORY "${COLLECTIONS_EXTRACTED}" EXPECT_FAILURE)

string(REPLACE "modules = [\"nomad-system\", \"nomad-collections\"]"
    "modules = [\"nomad-game\", \"nomad-os\"]" CONFIGURATION "${CONFIGURATION}")
set(ENV{SDL_VIDEODRIVER} "dummy")
set(ENV{SDL_AUDIODRIVER} "dummy")
file(WRITE "${PROJECT}/nomad.toml" "${CONFIGURATION}")
file(WRITE "${PROJECT}/res/scripts/main.nomad"
    "assert os.argv.count == 0\nprint \"OS_EMPTY_ARGUMENTS_COMPLETE\"\nsystem.exit\n")
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
assert os.env.contains "NOMAD_OS_LOCAL"
assert os.env["NOMAD_OS_LOCAL"] == "snapshot"
print "OS_MODULE_COMPLETE"
system.exit
]=])
set(APPLICATION_ARGUMENTS "Player 日本語" --help --project-path --debug --no-debug --)
set(ENV{NOMAD_OS_LOCAL} "snapshot")
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
    NOT EXISTS "${OS_EXTRACTED}/${NOMAD_GAME_LIBRARY}")
    message(FATAL_ERROR "OS package did not include exactly its required Nomad modules")
endif()
file(READ "${OS_EXTRACTED}/nomad.toml" RELEASE)
if(NOT RELEASE MATCHES "nomad-os" OR NOT RELEASE MATCHES "nomad-collections")
    message(FATAL_ERROR "OS package configuration does not include resolved module dependencies")
endif()
nomadRunTestCommand("Packaged OS application receives application arguments"
    COMMAND "${OS_EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}" -- ${APPLICATION_ARGUMENTS}
    WORKING_DIRECTORY "${OS_EXTRACTED}" OUTPUT_MATCH "OS_MODULE_COMPLETE")
foreach(ASSIGNMENT IN ITEMS "os.argv[0] = \"updated\"" "os.env[\"key\"] = \"updated\"")
    file(WRITE "${PROJECT}/res/scripts/main.nomad" "${ASSIGNMENT}\nsystem.exit\n")
    nomadRunTestCommand("Reject writing OS collection contents: ${ASSIGNMENT}"
        COMMAND "${NOMAD_CLI}" check "${PROJECT}" EXPECT_FAILURE ERROR_VARIABLE DIAGNOSTIC)
    if(NOT "${DIAGNOSTIC}" MATCHES "read-only")
        message(FATAL_ERROR "Expected read-only diagnostic for ${ASSIGNMENT}: ${DIAGNOSTIC}")
    endif()
endforeach()

set(LOG_PROJECT "${NOMAD_TEST_ROOT}/log application")
nomadRunTestCommand("Create log application" COMMAND "${NOMAD_CLI}" init "${LOG_PROJECT}")
file(READ "${LOG_PROJECT}/nomad.toml" LOG_CONFIGURATION)
string(REPLACE "modules = [\"nomad-game\"]" "modules = [\"nomad-log\"]"
    LOG_CONFIGURATION "${LOG_CONFIGURATION}")
file(WRITE "${LOG_PROJECT}/nomad.toml" "${LOG_CONFIGURATION}")
file(WRITE "${LOG_PROJECT}/res/scripts/main.nomad"
    "log.debug \"debug\"\nlog.info \"info\"\nlog.warning \"warning\"\n"
    "log.error \"error\"\nlog.critical \"critical\"\n")
nomadRunTestCommand("Check independent log module" COMMAND "${NOMAD_CLI}" check "${LOG_PROJECT}")
nomadRunTestCommand("Package independent log module" COMMAND "${NOMAD_CLI}" package "${LOG_PROJECT}")
file(GLOB LOG_ARCHIVES "${LOG_PROJECT}/dist/*.zip")
list(LENGTH LOG_ARCHIVES LOG_ARCHIVE_COUNT)
if(NOT LOG_ARCHIVE_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected one log application package: ${LOG_ARCHIVES}")
endif()
list(GET LOG_ARCHIVES 0 LOG_ARCHIVE)
set(LOG_EXTRACTED "${NOMAD_TEST_ROOT}/log-extracted")
file(ARCHIVE_EXTRACT INPUT "${LOG_ARCHIVE}" DESTINATION "${LOG_EXTRACTED}")
if(NOT EXISTS "${LOG_EXTRACTED}/${NOMAD_LOG_LIBRARY}"
    OR EXISTS "${LOG_EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}"
    OR EXISTS "${LOG_EXTRACTED}/${NOMAD_GAME_LIBRARY}"
    OR EXISTS "${LOG_EXTRACTED}/${NOMAD_OS_LIBRARY}"
    OR EXISTS "${LOG_EXTRACTED}/${NOMAD_COLLECTIONS_LIBRARY}")
    message(FATAL_ERROR "Log-only package did not include exactly its required Nomad modules")
endif()
string(REPLACE "modules = [\"nomad-log\"]" "modules = [\"nomad-game\", \"nomad-log\"]"
    LOG_CONFIGURATION "${LOG_CONFIGURATION}")
file(WRITE "${LOG_PROJECT}/nomad.toml" "${LOG_CONFIGURATION}")
file(APPEND "${LOG_PROJECT}/res/scripts/main.nomad" "print \"LOG_MODULE_COMPLETE\"\nsystem.exit\n")
nomadRunTestCommand("Run game with explicit log module"
    COMMAND "${NOMAD_CLI}" run "${LOG_PROJECT}" OUTPUT_MATCH "LOG_MODULE_COMPLETE")
file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
