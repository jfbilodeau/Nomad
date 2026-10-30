foreach(REQUIRED IN ITEMS NOMADC NOMAD_VERSION NOMAD_TEST_ROOT)
    if(NOT DEFINED ${REQUIRED})
        message(FATAL_ERROR "${REQUIRED} is required")
    endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/TestProcess.cmake")

set(PROJECT "${NOMAD_TEST_ROOT}/project with spaces")
file(MAKE_DIRECTORY "${PROJECT}/res/scripts")
file(WRITE "${PROJECT}/nomad.toml"
    "schema = 1\n[project]\nname = \"Compiler export test\"\nidentifier = \"org.nomad.exports\"\n"
    "version = \"0.1.0\"\nexecutable = \"compiler-exports\"\n"
    "[nomad]\nversion = \"${NOMAD_VERSION}\"\nmodules = [\"system\"]\n"
    "[scripts]\ndirectories = [\"res/scripts\"]\n[resources]\ndirectory = \"res\"\n")
set(SCRIPT "${PROJECT}/res/scripts/main.nomad")
file(WRITE "${SCRIPT}"
    "fun exportedAnswer\nreturn 42\nend\nfun otherAnswer\nreturn 1\nend\nassert false\nreturn exportedAnswer\n")

nomadRunTestCommand("Dump without executing source"
    COMMAND "${NOMADC}" dump "${PROJECT}" OUTPUT_MATCH "function exportedAnswer")
nomadRunTestCommand("Dump selected function"
    COMMAND "${NOMADC}" dump "${PROJECT}" --function exportedAnswer
    OUTPUT_VARIABLE SELECTED OUTPUT_MATCH "function exportedAnswer")
if(SELECTED MATCHES "function otherAnswer")
    message(FATAL_ERROR "Selected dump included another function")
endif()

nomadRunTestCommand("Reject unknown dump function" EXPECT_FAILURE
    COMMAND "${NOMADC}" dump "${PROJECT}" --function missing
    OUTPUT_VARIABLE UNKNOWN_OUTPUT ERROR_VARIABLE UNKNOWN_ERROR)
if(NOT UNKNOWN_OUTPUT STREQUAL "" OR NOT UNKNOWN_ERROR MATCHES "error: Unknown function 'missing'")
    message(FATAL_ERROR "Unknown function diagnostic changed:\n${UNKNOWN_OUTPUT}\n${UNKNOWN_ERROR}")
endif()

set(DOCUMENTATION "${NOMAD_TEST_ROOT}/reference.md")
nomadRunTestCommand("Generate documentation without executing source"
    COMMAND "${NOMADC}" docs "${PROJECT}" --output "${DOCUMENTATION}")
file(READ "${DOCUMENTATION}" CONTENTS)
if(NOT CONTENTS MATCHES "exportedAnswer" OR NOT CONTENTS MATCHES "## Functions")
    message(FATAL_ERROR "Documentation did not contain the compiled API")
endif()

file(WRITE "${SCRIPT}" "return missingExportValue\n")
nomadRunTestCommand("Reject dumping invalid source" EXPECT_FAILURE
    COMMAND "${NOMADC}" dump "${PROJECT}" OUTPUT_VARIABLE INVALID_OUTPUT ERROR_VARIABLE INVALID_ERROR)
if(NOT INVALID_OUTPUT STREQUAL "" OR NOT INVALID_ERROR MATCHES "missingExportValue")
    message(FATAL_ERROR "Invalid source produced partial instructions or lost diagnostics")
endif()

file(WRITE "${DOCUMENTATION}" "unchanged")
nomadRunTestCommand("Reject documenting invalid source" EXPECT_FAILURE
    COMMAND "${NOMADC}" docs "${PROJECT}" --output "${DOCUMENTATION}"
    ERROR_VARIABLE DOCUMENTATION_ERROR)
file(READ "${DOCUMENTATION}" CONTENTS)
if(NOT CONTENTS STREQUAL "unchanged" OR NOT DOCUMENTATION_ERROR MATCHES "missingExportValue")
    message(FATAL_ERROR "Failed compilation overwrote documentation or lost diagnostics")
endif()
