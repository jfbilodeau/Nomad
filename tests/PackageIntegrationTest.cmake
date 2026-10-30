if(
    NOT DEFINED NOMAD_CLI OR
    NOT DEFINED NOMAD_COMPILER OR
    NOT DEFINED NOMAD_RUNTIME_SUFFIX OR
    NOT DEFINED NOMAD_TEST_ROOT
)
    message(FATAL_ERROR "NOMAD_CLI, NOMAD_COMPILER, NOMAD_RUNTIME_SUFFIX, and NOMAD_TEST_ROOT are required")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/TestProcess.cmake")

function(run_success DESCRIPTION)
    nomadRunTestCommand("${DESCRIPTION}" COMMAND ${ARGN})
endfunction()

function(run_failure DESCRIPTION)
    nomadRunTestCommand("${DESCRIPTION}" EXPECT_FAILURE COMMAND ${ARGN})
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
run_success("Compiler TextMate help without required output" "${NOMAD_COMPILER}" textmate -h)
run_failure("Compiler TextMate requires output" "${NOMAD_COMPILER}" textmate)
run_failure("Compiler dump rejects docs options" "${NOMAD_COMPILER}" dump --output ignored.md)

set(REFERENCE_DIRECTORY "${NOMAD_TEST_ROOT}/reference")
file(MAKE_DIRECTORY "${REFERENCE_DIRECTORY}")
nomadRunTestCommand(
    "Engine-only documentation outside a project"
    COMMAND "${NOMAD_COMPILER}" docs --output .
    WORKING_DIRECTORY "${REFERENCE_DIRECTORY}"
    ERROR_VARIABLE DOCUMENTATION_WARNING
)
if(NOT DOCUMENTATION_WARNING MATCHES "warning: No Nomad project found.*generating engine-only documentation")
    message(FATAL_ERROR "Engine-only documentation did not report the expected warning")
endif()
set(REFERENCE_DOCUMENT "${REFERENCE_DIRECTORY}/nomad-reference.md")
require_path("${REFERENCE_DOCUMENT}" "Generated engine-only documentation")
file(READ "${REFERENCE_DOCUMENT}" REFERENCE_CONTENT)
if(NOT REFERENCE_CONTENT MATCHES "# Nomad Engine Reference")
    message(FATAL_ERROR "Generated engine-only documentation is invalid")
endif()

run_success(
    "Project initialization"
    "${NOMAD_CLI}" init "${PROJECT_DIR}"
)

file(MAKE_DIRECTORY "${PROJECT_DIR}/res/sprites/player")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/projects/package-smoke/res/sprites/checker.png"
    "${PROJECT_DIR}/res/sprites/player/checker.png"
    COPYONLY
)
nomadRunTestCommand(
    "Project sprite packing report"
    COMMAND "${NOMAD_CLI}" check "${PROJECT_DIR}"
    OUTPUT_MATCH "Sprites: 1 logical sprites, 1 groups, 1 texture pages[\r\n]+  sprites.player#0:"
)
file(WRITE "${PROJECT_DIR}/res/sprites/player/broken.png" "not a PNG")
nomadRunTestCommand(
    "Invalid project sprite"
    EXPECT_FAILURE
    COMMAND "${NOMAD_CLI}" check "${PROJECT_DIR}"
    ERROR_VARIABLE INVALID_SPRITE_ERROR
)
if(NOT INVALID_SPRITE_ERROR MATCHES "error: Failed to check sprites: Failed to load image for sprite")
    message(FATAL_ERROR "Invalid project sprite did not report the expected error")
endif()
file(REMOVE "${PROJECT_DIR}/res/sprites/player/broken.png")

set(TEXTMATE_GRAMMAR "${PROJECT_DIR}/nomad.tmLanguage.json")
run_success(
    "TextMate grammar generation"
    "${NOMAD_COMPILER}" textmate "${PROJECT_DIR}" --output "${TEXTMATE_GRAMMAR}"
)
require_path("${TEXTMATE_GRAMMAR}" "Generated TextMate grammar")
file(READ "${TEXTMATE_GRAMMAR}" TEXTMATE_GRAMMAR_JSON)
string(JSON TEXTMATE_SCOPE_NAME GET "${TEXTMATE_GRAMMAR_JSON}" scopeName)
if(NOT TEXTMATE_SCOPE_NAME STREQUAL "source.nomad")
    message(FATAL_ERROR "Generated TextMate grammar has an invalid scope name")
