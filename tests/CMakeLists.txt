add_executable(
    nomad_test
    ./ArrayObjectTest.cpp
    ./AudioDiscoveryTest.cpp
    ./AudioTest.cpp
    ./CallableCatalogTest.cpp
    ./ColorTest.cpp
    ./CompilerContextTest.cpp
    ./CompilerHooksTest.cpp
    ./CompilerTest.cpp
    ./CompilerToolsTest.cpp
    ./DebugDrawTest.cpp
    ./DeclareIdTest.cpp
    ./EntitySpriteAnchorTest.cpp
    ./EntityTextTest.cpp
    ./EventRegistrationTest.cpp
    ./ExpressionEvaluationTest.cpp
    ./ExpressionResolveTest.cpp
    ./FileSinkTest.cpp
    ./FormatStringCompilerTest.cpp
    ./GameConfigurationTest.cpp
    ./GameEventQueueTest.cpp
    ./GameStartupTest.cpp
    ./GeometryTest.cpp
    ./InterpreterTest.cpp
    ./LanguageCallbackEventTest.cpp
    ./LanguageClassTest.cpp
    ./LanguageConstantTest.cpp
    ./LanguageContextVariableTest.cpp
    ./LanguageControlFlowTest.cpp
    ./LanguageFormatStringFunctionTest.cpp
    ./LanguageFunctionTest.cpp
    ./LanguageHubTest.cpp
    ./LanguageLexicalTest.cpp
    ./LanguageOperatorTest.cpp
    ./LanguageRegistryIdTest.cpp
    ./LanguageScriptDiscoveryTest.cpp
    ./LanguageServerTest.cpp
    ./LanguageStringLifetimeTest.cpp
    ./LanguageTestFixture.cpp
    ./LanguageTypeTest.cpp
    ./LanguageVariableIdTest.cpp
    ./LogConfigurationTest.cpp
    ./LogModuleTest.cpp
    ./LogSinkObjectTest.cpp
    ./LogTest.cpp
    ./ModuleGameTest.cpp
    ./ModuleHostTest.cpp
    ./ModuleManifestTest.cpp
    ./NativeMemberConflictTest.cpp
    ./NativeMemberExecutionTest.cpp
    ./NativeMemberSyntaxTest.cpp
    ./NativeMemberTest.cpp
    ./NativeStringMemberTest.cpp
    ./NomadConfigurationTest.cpp
    ./NomadObjectTest.cpp
    ./main.cpp
    ./NomadVersionTest.cpp
    ./ObjectConstructionTest.cpp
    ./OsModuleTest.cpp
    ./PackedSpriteRegistrationTest.cpp
    ./ParserTest.cpp
    ./ProgramOptionsTest.cpp
    ./ProjectConfigurationTest.cpp
    ./ProjectInitializerTest.cpp
    ./ProjectPackagerTest.cpp
    ./ReadOnlyCollectionTest.cpp
    ./RectanglePackerTest.cpp
    ./RuntimeTest.cpp
    ./ScriptMethodTest.cpp
    ./ScriptObjectTest.cpp
    ./ScriptObjectTypeTest.cpp
    ./SourceLoaderTest.cpp
    ./SpriteCatalogTest.cpp
    ./SpriteDiscoveryTest.cpp
    ./SpritePackerTest.cpp
    ./StatementExecutionTest.cpp
    ./SystemTest.cpp
    ./TestDirectory.hpp
    ./TestDirectoryTest.cpp
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
    nomad-collections
    nomad-game
    nomad-system
    Nomad::CliOptions
    archive
    Boost::nowide
    Boost::unit_test_framework
)

if(NOMAD_ENABLE_SANITIZERS)
    target_link_libraries(nomad_test PRIVATE nomad_sanitizers)
endif()

nomad_copy_runtime_dependencies(nomad_test)

# Filters select top-level suites. The remaining test catches all other suites and root-level cases,
# including newly added tests, so splitting the runner cannot silently omit coverage.
set(NOMAD_TEST_AUDIO_FILTER "audio")
set(NOMAD_TEST_LANGUAGE_FILTER "language_*,native_*,array_objects,object_construction,callable_*,function_resolution")
set(NOMAD_TEST_TOOLING_FILTER "compilation,project_*,sprite_*,audio_discovery,module_host,module_manifest,source_overlays")
set(NOMAD_TEST_REMAINING_FILTER
    "!${NOMAD_TEST_AUDIO_FILTER}:!${NOMAD_TEST_LANGUAGE_FILTER}:!${NOMAD_TEST_TOOLING_FILTER}")

foreach(GROUP IN ITEMS audio language tooling remaining)
    string(TOUPPER "${GROUP}" GROUP_UPPER)
    add_test(NAME nomad_test_${GROUP}
        COMMAND nomad_test "--run_test=${NOMAD_TEST_${GROUP_UPPER}_FILTER}" --log_level=test_suite --color_output=no)
    set_tests_properties(nomad_test_${GROUP} PROPERTIES LABELS unit)
endforeach()

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
