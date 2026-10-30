foreach(REQUIRED IN ITEMS NOMAD_CLI NOMAD_RUNTIME_SUFFIX NOMAD_SYSTEM_LIBRARY NOMAD_COLLECTIONS_LIBRARY NOMAD_GAME_LIBRARY NOMAD_OS_LIBRARY NOMAD_LOG_LIBRARY NOMAD_FIXTURE_LIBRARY NOMAD_TEST_ROOT)
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
string(REPLACE "modules = [\"game\"]" "modules = [\"system\"]" CONFIGURATION "${CONFIGURATION}")
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
if(NOT EXISTS "${EXTRACTED}/modules/system/${NOMAD_SYSTEM_LIBRARY}" OR EXISTS "${EXTRACTED}/modules/game"
    OR EXISTS "${EXTRACTED}/modules/log" OR EXISTS "${EXTRACTED}/${NOMAD_SYSTEM_LIBRARY}")
    message(FATAL_ERROR "System application did not include exactly the required Nomad modules")
endif()
file(GLOB GRAPHICAL_LIBRARIES "${EXTRACTED}/*SDL*" "${EXTRACTED}/*box2d*")
if(GRAPHICAL_LIBRARIES)
    message(FATAL_ERROR "System-only package contains graphical dependencies: ${GRAPHICAL_LIBRARIES}")
endif()
file(READ "${EXTRACTED}/nomad.toml" RELEASE)
if(RELEASE MATCHES "[\"']game[\"']" OR NOT RELEASE MATCHES "[\"']system[\"']")
    message(FATAL_ERROR "Release module list does not match the resolved system-only modules")
endif()
set(PROGRAM "${EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}")
nomadRunTestCommand("Reject running a packaged API-only application" COMMAND "${PROGRAM}"
    WORKING_DIRECTORY "${EXTRACTED}" EXPECT_FAILURE)
file(RENAME "${EXTRACTED}/modules/system/${NOMAD_SYSTEM_LIBRARY}"
    "${EXTRACTED}/modules/system/${NOMAD_SYSTEM_LIBRARY}.missing")
nomadRunTestCommand("Reject missing packaged module without SDK fallback"
    EXPECT_FAILURE COMMAND "${PROGRAM}" WORKING_DIRECTORY "${EXTRACTED}")

