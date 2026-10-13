cmake_minimum_required(VERSION 3.30)
set(CMAKE_POLICY_VERSION 3.30)

project(Nomad VERSION 0.1.3 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
        add_compile_options(
                $<$<COMPILE_LANG_AND_ID:C,MSVC>:/MP>
                $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/MP>
        )
endif()
add_compile_definitions($<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>)

set(NOMAD_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(
        cmake/Version.hpp.in
        "${NOMAD_GENERATED_INCLUDE_DIR}/nomad/Version.hpp"
        @ONLY
)

option(NOMAD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NOMAD_WARNINGS_AS_ERRORS "Treat Nomad compiler warnings as errors" ON)
option(NOMAD_USE_CCACHE "Use ccache when it is available" ON)

if(WIN32 AND MINGW)
        get_filename_component(NOMAD_MINGW_BINARY_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        find_file(
                NOMAD_MINGW_STDLIB_RUNTIME
                NAMES libstdc++-6.dll
                PATHS "${NOMAD_MINGW_BINARY_DIR}"
                NO_DEFAULT_PATH
                REQUIRED
        )
        find_file(
                NOMAD_MINGW_THREAD_RUNTIME
                NAMES libwinpthread-1.dll
                PATHS "${NOMAD_MINGW_BINARY_DIR}"
                NO_DEFAULT_PATH
                REQUIRED
        )
        file(GLOB NOMAD_MINGW_GCC_RUNTIMES "${NOMAD_MINGW_BINARY_DIR}/libgcc_s_*.dll")
        list(LENGTH NOMAD_MINGW_GCC_RUNTIMES NOMAD_MINGW_GCC_RUNTIME_COUNT)
        if(NOT NOMAD_MINGW_GCC_RUNTIME_COUNT EQUAL 1)
                message(FATAL_ERROR
                        "Expected one MinGW GCC runtime DLL in ${NOMAD_MINGW_BINARY_DIR}, "
                        "found: ${NOMAD_MINGW_GCC_RUNTIMES}"
                )
        endif()
        set(
                NOMAD_TOOLCHAIN_RUNTIME_DLLS
                ${NOMAD_MINGW_GCC_RUNTIMES}
                "${NOMAD_MINGW_STDLIB_RUNTIME}"
                "${NOMAD_MINGW_THREAD_RUNTIME}"
        )
endif()

if(NOMAD_USE_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
        find_program(NOMAD_CCACHE_PROGRAM ccache)
        if(NOMAD_CCACHE_PROGRAM)
                set(CMAKE_CXX_COMPILER_LAUNCHER "${NOMAD_CCACHE_PROGRAM}")
                message(STATUS "Using ccache: ${NOMAD_CCACHE_PROGRAM}")
        endif()
endif()

if(NOMAD_ENABLE_SANITIZERS)
        if(MSVC)
                message(FATAL_ERROR "NOMAD_ENABLE_SANITIZERS requires GCC or Clang")
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
                add_library(nomad_sanitizers INTERFACE)
                target_compile_options(nomad_sanitizers INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
                target_link_options(nomad_sanitizers INTERFACE -fsanitize=address,undefined)
        else()
                message(FATAL_ERROR "NOMAD_ENABLE_SANITIZERS requires GCC or Clang")
        endif()
endif()

include(cmake/Dependencies.cmake)

string(TOLOWER "${CMAKE_SYSTEM_NAME}" NOMAD_PACKAGE_SYSTEM)
if(APPLE)
    set(NOMAD_PACKAGE_SYSTEM macos)
endif()
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" NOMAD_PACKAGE_ARCHITECTURE)
if(CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR NOMAD_PACKAGE_ARCHITECTURE MATCHES "^(amd64|x86_64)$")
    set(NOMAD_PACKAGE_ARCHITECTURE x64)
elseif(NOMAD_PACKAGE_ARCHITECTURE MATCHES "^(aarch64|arm64)$")
    set(NOMAD_PACKAGE_ARCHITECTURE arm64)
endif()
set(NOMAD_PACKAGE_PLATFORM "${NOMAD_PACKAGE_SYSTEM}-${NOMAD_PACKAGE_ARCHITECTURE}")

set(NOMAD_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(NOMAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

set(
        DEAR_IMGUI_SOURCE
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp

        ${dearimgui_SOURCE_DIR}/imgui.cpp
        ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
        ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
        ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
)

set(
        NOMAD_LANGUAGE_SOURCES
        ${NOMAD_SOURCE_DIR}/nomad/Nomad.cpp

        ${NOMAD_SOURCE_DIR}/nomad/compiler/Argument.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CallNode.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Compiler.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CompilerContext.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CompilerTools.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Expression.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/FormatStringCompiler.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Operators.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Parser.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/StatementParsers.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/SyntaxTree.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp

        ${NOMAD_SOURCE_DIR}/nomad/compiler/statements/EventStatement.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/statements/ReturnStatement.cpp

        ${NOMAD_SOURCE_DIR}/nomad/log/ConsoleSink.cpp
        ${NOMAD_SOURCE_DIR}/nomad/log/Logger.cpp
        ${NOMAD_SOURCE_DIR}/nomad/log/MemorySink.cpp

        ${NOMAD_SOURCE_DIR}/nomad/script/Closure.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/DynamicVariable.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Event.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/FormatString.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Instructions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Interpreter.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/IntrinsicMembers.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunction.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/OpCode.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Runtime.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeOpCodes.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeValue.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/ScriptFunction.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Type.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Variable.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/VariableContext.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/VirtualMachine.cpp

        ${NOMAD_SOURCE_DIR}/nomad/system/Memory.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/String.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/TempHeap.cpp
)

set(
        NOMAD_PROJECT_SOURCES
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectConfiguration.cpp
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectInitializer.cpp
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp
)

set(
        NOMAD_ENGINE_SOURCES
        ${DEAR_IMGUI_SOURCE}

        ${NOMAD_SOURCE_DIR}/nomad/debug/DebugConsole.cpp
        ${NOMAD_SOURCE_DIR}/nomad/debug/DebugDraw.cpp

        ${NOMAD_SOURCE_DIR}/nomad/game/ActionManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Alignment.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Canvas.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Color.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Entity.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/EntityVariableContext.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/EventRegistration.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Game.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Game.persistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/GameEvent.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/GameEventQueue.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/GameExecutionContext.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/GameFactory.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/GameStatementParsers.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/InputManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Layer.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Scene.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Scene.loadTileMap.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Scene.render.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Scene.update.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/SceneVariableContext.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/System.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMap.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMapData.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/VariablePersistence.cpp

        ${NOMAD_SOURCE_DIR}/nomad/game/functions/GameFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/functions/InputFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/functions/OtherEntityFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/functions/SceneFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/functions/SystemFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/functions/ThisEntityFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/functions/WindowFunctions.cpp

        ${NOMAD_SOURCE_DIR}/nomad/game/variables/GameDynamicVariables.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/variables/InputDynamicVariables.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/variables/OtherDynamicVariables.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/variables/SceneDynamicVariables.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/variables/ThisDynamicVariables.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/variables/WindowDynamicVariables.cpp

        ${NOMAD_SOURCE_DIR}/nomad/geometry/Circle.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/CircleF.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Intersection.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Point.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/PointF.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Rectangle.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/RectangleF.cpp

        ${NOMAD_SOURCE_DIR}/nomad/resource/Animation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Font.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/RectanglePacker.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Resource.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/ResourceManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Sprite.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteAtlas.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteDiscovery.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpritePacker.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Text.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Texture.cpp
)

include(cmake/NomadLibrary.cmake)
include(cmake/NomadRuntime.cmake)
target_compile_definitions(nomad-project PRIVATE NOMAD_PACKAGE_PLATFORM="${NOMAD_PACKAGE_PLATFORM}")

add_library(nomad-cli-options INTERFACE)
add_library(Nomad::CliOptions ALIAS nomad-cli-options)
target_sources(nomad-cli-options INTERFACE ${NOMAD_INCLUDE_DIR}/nomad/system/ProgramOptions.hpp)
target_include_directories(nomad-cli-options INTERFACE ${NOMAD_INCLUDE_DIR})
target_link_libraries(nomad-cli-options INTERFACE Boost::program_options)

option(NOMAD_BUILD_CLI "Build the Nomad project manager" ON)
if(NOMAD_BUILD_CLI)
    add_executable(nomad-cli apps/manager/main.cpp)
    set_target_properties(nomad-cli PROPERTIES OUTPUT_NAME nomad)

    set(NOMAD_PROJECT_TEMPLATE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/templates")
    file(
        GLOB_RECURSE NOMAD_PROJECT_TEMPLATE_FILES
        CONFIGURE_DEPENDS
        "${NOMAD_PROJECT_TEMPLATE_DIR}/*"
        "${NOMAD_PROJECT_TEMPLATE_DIR}/.*"
    )
    set_source_files_properties(${NOMAD_PROJECT_TEMPLATE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(nomad-cli PRIVATE ${NOMAD_PROJECT_TEMPLATE_FILES})

    add_custom_target(
        nomad-cli-templates
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:nomad-cli>"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:nomad-cli>/templates"
        COMMAND
            ${CMAKE_COMMAND} -E copy_directory
            "${NOMAD_PROJECT_TEMPLATE_DIR}"
            "$<TARGET_FILE_DIR:nomad-cli>/templates"
        SOURCES ${NOMAD_PROJECT_TEMPLATE_FILES}
        VERBATIM
    )
    add_dependencies(nomad-cli nomad-cli-templates)

    target_link_libraries(
        nomad-cli
        PRIVATE
        Nomad::Project
        Boost::dll
        Boost::nowide
        Boost::process
        Nomad::CliOptions
    )

    nomad_copy_runtime_dependencies(nomad-cli)
endif()

option(NOMAD_BUILD_RUNTIME "Build the Nomad game runtime" ON)
if(NOMAD_BUILD_RUNTIME)
    add_executable(nomad-runtime apps/runtime/main.cpp)
    set_target_properties(nomad-runtime PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(nomad-runtime PRIVATE Nomad::Engine)

    nomad_copy_runtime_dependencies(nomad-runtime)
    if(APPLE)
        set_target_properties(nomad-runtime PROPERTIES BUILD_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(
            nomad-runtime
            PROPERTIES
            BUILD_RPATH "$ORIGIN"
            BUILD_RPATH_USE_ORIGIN TRUE
        )
    endif()

    if(TARGET nomad-cli)
        nomad_add_runtime_bundle(nomad-runtime nomad-runtime-bundle)
        add_dependencies(nomad-cli nomad-runtime-bundle)
    endif()
endif()

option(NOMAD_BUILD_COMPILER "Build the Nomad compiler tooling" ON)
if(NOMAD_BUILD_COMPILER)
    add_executable(nomadc apps/compiler/main.cpp)
    target_link_libraries(nomadc PRIVATE Nomad::Engine Nomad::Project Nomad::CliOptions Boost::nowide)
endif()

include(cmake/NomadDistribution.cmake)

option(NOMAD_BUILD_TESTS "Build Nomad unit tests" ON)
if(NOMAD_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
