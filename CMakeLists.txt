cmake_minimum_required(VERSION 3.30)
set(CMAKE_POLICY_VERSION 3.30)

project(Nomad VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NOMAD_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(NOMAD_WARNINGS_AS_ERRORS "Treat Nomad compiler warnings as errors" ON)
option(NOMAD_USE_CCACHE "Use ccache when it is available" ON)

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

set(Boost_USE_STATIC_LIBS ON)

#set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
#set(DAV1D_ASM OFF CACHE BOOL "" FORCE)
#set(AOM_TARGET_CPU generic CACHE STRING "" FORCE)
set(SDL2IMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_JXL OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED OFF CACHE BOOL "" FORCE)

#set(SDL2IMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
#set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2NET_INSTALL OFF CACHE BOOL "" FORCE)

set(BOX2D_COMPILE_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)

set(FETCHCONTENT_QUIET OFF)

include(FetchContent)

set(
        BOOST_INCLUDE_LIBRARIES
        algorithm
        exception
        json
        phoenix
        program_options
        spirit
        test
        tokenizer
)

set(
        NOMAD_BOOST_GIT_SUBMODULES
        libs/algorithm
        libs/align
        libs/any
        libs/array
        libs/assert
        libs/atomic
        libs/bind
        libs/chrono
        libs/concept_check
        libs/config
        libs/container
        libs/container_hash
        libs/conversion
        libs/core
        libs/date_time
        libs/describe
        libs/detail
        libs/endian
        libs/exception
        libs/function
        libs/function_types
        libs/functional
        libs/fusion
        libs/headers
        libs/integer
        libs/intrusive
        libs/io
        libs/iterator
        libs/json
        libs/lexical_cast
        libs/move
        libs/mp11
        libs/mpl
        libs/numeric/conversion
        libs/optional
        libs/phoenix
        libs/pool
        libs/predef
        libs/preprocessor
        libs/program_options
        libs/proto
        libs/range
        libs/ratio
        libs/regex
        libs/smart_ptr
        libs/spirit
        libs/static_assert
        libs/system
        libs/test
        libs/thread
        libs/throw_exception
        libs/tokenizer
        libs/tuple
        libs/type_index
        libs/type_traits
        libs/typeof
        libs/unordered
        libs/utility
        libs/variant
        libs/variant2
        libs/winapi
        tools/cmake
)

FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG boost-1.88.0
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        GIT_SUBMODULES ${NOMAD_BOOST_GIT_SUBMODULES}
        GIT_SUBMODULES_RECURSE FALSE
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        SDL
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.22
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        SDL_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG release-3.2.4
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        GIT_SUBMODULES ""
        GIT_SUBMODULES_RECURSE FALSE
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        SDL_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-3.2.2
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        DearImGui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.9
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
)

FetchContent_Declare(
        box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d
        GIT_TAG v3.1.1
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        OVERRIDE_FIND_PACKAGE
        EXCLUDE_FROM_ALL
)

FetchContent_MakeAvailable(Boost box2d SDL SDL_image SDL_ttf DearImGui)
#FetchContent_GetProperties(DearImGui)

# find_package(Boost REQUIRED algorithm exception json phoenix program_options spirit unit_test_framework tokenizer)
#if(NOT TARGET SDL3::SDL3)
#    find_package(SDL3 REQUIRED CONFIG)
#endif()
#if(NOT TARGET SDL3_image::SDL3_image)
#    find_package(SDL3_image REQUIRED CONFIG)
#endif()
#if(NOT TARGET SDL3_ttf::SDL3_ttf)
#    find_package(SDL3_ttf REQUIRED CONFIG)
#endif()
#find_package(SDL3 REQUIRED CONFIG)
#find_package(SDL3_image REQUIRED CONFIG)
#find_package(SDL3_ttf REQUIRED CONFIG)
#find_package(DearImGui REQUIRED)
#find_package(box2d REQUIRED)

if(MSVC)
    target_compile_options(box2d PRIVATE /wd4201)
endif()

