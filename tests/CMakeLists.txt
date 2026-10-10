add_executable(
    nomad_test
    ./main.cpp
    ./ColorTest.cpp
    ./EntityTextTest.cpp
    ./CallableCatalogTest.cpp
    ./CompilerToolsTest.cpp
    ./CompilerContextTest.cpp
    ./CompilerTest.cpp
    ./EventRegistrationTest.cpp
    ./ExpressionEvaluationTest.cpp
    ./ExpressionResolveTest.cpp
    ./GameEventQueueTest.cpp
    ./GameFactoryTest.cpp
    ./GeometryTest.cpp
    ./InterpreterTest.cpp
    ./LanguageCallbackEventTest.cpp
    ./LanguageControlFlowTest.cpp
    ./LanguageFormatStringFunctionTest.cpp
    ./LanguageFunctionTest.cpp
    ./LanguageLexicalTest.cpp
    ./LanguageOperatorTest.cpp
    ./LanguageScriptDiscoveryTest.cpp
    ./LanguageStringLifetimeTest.cpp
    ./LanguageTestFixture.cpp
    ./LanguageTypeTest.cpp
    ./LogTest.cpp
    ./NomadVersionTest.cpp
    ./ParserTest.cpp
    ./ProgramOptionsTest.cpp
    ./ProjectConfigurationTest.cpp
    ./ProjectInitializerTest.cpp
    ./ProjectPackagerTest.cpp
    ./RuntimeTest.cpp
    ./StatementExecutionTest.cpp
    ./SystemTest.cpp
    ./TestDirectory.hpp
    ./TileMapDataTest.cpp
    ./TileMapTest.cpp
    ./TokenizerTest.cpp
    ./VariablePersistenceTest.cpp
)

target_include_directories(nomad_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Matches the `nomad` library so debug-only checks (e.g. the live string counter) are compiled into the tests.
target_compile_definitions(
    nomad_test
    PRIVATE
    $<$<CONFIG:Debug>:NOMAD_DEBUG>
    NOMAD_TEST_PROJECT_TEMPLATE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../templates/projects/default"
    NOMAD_TEST_FONT_FILE="${CMAKE_CURRENT_SOURCE_DIR}/projects/package-smoke/res/fonts/ProggyClean.ttf"
)

target_link_libraries(
    nomad_test
    PRIVATE
    nomad
    Nomad::Project
    Nomad::CliOptions
    archive_static
    Boost::unit_test_framework
)

if(NOMAD_ENABLE_SANITIZERS)
    target_link_libraries(nomad_test PRIVATE nomad_sanitizers)
endif()

if(WIN32)
    add_custom_command(
        TARGET nomad_test
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND}
            -DNOMAD_RUNTIME_DEPENDENCIES_FROM_ARGUMENTS=ON
            "-DNOMAD_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:nomad_test>"
            -P "${CMAKE_SOURCE_DIR}/cmake/BundleRuntimeDependencies.cmake"
            --
            $<TARGET_RUNTIME_DLLS:nomad_test>
        COMMAND_EXPAND_LISTS
        VERBATIM
    )
endif()

add_test(NAME nomad_test COMMAND nomad_test)

if(
    TARGET nomad-cli AND
    TARGET nomadc AND
    TARGET nomad-runtime AND
    NOT NOMAD_ENABLE_SANITIZERS
)
    add_test(NAME nomad_distribution_integration
        COMMAND "${CMAKE_COMMAND}"
            "-DNOMAD_DISTRIBUTION_CONFIG=${NOMAD_DISTRIBUTION_CONFIG}"
            "-DNOMAD_DISTRIBUTION_OUTPUT=${NOMAD_DISTRIBUTION_OUTPUT}"
            "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/distribution-smoke/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/DistributionIntegrationTest.cmake")
    set_tests_properties(nomad_distribution_integration PROPERTIES
        LABELS integration RUN_SERIAL TRUE TIMEOUT 120)
    set(NOMAD_GRAPHICAL_SMOKE_PROJECT "${CMAKE_CURRENT_BINARY_DIR}/package-smoke-project")
    file(REMOVE_RECURSE "${NOMAD_GRAPHICAL_SMOKE_PROJECT}/res")
    file(GLOB_RECURSE NOMAD_GRAPHICAL_SMOKE_RESOURCES CONFIGURE_DEPENDS
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/projects/package-smoke"
        "${CMAKE_CURRENT_SOURCE_DIR}/projects/package-smoke/res/*")
    foreach(RESOURCE IN LISTS NOMAD_GRAPHICAL_SMOKE_RESOURCES)
        configure_file("projects/package-smoke/${RESOURCE}"
            "${NOMAD_GRAPHICAL_SMOKE_PROJECT}/${RESOURCE}" COPYONLY)
    endforeach()
    configure_file(projects/package-smoke/nomad.toml.in
        "${NOMAD_GRAPHICAL_SMOKE_PROJECT}/nomad.toml" @ONLY)
    add_test(
        NAME nomad_package_graphical_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DNOMAD_CLI=$<TARGET_FILE:nomad-cli>"
            "-DNOMAD_SMOKE_PROJECT=${NOMAD_GRAPHICAL_SMOKE_PROJECT}"
            "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-graphical-smoke/$<CONFIG>"
            "-DNOMAD_RUNTIME_SUFFIX=$<TARGET_FILE_SUFFIX:nomad-runtime>"
            "-DNOMAD_PACKAGE_ARTIFACT_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-graphical-artifact/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageGraphicalSmokeTest.cmake"
    )
    set_tests_properties(nomad_package_graphical_smoke PROPERTIES
        LABELS integration RUN_SERIAL TRUE TIMEOUT 60)
    add_test(
        NAME nomad_package_integration
        COMMAND
            ${CMAKE_COMMAND}
            "-DNOMAD_CLI=$<TARGET_FILE:nomad-cli>"
            "-DNOMAD_COMPILER=$<TARGET_FILE:nomadc>"
            "-DNOMAD_RUNTIME_SUFFIX=$<TARGET_FILE_SUFFIX:nomad-runtime>"
            "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/package-integration/$<CONFIG>"
            "-DNOMAD_PACKAGE_ARTIFACT_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-artifact/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageIntegrationTest.cmake"
    )
    set_tests_properties(
        nomad_package_integration
        PROPERTIES
        LABELS integration
        RUN_SERIAL TRUE
        TIMEOUT 60
    )
endif()
