add_library(nomad-language SHARED
    ${NOMAD_SOURCE_DIR}/nomad/Nomad.cpp

    ${NOMAD_SOURCE_DIR}/nomad/compiler/Argument.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/CallNode.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/Compiler.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/CompilerContext.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/CompilerTools.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/Expression.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/FormatStringCompiler.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/LoopStatement.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/Operators.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/Parser.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/SourceLoader.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/StatementParsers.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/SyntaxTree.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp

    ${NOMAD_SOURCE_DIR}/nomad/compiler/statements/EventStatement.cpp
    ${NOMAD_SOURCE_DIR}/nomad/compiler/statements/ReturnStatement.cpp

    ${NOMAD_SOURCE_DIR}/nomad/log/ConsoleSink.cpp
    ${NOMAD_SOURCE_DIR}/nomad/log/Logger.cpp
    ${NOMAD_SOURCE_DIR}/nomad/log/MemorySink.cpp

    ${NOMAD_SOURCE_DIR}/nomad/script/Callable.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Closure.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/DynamicVariable.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Event.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/FormatString.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Hub.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Instructions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Interpreter.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/InterpreterContext.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/IteratorObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/NativeCallable.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/NativeMembers.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/NomadObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/NomadStringObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/OpCode.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/RangeIterator.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/RangeObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/RangeType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/RegisterRangeType.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Runtime.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeOpCodes.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/RuntimeValue.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Type.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/Variable.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/VariableContext.cpp
    ${NOMAD_SOURCE_DIR}/nomad/script/VirtualMachine.cpp

    ${NOMAD_SOURCE_DIR}/nomad/system/Memory.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/ModuleHost.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/ModuleHostImpl.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/ModuleManifest.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/NomadConfiguration.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/Path.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/String.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/TempHeap.cpp
)
add_library(Nomad::Language ALIAS nomad-language)
nomad_configure_library_target(nomad-language)
target_compile_definitions(nomad-language PRIVATE NOMAD_LANGUAGE_BUILD)

target_include_directories(nomad-language
    PRIVATE ${NOMAD_SOURCE_DIR}
    PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(nomad-language
    PUBLIC
    Boost::exception
    Boost::dll
    Boost::json
    Boost::locale
    Boost::spirit
    PRIVATE
    tomlplusplus::tomlplusplus
    ${CMAKE_DL_LIBS}
)

if(MSVC)
    # Boost.Spirit templates instantiate warnings outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp PROPERTIES COMPILE_OPTIONS "/wd4459")
    # Boost.DLL's Windows path lookup shadows a local variable in a third-party header.
    set_source_files_properties(
        ${NOMAD_SOURCE_DIR}/nomad/system/ModuleHost.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/ModuleHostImpl.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4456"
    )
else()
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion;-Wno-error=float-conversion")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_property(SOURCE ${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp
            APPEND PROPERTY COMPILE_OPTIONS "-Wno-error=maybe-uninitialized")
    endif()

    # Boost.JSON templates instantiate conversion warnings outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion")
endif()

    add_library(nomad SHARED
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Circle.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/CircleF.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Intersection.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Point.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/PointF.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/Rectangle.cpp
        ${NOMAD_SOURCE_DIR}/nomad/geometry/RectangleF.cpp

        ${NOMAD_SOURCE_DIR}/nomad/lsp/LanguageServer.cpp

        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectConfiguration.cpp
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectInitializer.cpp
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp

        ${NOMAD_SOURCE_DIR}/nomad/system/ProjectSession.cpp
    )
    add_library(Nomad::Engine ALIAS nomad)
    nomad_configure_library_target(nomad)
    target_compile_definitions(nomad PRIVATE NOMAD_PACKAGE_PLATFORM="${NOMAD_PACKAGE_PLATFORM}")

    target_include_directories(nomad PUBLIC
        $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
        ${dearimgui_SOURCE_DIR}
        $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
    )
    target_link_libraries(nomad
        PUBLIC Nomad::Language
        PRIVATE archive tomlplusplus::tomlplusplus Boost::json Boost::program_options
    )

    if(MSVC)
        # Boost.DLL's Windows path lookup shadows a local variable in a third-party header.
        set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/system/ProjectSession.cpp PROPERTIES COMPILE_OPTIONS "/wd4456")
    else()
        # Boost.JSON templates instantiate conversion warnings outside Nomad source.
        set_source_files_properties(
            ${NOMAD_SOURCE_DIR}/nomad/lsp/LanguageServer.cpp
            ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp
            PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion"
        )
    endif()
