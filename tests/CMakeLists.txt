add_executable(
    nomad_test
    ./main.cpp
    ./ColorTest.cpp
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
            "-DNOMAD_RUNTIME_DEPENDENCIES=$<JOIN:$<TARGET_RUNTIME_DLLS:nomad_test>,|>"
            "-DNOMAD_RUNTIME_DESTINATION=$<TARGET_FILE_DIR:nomad_test>"
            -P "${CMAKE_SOURCE_DIR}/cmake/BundleRuntimeDependencies.cmake"
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
