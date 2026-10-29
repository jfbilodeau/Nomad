cmake_minimum_required(VERSION 3.30)
set(CMAKE_POLICY_VERSION 3.30)

project(Nomad VERSION 0.2.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_SHARED_LIBS ON)

# Bundled dependencies must find sibling libraries after relocation, including indirect dependencies.
if(UNIX AND NOT APPLE)
        list(APPEND CMAKE_BUILD_RPATH "$ORIGIN")
        set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)
endif()

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

# Nomad uses headers, not C++20 named modules; avoid a separate compiler scan before compilation.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

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

include(cmake/NomadTarget.cmake)
include(cmake/NomadLibrary.cmake)
set(NOMAD_MODULE_NAMES collections game log os system)
add_subdirectory(modules/nomad-collections)
add_subdirectory(modules/nomad-log)
add_subdirectory(modules/nomad-os)
add_subdirectory(modules/nomad-system)
add_subdirectory(modules/nomad-game)
include(cmake/NomadRuntime.cmake)

add_library(nomad-cli-options INTERFACE)
add_library(Nomad::CliOptions ALIAS nomad-cli-options)
target_sources(nomad-cli-options INTERFACE ${NOMAD_INCLUDE_DIR}/nomad/system/ProgramOptions.hpp)
target_include_directories(nomad-cli-options INTERFACE ${NOMAD_INCLUDE_DIR})
target_link_libraries(nomad-cli-options INTERFACE Boost::program_options)

option(NOMAD_BUILD_CLI "Build the Nomad project manager" ON)
if(NOMAD_BUILD_CLI)
    add_executable(nomad-cli
        apps/manager/GenerateBindingsCommand.cpp
        apps/manager/GenerateBindingsCommand.hpp
        apps/manager/main.cpp
    )
    target_include_directories(nomad-cli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/apps/manager")
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
        Nomad::Engine
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
    target_link_libraries(nomad-runtime PRIVATE Nomad::Engine Boost::nowide)

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
    target_link_libraries(nomadc PRIVATE Nomad::Engine Nomad::CliOptions Boost::nowide)
endif()

option(NOMAD_BUILD_LSP "Build the Nomad language server" ON)
if(NOMAD_BUILD_LSP)
    add_executable(nomad-lsp apps/lsp/main.cpp)
    target_link_libraries(nomad-lsp PRIVATE Nomad::Engine Boost::program_options Boost::nowide)
    nomad_configure_runtime_target(nomad-lsp)
    nomad_copy_runtime_dependencies(nomad-lsp)
endif()

include(cmake/NomadDistribution.cmake)

option(NOMAD_BUILD_TESTS "Build Nomad unit tests" ON)
if(NOMAD_BUILD_TESTS AND BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(TARGET nomad-runtime)
    include(cmake/NomadModules.cmake)
endif()

add_custom_target(nomad-development)
foreach(program IN ITEMS nomad-cli nomad-lsp nomad-runtime nomadc nomad_test)
    if(TARGET ${program})
        add_dependencies(nomad-development ${program})
    endif()
endforeach()