endif()
string(JSON TEXTMATE_REPOSITORY_TYPE TYPE "${TEXTMATE_GRAMMAR_JSON}" repository)
if(NOT TEXTMATE_REPOSITORY_TYPE STREQUAL "OBJECT")
    message(FATAL_ERROR "Generated TextMate grammar does not contain a repository")
endif()

file(MAKE_DIRECTORY "${PROJECT_DIR}/res/development")
file(WRITE "${PROJECT_DIR}/res/development/notes.txt" "excluded")
file(WRITE "${PROJECT_DIR}/res/sprite.aseprite" "excluded")
file(WRITE "${PROJECT_DIR}/res/keep.txt" "included")
file(WRITE "${PROJECT_DIR}/res/日本語.txt" "Unicode resource")
file(READ "${PROJECT_DIR}/nomad.toml" PROJECT_MANIFEST)
string(REPLACE "exclude = []" "exclude = [\"**/*.aseprite\", \"development/**\"]"
    PROJECT_MANIFEST "${PROJECT_MANIFEST}")
file(WRITE "${PROJECT_DIR}/nomad.toml" "${PROJECT_MANIFEST}")

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
require_path("${PACKAGE_DIR}/res/scripts/main.nomad" "Packaged entry script")
require_path("${PACKAGE_DIR}/res/keep.txt" "Included resource")
require_path("${PACKAGE_DIR}/res/日本語.txt" "Unicode resource")
require_path("${PACKAGE_DIR}/res/sprites/catalog.json" "Compiled sprite catalog")
require_path("${PACKAGE_DIR}/res/sprites/sprites.player-0.png" "Compiled sprite texture page")
reject_path("${PACKAGE_DIR}/res/sprites/player/checker.png" "Source sprite PNG")
file(READ "${PACKAGE_DIR}/res/sprites/catalog.json" SPRITE_CATALOG)
string(JSON SPRITE_CATALOG_SCHEMA GET "${SPRITE_CATALOG}" schema)
if(NOT SPRITE_CATALOG_SCHEMA EQUAL 1)
    message(FATAL_ERROR "Compiled sprite catalog has an invalid schema")
endif()
string(JSON SPRITE_CATALOG_GROUP_COUNT LENGTH "${SPRITE_CATALOG}" groups)
if(NOT SPRITE_CATALOG_GROUP_COUNT EQUAL 1)
    message(FATAL_ERROR "Compiled sprite catalog should contain exactly one group")
endif()
file(READ "${PACKAGE_DIR}/res/日本語.txt" UNICODE_RESOURCE)
if(NOT UNICODE_RESOURCE STREQUAL "Unicode resource")
    message(FATAL_ERROR "Unicode resource contents changed during packaging")
endif()
require_path("${PACKAGED_RUNTIME}" "Packaged runtime")
require_path("${PACKAGE_DIR}/licenses/Nomad.txt" "Nomad license")
require_path("${PACKAGE_DIR}/licenses/SDL.txt" "SDL license")
require_path("${PACKAGE_DIR}/licenses/FreeType.txt" "FreeType license")
foreach(LICENSE IN ITEMS
    Boost Box2D DearImGui tomlplusplus libarchive zlib SDL_image SDL_ttf
    HarfBuzz plutosvg plutovg stb_image NanoSVG NanoSVG_rasterizer QOI tiny_jpeg miniz
    ThirdPartyNotices
)
    set(LICENSE_PATH "${PACKAGE_DIR}/licenses/${LICENSE}.txt")
    require_path("${LICENSE_PATH}" "${LICENSE} license")
    file(SIZE "${LICENSE_PATH}" LICENSE_SIZE)
    if(LICENSE_SIZE EQUAL 0)
        message(FATAL_ERROR "Empty ${LICENSE} license")
    endif()
endforeach()
reject_path("${PACKAGE_DIR}/runtime.json" "Internal runtime bundle manifest")
reject_path("${PACKAGE_DIR}/res/development/notes.txt" "Excluded development resource")
reject_path("${PACKAGE_DIR}/res/sprite.aseprite" "Excluded Aseprite resource")

file(READ "${PACKAGE_DIR}/nomad.toml" RELEASE_MANIFEST)

foreach(DEVELOPMENT_FIELD IN ITEMS "executable" "[package]")
    string(FIND "${RELEASE_MANIFEST}" "${DEVELOPMENT_FIELD}" FIELD_POSITION)

    if(NOT FIELD_POSITION EQUAL -1)
        message(FATAL_ERROR "Release manifest contains development field: ${DEVELOPMENT_FIELD}")
    endif()
endforeach()