string(REPLACE "modules = [\"system\"]" "modules = [\"system\", \"collections\"]"
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
if(NOT EXISTS "${COLLECTIONS_EXTRACTED}/modules/collections/${NOMAD_COLLECTIONS_LIBRARY}")
    message(FATAL_ERROR "Collections application package is missing the collections module")
endif()
nomadRunTestCommand("Reject running packaged API-only collections modules"
    COMMAND "${COLLECTIONS_EXTRACTED}/${EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}"
    WORKING_DIRECTORY "${COLLECTIONS_EXTRACTED}" EXPECT_FAILURE)

string(REPLACE "modules = [\"system\", \"collections\"]"
    "modules = [\"game\", \"os\"]" CONFIGURATION "${CONFIGURATION}")
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
if(NOT EXISTS "${OS_EXTRACTED}/modules/os/${NOMAD_OS_LIBRARY}" OR
    NOT EXISTS "${OS_EXTRACTED}/modules/collections/${NOMAD_COLLECTIONS_LIBRARY}" OR
    NOT EXISTS "${OS_EXTRACTED}/modules/game/${NOMAD_GAME_LIBRARY}")
    message(FATAL_ERROR "OS package did not include exactly its required Nomad modules")
endif()
file(READ "${OS_EXTRACTED}/nomad.toml" RELEASE)
if(NOT RELEASE MATCHES "[\"']os[\"']" OR NOT RELEASE MATCHES "[\"']collections[\"']")
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
string(REPLACE "modules = [\"game\"]" "modules = [\"log\"]"
    LOG_CONFIGURATION "${LOG_CONFIGURATION}")
string(REGEX MATCH "executable = \"([^\"]+)\"" LOG_EXECUTABLE_FIELD "${LOG_CONFIGURATION}")
if(NOT LOG_EXECUTABLE_FIELD)
    message(FATAL_ERROR "Generated log project has no executable name")
endif()
set(LOG_EXECUTABLE_NAME "${CMAKE_MATCH_1}")
string(APPEND LOG_CONFIGURATION
    "\n[log]\nlevel = 'debug'\ndelivery-mode = 'synchronous'\n"
    "[[log.sinks]]\ntype = 'file'\nlevel = 'debug'\ndirectory = 'runtime-logs'\n"
    "name = 'integration'\nrotate-at-startup = false\n")
file(WRITE "${LOG_PROJECT}/nomad.toml" "${LOG_CONFIGURATION}")
file(WRITE "${LOG_PROJECT}/res/scripts/main.nomad"
    "log.debug \"debug\"\nlog.info \"info\"\nlog.warning \"warning\"\n"
    "log.error \"error\"\nlog.critical \"critical\"\n")
nomadRunTestCommand("Check independent log module" COMMAND "${NOMAD_CLI}" check "${LOG_PROJECT}")
nomadRunTestCommand("Preview independent log package"
    COMMAND "${NOMAD_CLI}" package "${LOG_PROJECT}" --dry-run)
nomadRunTestCommand("Package independent log module" COMMAND "${NOMAD_CLI}" package "${LOG_PROJECT}")
if(EXISTS "${LOG_PROJECT}/runtime-logs")
    message(FATAL_ERROR "Log checking or packaging activated file sinks")
endif()
file(GLOB LOG_ARCHIVES "${LOG_PROJECT}/dist/*.zip")
list(LENGTH LOG_ARCHIVES LOG_ARCHIVE_COUNT)
if(NOT LOG_ARCHIVE_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected one log application package: ${LOG_ARCHIVES}")
endif()
list(GET LOG_ARCHIVES 0 LOG_ARCHIVE)
set(LOG_EXTRACTED "${NOMAD_TEST_ROOT}/log-extracted")
file(ARCHIVE_EXTRACT INPUT "${LOG_ARCHIVE}" DESTINATION "${LOG_EXTRACTED}")
if(NOT EXISTS "${LOG_EXTRACTED}/modules/log/${NOMAD_LOG_LIBRARY}"
    OR EXISTS "${LOG_EXTRACTED}/modules/system"
    OR EXISTS "${LOG_EXTRACTED}/modules/game"
    OR EXISTS "${LOG_EXTRACTED}/modules/os"
    OR EXISTS "${LOG_EXTRACTED}/modules/collections")
    message(FATAL_ERROR "Log-only package did not include exactly its required Nomad modules")
endif()
string(REPLACE "modules = [\"log\"]" "modules = [\"game\", \"log\"]"
    LOG_CONFIGURATION "${LOG_CONFIGURATION}")
file(WRITE "${LOG_PROJECT}/nomad.toml" "${LOG_CONFIGURATION}")
file(APPEND "${LOG_PROJECT}/res/scripts/main.nomad" "print \"LOG_MODULE_COMPLETE\"\nsystem.exit\n")
nomadRunTestCommand("Run game with explicit log module"
    COMMAND "${NOMAD_CLI}" run "${LOG_PROJECT}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" OUTPUT_MATCH "LOG_MODULE_COMPLETE")
file(READ "${LOG_PROJECT}/runtime-logs/integration.log" LOG_OUTPUT)
if(NOT LOG_OUTPUT MATCHES "\\[DEBUG\\] debug" OR NOT LOG_OUTPUT MATCHES "\\[CRITICAL\\] critical")
    message(FATAL_ERROR "Development launch did not write configured log output: ${LOG_OUTPUT}")
endif()
nomadRunTestCommand("Package game with configured logging"
    COMMAND "${NOMAD_CLI}" package "${LOG_PROJECT}" --force)
set(LOG_GAME_EXTRACTED "${NOMAD_TEST_ROOT}/log-game-extracted")
file(ARCHIVE_EXTRACT INPUT "${LOG_ARCHIVE}" DESTINATION "${LOG_GAME_EXTRACTED}")
if(EXISTS "${LOG_GAME_EXTRACTED}/runtime-logs")
    message(FATAL_ERROR "Generated development logs were included in the package")
endif()
nomadRunTestCommand("Run packaged game with configured logging from another directory"
    COMMAND "${LOG_GAME_EXTRACTED}/${LOG_EXECUTABLE_NAME}${NOMAD_RUNTIME_SUFFIX}"
    --project-path "${LOG_GAME_EXTRACTED}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" OUTPUT_MATCH "LOG_MODULE_COMPLETE")
file(READ "${LOG_GAME_EXTRACTED}/runtime-logs/integration.log" LOG_RELEASE_OUTPUT)
if(NOT LOG_RELEASE_OUTPUT MATCHES "\\[DEBUG\\] debug"
    OR NOT LOG_RELEASE_OUTPUT MATCHES "\\[CRITICAL\\] critical")
    message(FATAL_ERROR "Packaged launch did not write configured log output: ${LOG_RELEASE_OUTPUT}")
endif()
if(EXISTS "${NOMAD_TEST_ROOT}/runtime-logs")
    message(FATAL_ERROR "Configured log paths were resolved relative to the working directory")
endif()

set(SCRIPT_SDK "${NOMAD_TEST_ROOT}/script-sdk")
set(SCRIPT_PROJECT "${NOMAD_TEST_ROOT}/module script application")
file(MAKE_DIRECTORY "${SCRIPT_SDK}/modules")
set(NOMAD_RUNTIME_DESTINATION "${SCRIPT_SDK}")
foreach(PROGRAM IN ITEMS "${NOMAD_CLI}" "${RUNTIME}" "${SDK_DIRECTORY}/nomadc${NOMAD_RUNTIME_SUFFIX}")
    set(NOMAD_RUNTIME "${PROGRAM}")
    unset(NOMAD_RUNTIME_DEPENDENCIES)
    if(WIN32)
        set(NOMAD_RUNTIME_DEPENDENCIES "${NOMAD_SCRIPT_SDK_RUNTIME_DEPENDENCIES}")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundleRuntimeDependencies.cmake")
endforeach()
file(COPY "${SDK_DIRECTORY}/templates" "${SDK_DIRECTORY}/runtime" DESTINATION "${SCRIPT_SDK}")
file(COPY "${SDK_DIRECTORY}/modules/system" DESTINATION "${SCRIPT_SDK}/modules")
file(MAKE_DIRECTORY "${SCRIPT_SDK}/modules/fixture")
file(COPY "${NOMAD_FIXTURE_LIBRARY}" DESTINATION "${SCRIPT_SDK}/modules/fixture")
get_filename_component(FIXTURE_LIBRARY_NAME "${NOMAD_FIXTURE_LIBRARY}" NAME)
file(WRITE "${SCRIPT_SDK}/modules/fixture/module.toml"
    "schema = 1\n[module]\nname = 'fixture'\n[nomad]\nmodules = ['system']\n")
file(WRITE "${SCRIPT_SDK}/modules/fixture/runtime.json"
    "{\"schema\":1,\"files\":[{\"path\":\"${FIXTURE_LIBRARY_NAME}\",\"role\":\"library\"}]}")
file(MAKE_DIRECTORY "${SCRIPT_SDK}/modules/system/scripts" "${SCRIPT_SDK}/modules/fixture/scripts")
file(WRITE "${SCRIPT_SDK}/modules/system/scripts/main.nomad"
    "fun dependencyAnswer\nreturn 40\nend\nassert false\n")
file(WRITE "${SCRIPT_SDK}/modules/fixture/scripts/main.nomad" [=[
open hub packaged
class Configuration
answer = moduleDefault
end
end
fun moduleDefault
print "MODULE_DEFAULT_INITIALIZED"
return dependencyAnswer
end
fun moduleAnswer
return 42
end
print "MODULE_FILE_BODY_EXECUTED"
assert false
]=])
nomadRunTestCommand("Initialize module-script application"
    COMMAND "${SCRIPT_SDK}/nomad${NOMAD_RUNTIME_SUFFIX}" init "${SCRIPT_PROJECT}")
file(READ "${SCRIPT_PROJECT}/nomad.toml" SCRIPT_CONFIGURATION)
string(REGEX MATCH "executable = \"([^\"]+)\"" SCRIPT_EXECUTABLE_FIELD "${SCRIPT_CONFIGURATION}")
if(NOT SCRIPT_EXECUTABLE_FIELD)
    message(FATAL_ERROR "Generated module-script project has no executable name")
endif()
set(SCRIPT_EXECUTABLE "${CMAKE_MATCH_1}${NOMAD_RUNTIME_SUFFIX}")
string(REPLACE "modules = [\"game\"]" "modules = [\"fixture\"]"
    SCRIPT_CONFIGURATION "${SCRIPT_CONFIGURATION}")
file(WRITE "${SCRIPT_PROJECT}/nomad.toml" "${SCRIPT_CONFIGURATION}\n[fixture]\nentry = 'main'\n")
file(WRITE "${SCRIPT_PROJECT}/res/scripts/main.nomad" [=[
settings = new packaged.Configuration
assert settings.answer == 40
answer = moduleAnswer
nativeValue = toInt "2"
assert answer == settings.answer + nativeValue
assert fixture.argumentCount == 2
name = fixture.projectName
assert name.length > 0
print "MODULE_SCRIPTS_COMPLETE"
system.exit
]=])
nomadRunTestCommand("Preview module-script package"
    COMMAND "${SCRIPT_SDK}/nomad${NOMAD_RUNTIME_SUFFIX}" package "${SCRIPT_PROJECT}" --dry-run
    OUTPUT_VARIABLE SCRIPT_PREVIEW)
if(EXISTS "${SCRIPT_PROJECT}/dist" OR SCRIPT_PREVIEW MATCHES "MODULE_DEFAULT_INITIALIZED|MODULE_FILE_BODY_EXECUTED")
    message(FATAL_ERROR "Module-script dry run wrote output or executed module code")
endif()
nomadRunTestCommand("Package module-script application"
    COMMAND "${SCRIPT_SDK}/nomad${NOMAD_RUNTIME_SUFFIX}" package "${SCRIPT_PROJECT}"
    OUTPUT_VARIABLE SCRIPT_PACKAGE_OUTPUT)
if(SCRIPT_PACKAGE_OUTPUT MATCHES "MODULE_DEFAULT_INITIALIZED|MODULE_FILE_BODY_EXECUTED")
    message(FATAL_ERROR "Module packaging executed script code")
endif()
file(GLOB SCRIPT_ARCHIVES "${SCRIPT_PROJECT}/dist/*.zip")
list(LENGTH SCRIPT_ARCHIVES SCRIPT_ARCHIVE_COUNT)
if(NOT SCRIPT_ARCHIVE_COUNT EQUAL 1)
    message(FATAL_ERROR "Expected one module-script package: ${SCRIPT_ARCHIVES}")
endif()
list(GET SCRIPT_ARCHIVES 0 SCRIPT_ARCHIVE)
set(SCRIPT_EXTRACTED "${NOMAD_TEST_ROOT}/script-extracted")
file(ARCHIVE_EXTRACT INPUT "${SCRIPT_ARCHIVE}" DESTINATION "${SCRIPT_EXTRACTED}")
set(SCRIPT_RELOCATED "${NOMAD_TEST_ROOT}/relocated module application")
file(RENAME "${SCRIPT_EXTRACTED}" "${SCRIPT_RELOCATED}")
file(RENAME "${SCRIPT_SDK}" "${SCRIPT_SDK}.unavailable")
file(RENAME "${SCRIPT_PROJECT}" "${SCRIPT_PROJECT}.unavailable")
if(CMAKE_HOST_WIN32)
    set(ENV{PATH} "$ENV{SystemRoot}/System32;$ENV{SystemRoot}")
else()
    set(ENV{PATH} "/usr/bin:/bin")
endif()
unset(ENV{LD_LIBRARY_PATH})
unset(ENV{LD_PRELOAD})
unset(ENV{DYLD_LIBRARY_PATH})
unset(ENV{DYLD_FALLBACK_LIBRARY_PATH})
nomadRunTestCommand("Run relocated module-script application without its SDK or sources"
    COMMAND "${SCRIPT_RELOCATED}/${SCRIPT_EXECUTABLE}" --project-path "${SCRIPT_RELOCATED}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" OUTPUT_MATCH "MODULE_SCRIPTS_COMPLETE"
    OUTPUT_VARIABLE SCRIPT_RUN_OUTPUT)
string(REGEX MATCHALL "MODULE_DEFAULT_INITIALIZED" SCRIPT_INITIALIZATIONS "${SCRIPT_RUN_OUTPUT}")
list(LENGTH SCRIPT_INITIALIZATIONS SCRIPT_INITIALIZATION_COUNT)
if(NOT SCRIPT_INITIALIZATION_COUNT EQUAL 1 OR SCRIPT_RUN_OUTPUT MATCHES "MODULE_FILE_BODY_EXECUTED")
    message(FATAL_ERROR "Relocated application did not preserve normal initialization: ${SCRIPT_RUN_OUTPUT}")
endif()
set(PACKAGED_FIXTURE "${SCRIPT_RELOCATED}/modules/fixture")
file(RENAME "${PACKAGED_FIXTURE}/module.toml" "${PACKAGED_FIXTURE}/module.toml.missing")
nomadRunTestCommand("Reject missing packaged manifest without SDK fallback" EXPECT_FAILURE
    COMMAND "${SCRIPT_RELOCATED}/${SCRIPT_EXECUTABLE}" --project-path "${SCRIPT_RELOCATED}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" ERROR_VARIABLE SCRIPT_MISSING_MANIFEST)
if(NOT SCRIPT_MISSING_MANIFEST MATCHES "module.toml")
    message(FATAL_ERROR "Missing packaged manifest did not report its path: ${SCRIPT_MISSING_MANIFEST}")
endif()
file(RENAME "${PACKAGED_FIXTURE}/module.toml.missing" "${PACKAGED_FIXTURE}/module.toml")
file(RENAME "${PACKAGED_FIXTURE}/${FIXTURE_LIBRARY_NAME}" "${SCRIPT_RELOCATED}/${FIXTURE_LIBRARY_NAME}")
nomadRunTestCommand("Reject flat module library fallback" EXPECT_FAILURE
    COMMAND "${SCRIPT_RELOCATED}/${SCRIPT_EXECUTABLE}" --project-path "${SCRIPT_RELOCATED}"
    WORKING_DIRECTORY "${NOMAD_TEST_ROOT}" ERROR_VARIABLE SCRIPT_MISSING_LIBRARY)
if(NOT SCRIPT_MISSING_LIBRARY MATCHES "Failed to load module 'fixture'")
    message(FATAL_ERROR "Missing nested module library was not diagnosed: ${SCRIPT_MISSING_LIBRARY}")
endif()
file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
