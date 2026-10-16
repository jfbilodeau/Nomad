include(${CMAKE_CURRENT_LIST_DIR}/NomadTarget.cmake)

# Build the reusable language and VM without engine dependencies.
add_library(nomad-language SHARED ${NOMAD_LANGUAGE_SOURCES})
add_library(Nomad::Language ALIAS nomad-language)
nomad_configure_runtime_target(nomad-language)

# Project tooling does not link the game module.
set(NOMAD_TOOLING_SOURCES ${NOMAD_ENGINE_SOURCES})
list(FILTER NOMAD_TOOLING_SOURCES INCLUDE REGEX "/nomad/(project|lsp|system|geometry)/")
set(NOMAD_GAME_SOURCES ${NOMAD_ENGINE_SOURCES})
list(REMOVE_ITEM NOMAD_GAME_SOURCES ${NOMAD_TOOLING_SOURCES})
add_library(nomad SHARED ${NOMAD_TOOLING_SOURCES})
add_library(Nomad::Engine ALIAS nomad)
nomad_configure_runtime_target(nomad)
add_library(nomad-system SHARED
    ${NOMAD_SOURCE_DIR}/nomad/script/NativeFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/system/SystemModule.cpp
)
add_library(nomad-game SHARED ${NOMAD_GAME_SOURCES})
nomad_configure_runtime_target(nomad-system)
nomad_configure_runtime_target(nomad-game)
set_target_properties(nomad-language nomad nomad-system nomad-game PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(WIN32)
    set_target_properties(nomad-language nomad nomad-system nomad-game PROPERTIES PREFIX "")
endif()
target_compile_definitions(nomad-language PRIVATE NOMAD_LANGUAGE_BUILD)

if(MSVC)
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "/W0;/WX-")
    # Boost.Spirit templates instantiated by Tokenizer.cpp trigger C4459 outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp PROPERTIES COMPILE_OPTIONS "/wd4459")
    # Boost.DLL's Windows path lookup shadows a local variable in a third-party header.
    set_source_files_properties(
        ${NOMAD_SOURCE_DIR}/nomad/system/ModuleHost.cpp
        ${NOMAD_SOURCE_DIR}/nomad/system/ProjectSession.cpp
        PROPERTIES COMPILE_OPTIONS "/wd4456"
    )
else()
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "-w;-Wno-error")
    # Boost.Spirit templates instantiated by Tokenizer.cpp trigger conversion warnings outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion;-Wno-error=float-conversion")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set_property(
            SOURCE ${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp
            APPEND PROPERTY COMPILE_OPTIONS "-Wno-error=maybe-uninitialized"
        )
    endif()
    # Boost.JSON templates instantiated by these sources trigger conversion warnings outside Nomad source.
    set_source_files_properties(
        ${NOMAD_SOURCE_DIR}/nomad/game/ActionManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Game.persistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/InputManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMapData.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/VariablePersistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/lsp/LanguageServer.cpp
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/ResourceManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteCatalog.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion"
    )
endif()

target_include_directories(
    nomad-language
    PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_include_directories(
    nomad
    PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
    ${dearimgui_SOURCE_DIR}  # DearImGui is not configured by CMake. Need to manually add include dir.
    $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(
    nomad-language
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

target_link_libraries(
    nomad
    PUBLIC
    Nomad::Language
    PRIVATE
    archive
    tomlplusplus::tomlplusplus
    Boost::json
    Boost::program_options
)

target_link_libraries(nomad-system PUBLIC Nomad::Language)
target_link_libraries(nomad-game
    PUBLIC
    Nomad::Engine
    nomad-system
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    box2d
)
target_include_directories(nomad-game PRIVATE ${dearimgui_SOURCE_DIR})
foreach(TARGET IN ITEMS nomad-language nomad nomad-system nomad-game)
    if(APPLE)
        set_target_properties(${TARGET} PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${TARGET} PROPERTIES BUILD_RPATH "$ORIGIN" BUILD_RPATH_USE_ORIGIN TRUE INSTALL_RPATH "$ORIGIN")
    endif()
endforeach()