if(NOT RELEASE_MANIFEST MATCHES "\\[resources\\][ \t\r\n]+directory = ['\"]res['\"]")
    message(FATAL_ERROR "Release game configuration must point resources.directory to res")
endif()

run_success(
    "Release package validation"
    "${NOMAD_CLI}" check "${PACKAGE_DIR}"
)
set(ISOLATED_TOOLS "${NOMAD_TEST_ROOT}/isolated-tools")
set(NOMAD_RUNTIME_DESTINATION "${ISOLATED_TOOLS}")
foreach(PROGRAM IN ITEMS CLI COMPILER)
    set(NOMAD_RUNTIME "${NOMAD_${PROGRAM}}")
    unset(NOMAD_RUNTIME_DEPENDENCIES)
    if(WIN32)
        set(NOMAD_RUNTIME_DEPENDENCIES "${NOMAD_${PROGRAM}_RUNTIME_DEPENDENCIES}")
    endif()
    include("${CMAKE_CURRENT_LIST_DIR}/../cmake/BundleRuntimeDependencies.cmake")
endforeach()
get_filename_component(CLI_FILENAME "${NOMAD_CLI}" NAME)
run_success(
    "Isolated tooling release package validation"
    "${ISOLATED_TOOLS}/${CLI_FILENAME}" check "${PACKAGE_DIR}"
)
run_success(
    "Isolated packaged runtime startup"
    "${CMAKE_COMMAND}"
    "-DNOMAD_PACKAGE_ARCHIVE=${PACKAGE_ARCHIVE}"
    "-DNOMAD_PACKAGE_DIRECTORY=${NOMAD_TEST_ROOT}/isolated"
    -P "${CMAKE_CURRENT_LIST_DIR}/PackageSmokeTest.cmake"
)

run_failure(
    "Package overwrite protection"
    "${NOMAD_CLI}" package "${PROJECT_DIR}"
)

file(SHA256 "${PACKAGE_ARCHIVE}" FIRST_CHECKSUM)
file(TIMESTAMP "${PROJECT_DIR}/res/keep.txt" ORIGINAL_TIMESTAMP "%s")
run_success("Wait for distinct source timestamp" "${CMAKE_COMMAND}" -E sleep 2)
file(GLOB_RECURSE SOURCE_FILES LIST_DIRECTORIES FALSE "${PROJECT_DIR}/res/*")
file(TOUCH ${SOURCE_FILES} "${PROJECT_DIR}/nomad.toml")
file(TIMESTAMP "${PROJECT_DIR}/res/keep.txt" CHANGED_TIMESTAMP "%s")
if(ORIGINAL_TIMESTAMP STREQUAL CHANGED_TIMESTAMP)
    message(FATAL_ERROR "Source timestamp did not change before reproducibility check")
endif()
run_success(
    "Forced package replacement"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --force
)
file(SHA256 "${PACKAGE_ARCHIVE}" REPEATED_CHECKSUM)
if(NOT FIRST_CHECKSUM STREQUAL REPEATED_CHECKSUM)
    message(FATAL_ERROR "Changing source timestamps produced different archive bytes")
endif()

if(DEFINED NOMAD_PACKAGE_ARTIFACT_DIR)
    file(MAKE_DIRECTORY "${NOMAD_PACKAGE_ARTIFACT_DIR}")
    get_filename_component(ARCHIVE_NAME "${PACKAGE_ARCHIVE}" NAME)
    file(COPY_FILE "${PACKAGE_ARCHIVE}" "${NOMAD_PACKAGE_ARTIFACT_DIR}/${ARCHIVE_NAME}")
    file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/PackageSmokeTest.cmake"
        "${NOMAD_PACKAGE_ARTIFACT_DIR}/PackageSmokeTest.cmake")
    file(COPY_FILE "${CMAKE_CURRENT_LIST_DIR}/TestProcess.cmake"
        "${NOMAD_PACKAGE_ARTIFACT_DIR}/TestProcess.cmake")
endif()

file(WRITE "${PROJECT_DIR}/res/scripts/main.nomad" "missing.statement\n")
run_failure(
    "Package compilation gate"
    "${NOMAD_CLI}" package "${PROJECT_DIR}" --force
)
file(SHA256 "${PACKAGE_ARCHIVE}" PRESERVED_CHECKSUM)
if(NOT FIRST_CHECKSUM STREQUAL PRESERVED_CHECKSUM)
    message(FATAL_ERROR "Failed compilation changed the previous archive")
endif()

file(REMOVE_RECURSE "${NOMAD_TEST_ROOT}")
