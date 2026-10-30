foreach(REQUIRED IN ITEMS NOMAD_MODULE_SCRIPT_STAGER NOMAD_TEST_ROOT)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/TestProcess.cmake")

set(SOURCE "${NOMAD_TEST_ROOT}/source module")
set(DESTINATION "${NOMAD_TEST_ROOT}/staged module")
file(MAKE_DIRECTORY "${SOURCE}/scripts/nested" "${DESTINATION}/scripts")
file(WRITE "${SOURCE}/scripts/nested/answer.nomad" "return 42\n")
file(WRITE "${DESTINATION}/scripts/stale.nomad" "return missing\n")
file(WRITE "${DESTINATION}/module.toml" "schema = 1\n[module]\nname = 'example'\n")
file(WRITE "${DESTINATION}/native-marker" "unchanged")

function(stageScripts DESCRIPTION)
    nomadRunTestCommand("${DESCRIPTION}"
        COMMAND "${CMAKE_COMMAND}"
            "-DNOMAD_MODULE_SOURCE=${SOURCE}"
            "-DNOMAD_MODULE_DESTINATION=${DESTINATION}"
            -P "${NOMAD_MODULE_SCRIPT_STAGER}")
endfunction()

stageScripts("Stage nested module scripts")
file(READ "${DESTINATION}/scripts/nested/answer.nomad" CONTENT)
if(NOT CONTENT STREQUAL "return 42\n" OR EXISTS "${DESTINATION}/scripts/stale.nomad")
    message(FATAL_ERROR "Staged module scripts did not match their source")
endif()

nomadRunTestCommand("Staging in place preserves source scripts"
    COMMAND "${CMAKE_COMMAND}"
        "-DNOMAD_MODULE_SOURCE=${SOURCE}" "-DNOMAD_MODULE_DESTINATION=${SOURCE}"
        -P "${NOMAD_MODULE_SCRIPT_STAGER}")
if(NOT EXISTS "${SOURCE}/scripts/nested/answer.nomad")
    message(FATAL_ERROR "Staging in place removed source scripts")
endif()

file(WRITE "${SOURCE}/scripts/nested/answer.nomad" "return 43\n")
stageScripts("Update changed module scripts")
file(READ "${DESTINATION}/scripts/nested/answer.nomad" CONTENT)
if(NOT CONTENT STREQUAL "return 43\n")
    message(FATAL_ERROR "Staged module script did not update")
endif()

file(REMOVE "${SOURCE}/scripts/nested/answer.nomad")
file(REMOVE_RECURSE "${SOURCE}/scripts/nested")
stageScripts("Remove deleted module scripts")
if(EXISTS "${DESTINATION}/scripts/nested/answer.nomad")
    message(FATAL_ERROR "Deleted source remained in the staged module")
endif()
if(NOT IS_DIRECTORY "${DESTINATION}/scripts")
    message(FATAL_ERROR "Empty module scripts directory was lost")
endif()

file(REMOVE_RECURSE "${SOURCE}/scripts")
stageScripts("Missing module scripts remove stale staging")
if(EXISTS "${DESTINATION}/scripts")
    message(FATAL_ERROR "Missing scripts directory left stale staging")
endif()

file(WRITE "${SOURCE}/scripts" "not a directory")
nomadRunTestCommand("Reject a non-directory scripts path" EXPECT_FAILURE
    COMMAND "${CMAKE_COMMAND}"
        "-DNOMAD_MODULE_SOURCE=${SOURCE}" "-DNOMAD_MODULE_DESTINATION=${DESTINATION}"
        -P "${NOMAD_MODULE_SCRIPT_STAGER}")
nomadRunTestCommand("Reject a non-directory scripts path when staging in place" EXPECT_FAILURE
    COMMAND "${CMAKE_COMMAND}"
        "-DNOMAD_MODULE_SOURCE=${SOURCE}" "-DNOMAD_MODULE_DESTINATION=${SOURCE}"
        -P "${NOMAD_MODULE_SCRIPT_STAGER}")
file(REMOVE "${SOURCE}/scripts")
file(READ "${DESTINATION}/native-marker" CONTENT)
if(NOT CONTENT STREQUAL "unchanged" OR NOT EXISTS "${DESTINATION}/module.toml")
    message(FATAL_ERROR "Script staging modified unrelated module files")
endif()
