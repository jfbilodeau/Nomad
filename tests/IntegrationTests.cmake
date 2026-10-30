if(
    NOT TARGET nomad-cli OR
    NOT TARGET nomadc OR
    NOT TARGET nomad-runtime OR
    NOMAD_ENABLE_SANITIZERS
)
    return()
endif()

add_test(
    NAME nomad_module_runtime_integration
    COMMAND "${CMAKE_COMMAND}"
        "-DNOMAD_CLI=$<TARGET_FILE:nomad-cli>"
        "-DNOMAD_SCRIPT_SDK_RUNTIME_DEPENDENCIES=$<JOIN:$<TARGET_RUNTIME_DLLS:nomad-cli>;$<TARGET_RUNTIME_DLLS:nomadc>;$<TARGET_RUNTIME_DLLS:nomad-runtime>,|>"
        "-DNOMAD_RUNTIME_SUFFIX=$<TARGET_FILE_SUFFIX:nomad-runtime>"
        "-DNOMAD_SYSTEM_LIBRARY=$<TARGET_FILE_NAME:nomad-system>"
        "-DNOMAD_COLLECTIONS_LIBRARY=$<TARGET_FILE_NAME:nomad-collections>"
        "-DNOMAD_GAME_LIBRARY=$<TARGET_FILE_NAME:nomad-game>"
        "-DNOMAD_OS_LIBRARY=$<TARGET_FILE_NAME:nomad-os>"
        "-DNOMAD_LOG_LIBRARY=$<TARGET_FILE_NAME:nomad-log>"
        "-DNOMAD_FIXTURE_LIBRARY=$<TARGET_FILE:nomad-module-fixture>"
        "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/module-runtime/$<CONFIG>"
        -P "${CMAKE_CURRENT_LIST_DIR}/ModuleRuntimeIntegrationTest.cmake"
)
set_tests_properties(nomad_module_runtime_integration PROPERTIES LABELS integration TIMEOUT 120)

add_test(
    NAME nomad_distribution_integration
    COMMAND
        "${CMAKE_COMMAND}"
        "-DNOMAD_DISTRIBUTION_CONFIG=${NOMAD_DISTRIBUTION_CONFIG}"
        "-DNOMAD_DISTRIBUTION_OUTPUT=${NOMAD_DISTRIBUTION_OUTPUT}"
        "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/distribution-smoke/$<CONFIG>"
        -P "${CMAKE_CURRENT_LIST_DIR}/DistributionIntegrationTest.cmake"
)
set_tests_properties(
    nomad_distribution_integration
    PROPERTIES
    LABELS integration
    RUN_SERIAL TRUE
    TIMEOUT 120
)

set(NOMAD_GRAPHICAL_SMOKE_PROJECT "${CMAKE_CURRENT_BINARY_DIR}/package-smoke-project")
file(REMOVE_RECURSE "${NOMAD_GRAPHICAL_SMOKE_PROJECT}/res")
file(
    GLOB_RECURSE NOMAD_GRAPHICAL_SMOKE_RESOURCES
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_LIST_DIR}/projects/package-smoke"
    "${CMAKE_CURRENT_LIST_DIR}/projects/package-smoke/res/*"
)
foreach(RESOURCE IN LISTS NOMAD_GRAPHICAL_SMOKE_RESOURCES)
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/projects/package-smoke/${RESOURCE}"
        "${NOMAD_GRAPHICAL_SMOKE_PROJECT}/${RESOURCE}"
        COPYONLY
    )
endforeach()
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/projects/package-smoke/nomad.toml.in"
    "${NOMAD_GRAPHICAL_SMOKE_PROJECT}/nomad.toml"
    @ONLY
)

add_test(
    NAME nomad_package_graphical_smoke
    COMMAND
        "${CMAKE_COMMAND}"
        "-DNOMAD_CLI=$<TARGET_FILE:nomad-cli>"
        "-DNOMAD_SMOKE_PROJECT=${NOMAD_GRAPHICAL_SMOKE_PROJECT}"
        "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-graphical-smoke/$<CONFIG>"
        "-DNOMAD_RUNTIME_SUFFIX=$<TARGET_FILE_SUFFIX:nomad-runtime>"
        "-DNOMAD_PACKAGE_ARTIFACT_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-graphical-artifact/$<CONFIG>"
        -P "${CMAKE_CURRENT_LIST_DIR}/PackageGraphicalSmokeTest.cmake"
)
set_tests_properties(
    nomad_package_graphical_smoke
    PROPERTIES
    LABELS integration
    RUN_SERIAL TRUE
    TIMEOUT 60
)

add_test(
    NAME nomad_package_integration
    COMMAND
        "${CMAKE_COMMAND}"
        "-DNOMAD_CLI=$<TARGET_FILE:nomad-cli>"
        "-DNOMAD_COMPILER=$<TARGET_FILE:nomadc>"
        "-DNOMAD_CLI_RUNTIME_DEPENDENCIES=$<JOIN:$<TARGET_RUNTIME_DLLS:nomad-cli>,|>"
        "-DNOMAD_COMPILER_RUNTIME_DEPENDENCIES=$<JOIN:$<TARGET_RUNTIME_DLLS:nomadc>,|>"
        "-DNOMAD_RUNTIME_SUFFIX=$<TARGET_FILE_SUFFIX:nomad-runtime>"
        "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-integration/$<CONFIG>"
        "-DNOMAD_PACKAGE_ARTIFACT_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-artifact/$<CONFIG>"
        -P "${CMAKE_CURRENT_LIST_DIR}/PackageIntegrationTest.cmake"
)
set_tests_properties(
    nomad_package_integration
    PROPERTIES
    LABELS integration
    RUN_SERIAL TRUE
    TIMEOUT 60
)
