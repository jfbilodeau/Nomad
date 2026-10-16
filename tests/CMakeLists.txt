add_executable(
    nomad_test
    ./AudioDiscoveryTest.cpp
    ./AudioTest.cpp
    ./CallableCatalogTest.cpp
    ./ColorTest.cpp
    ./CompilerContextTest.cpp
    ./CompilerHooksTest.cpp
    ./CompilerTest.cpp
    ./CompilerToolsTest.cpp
    ./DebugDrawTest.cpp
    ./EntitySpriteAnchorTest.cpp
    ./EntityTextTest.cpp
    ./EventRegistrationTest.cpp
    ./ExpressionEvaluationTest.cpp
    ./ExpressionResolveTest.cpp
    ./FormatStringCompilerTest.cpp
    ./GameEventQueueTest.cpp
    ./GameStartupTest.cpp
    ./GeometryTest.cpp
    ./InterpreterTest.cpp
    ./IntrinsicMemberConflictTest.cpp
    ./IntrinsicMemberExecutionTest.cpp
    ./IntrinsicMemberSyntaxTest.cpp
    ./IntrinsicMemberTest.cpp
    ./IntrinsicStringMemberTest.cpp
    ./LanguageCallbackEventTest.cpp
    ./LanguageControlFlowTest.cpp
    ./LanguageFormatStringFunctionTest.cpp
    ./LanguageFunctionTest.cpp
    ./LanguageLexicalTest.cpp
    ./LanguageOperatorTest.cpp
    ./LanguageScriptDiscoveryTest.cpp
    ./LanguageServerTest.cpp
    ./LanguageStringLifetimeTest.cpp
    ./LanguageTestFixture.cpp
    ./LanguageTypeTest.cpp
    ./LogTest.cpp
    ./ModuleGameTest.cpp
    ./ModuleHostTest.cpp
    ./ModuleManifestTest.cpp
    ./NomadConfigurationTest.cpp
    ./main.cpp
    ./NomadVersionTest.cpp
    ./PackedSpriteRegistrationTest.cpp
    ./ParserTest.cpp
    ./ProgramOptionsTest.cpp
    ./ProjectConfigurationTest.cpp
    ./ProjectInitializerTest.cpp
    ./ProjectPackagerTest.cpp
    ./RectanglePackerTest.cpp
    ./RuntimeTest.cpp
    ./SourceLoaderTest.cpp
    ./SpriteCatalogTest.cpp
    ./SpriteDiscoveryTest.cpp
    ./SpritePackerTest.cpp
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
    NOMAD_TEST_MODULE_DIRECTORY="$<TARGET_FILE_DIR:nomad-module-fixture>"
    NOMAD_TEST_CORE_DIRECTORY="$<TARGET_FILE_DIR:nomad-language>"
)

add_library(nomad-module-fixture SHARED ModuleFixture.cpp)
set_target_properties(nomad-module-fixture PROPERTIES OUTPUT_NAME fixture)
if(WIN32)
    set_target_properties(nomad-module-fixture PROPERTIES PREFIX "")
endif()
target_link_libraries(nomad-module-fixture PRIVATE Nomad::Language)
add_dependencies(nomad_test nomad-module-fixture)

target_link_libraries(
    nomad_test
    PRIVATE
    nomad
    nomad-game
    nomad-system
    Nomad::CliOptions
    archive
    Boost::unit_test_framework
)

if(NOMAD_ENABLE_SANITIZERS)
    target_link_libraries(nomad_test PRIVATE nomad_sanitizers)
endif()

nomad_copy_runtime_dependencies(nomad_test)

add_test(NAME nomad_test COMMAND nomad_test)

if(TARGET nomad-lsp)
    add_test(NAME nomad_lsp_process
        COMMAND "${CMAKE_COMMAND}"
            "-DNOMAD_LSP=$<TARGET_FILE:nomad-lsp>"
            "-DNOMAD_VERSION=${PROJECT_VERSION}"
            "-DNOMAD_TEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/lsp-process/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/LanguageServerProcessTest.cmake")
    set_tests_properties(nomad_lsp_process PROPERTIES TIMEOUT 60)
endif()

include(IntegrationTests.cmake)