set(NOMAD_SOURCE_DIR ${CMAKE_SOURCE_DIR}/src)
set(NOMAD_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

set(
        NOMAD_INCLUDE
        ${DEAR_IMGUI_INCLUDE}

        ${NOMAD_INCLUDE_DIR}/nomad/Nomad.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Argument.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/CallNode.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Compilation.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Compiler.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/CompilerContext.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/CompilerException.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Expression.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/FormatStringCompiler.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Identifier.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Operators.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Parser.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/StatementParsers.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/SyntaxTree.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/Tokenizer.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/compiler/statements/EventStatement.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/compiler/statements/ReturnStatement.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/debug/DebugConsole.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/debug/DebugDraw.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/game/Alignment.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/ActionManager.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/BodyType.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/Canvas.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/Color.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/Entity.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/EntityVariableContext.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/EventRegistration.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/Game.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/GameEvent.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/GameExecutionContext.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/GameFactory.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/GameStatementParsers.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/InputManager.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/Layer.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/Scene.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/SceneVariableContext.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/System.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/TileMapData.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/TileMap.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/game/VariablePersistence.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/geometry/Circle.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/CircleF.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/Geometry.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/Intersection.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/Point.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/PointF.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/Rectangle.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/geometry/RectangleF.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/log/ConsoleSink.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/log/Logger.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/log/MemorySink.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/resource/Animation.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/Font.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/Resource.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/ResourceManager.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/Sprite.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/SpriteAtlas.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/Texture.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/resource/Text.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/script/Closure.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Callable.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/NativeFunctions.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Documentation.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/DynamicVariable.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Event.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/FormatString.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Instructions.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/VirtualMachine.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Interpreter.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/OpCode.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Runtime.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Function.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/RuntimeValue.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Type.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/Variable.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/script/VariableContext.hpp

        ${NOMAD_INCLUDE_DIR}/nomad/system/Boolean.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/system/TempHeap.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/system/Float.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/system/Integer.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/system/Memory.hpp
        ${NOMAD_INCLUDE_DIR}/nomad/system/String.hpp
)

set(
        DEAR_IMGUI_SOURCE
        ${dearimgui_SOURCE_DIR}/imgui.cpp
        ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
        ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
        ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

set(
        NOMAD_SOURCES
        ${DEAR_IMGUI_SOURCE}

        ${NOMAD_SOURCE_DIR}/nomad/Nomad.cpp

        ${NOMAD_SOURCE_DIR}/nomad/compiler/Argument.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CallNode.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Compilation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Compiler.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CompilerContext.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Expression.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/FormatStringCompiler.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Operators.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Parser.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/StatementParsers.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/SyntaxTree.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp

        ${NOMAD_SOURCE_DIR}/nomad/compiler/statements/EventStatement.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/statements/ReturnStatement.cpp

        ${NOMAD_SOURCE_DIR}/nomad/debug/DebugConsole.cpp
        ${NOMAD_SOURCE_DIR}/nomad/debug/DebugDraw.cpp

        ${NOMAD_SOURCE_DIR}/nomad/game/Alignment.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/ActionManager.cpp
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
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMapData.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMap.cpp
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
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Point.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Intersection.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/PointF.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Rectangle.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/RectangleF.cpp

        ${NOMAD_SOURCE_DIR}/nomad/log/ConsoleSink.cpp
        ${NOMAD_SOURCE_DIR}/nomad/log/Logger.cpp
        ${NOMAD_SOURCE_DIR}/nomad/log/MemorySink.cpp

        ${NOMAD_SOURCE_DIR}/nomad/resource/Animation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Font.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Resource.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/ResourceManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Sprite.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteAtlas.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Texture.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/Text.cpp

        ${NOMAD_SOURCE_DIR}/nomad/script/Closure.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunction.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/DynamicVariable.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Event.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/FormatString.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Instructions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/VirtualMachine.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Interpreter.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/OpCode.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Runtime.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeOpCodes.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Function.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeValue.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Type.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Variable.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/VariableContext.cpp

        ${NOMAD_SOURCE_DIR}/nomad/system/TempHeap.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/Memory.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/String.cpp
)

set(
        NOMAD_LANGUAGE_SOURCES
        ${NOMAD_SOURCE_DIR}/nomad/Nomad.cpp

        ${NOMAD_SOURCE_DIR}/nomad/compiler/Argument.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CallNode.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Compilation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/Compiler.cpp
        ${NOMAD_SOURCE_DIR}/nomad/compiler/CompilerContext.cpp
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
        ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunction.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunctions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/DynamicVariable.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Event.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/FormatString.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Instructions.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/VirtualMachine.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Interpreter.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/OpCode.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Runtime.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeOpCodes.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Function.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeValue.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Type.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Variable.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/VariableContext.cpp

        ${NOMAD_SOURCE_DIR}/nomad/system/TempHeap.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/Memory.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/String.cpp
)

set(NOMAD_LANGUAGE_INCLUDE ${NOMAD_INCLUDE})
list(
        FILTER NOMAD_LANGUAGE_INCLUDE
        INCLUDE REGEX
        "/nomad/(compiler|log|script|system)/|/nomad/Nomad\\.hpp$"
)

list(REMOVE_ITEM NOMAD_SOURCES ${NOMAD_LANGUAGE_SOURCES})

#set(NOMAD_PUBLIC_HEADERS
#    ${NOMAD_INCLUDE_DIR}/nomad/Nomad.hpp
#    ${NOMAD_INCLUDE_DIR}/nomad/debug/DebugDraw.hpp
#    # add other public headers as needed
#)

include(cmake/NomadLibrary.cmake)

option(NOMAD_BUILD_RUNTIME "Build the Nomad game runtime" ON)
if(NOMAD_BUILD_RUNTIME)
    add_executable(nomad-runtime apps/runtime/main.cpp)
    target_link_libraries(nomad-runtime PRIVATE Nomad::Engine)

    if(WIN32)
        add_custom_command(
            TARGET nomad-runtime
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:nomad-runtime> $<TARGET_FILE_DIR:nomad-runtime>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()

option(NOMAD_BUILD_COMPILER "Build the Nomad compiler tooling" ON)
if(NOMAD_BUILD_COMPILER)
    add_executable(nomadc apps/compiler/main.cpp)
    target_link_libraries(nomadc PRIVATE Nomad::Engine)
endif()

option(NOMAD_BUILD_TESTS "Build Nomad unit tests" ON)
if(NOMAD_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()
