include(${CMAKE_CURRENT_LIST_DIR}/NomadTarget.cmake)

# Build the reusable language and VM without engine dependencies.
add_library(nomad-language STATIC ${NOMAD_LANGUAGE_SOURCES})
add_library(Nomad::Language ALIAS nomad-language)
nomad_configure_runtime_target(nomad-language)

# Build CPU-only sprite discovery, packing, and catalog support for tools and the engine.
add_library(nomad-assets STATIC ${NOMAD_ASSET_SOURCES})
add_library(Nomad::Assets ALIAS nomad-assets)
nomad_configure_target(nomad-assets)

# Build project configuration support independently from the language and engine.
add_library(nomad-project STATIC ${NOMAD_PROJECT_SOURCES})
add_library(Nomad::Project ALIAS nomad-project)
nomad_configure_target(nomad-project)

# Build the game engine on top of the language runtime.
add_library(nomad STATIC ${NOMAD_ENGINE_SOURCES})
add_library(Nomad::Engine ALIAS nomad)
nomad_configure_runtime_target(nomad)

if(MSVC)
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "/W0;/WX-")
    # Boost.Spirit templates instantiated by Tokenizer.cpp trigger C4459 outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp PROPERTIES COMPILE_OPTIONS "/wd4459")
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
        ${NOMAD_SOURCE_DIR}/nomad/project/ProjectPackager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/ResourceManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteCatalog.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteAtlas.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion"
    )
endif()

target_include_directories(
    nomad-assets
    PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

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
    $<INSTALL_INTERFACE:include>
    ${dearimgui_SOURCE_DIR}  # DearImGui is not configured by CMake. Need to manually add include dir.
)

target_include_directories(
    nomad-project
    PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${NOMAD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(
    nomad-language
    PUBLIC
    Boost::exception
    Boost::json
    Boost::locale
    Boost::spirit
)

target_link_libraries(nomad-project PRIVATE tomlplusplus::tomlplusplus archive_static Boost::json)

target_link_libraries(
    nomad-assets
    PUBLIC
    Nomad::Language
    SDL3::SDL3
    SDL3_image::SDL3_image
    PRIVATE
    Boost::json
)

target_link_libraries(nomad-project PRIVATE Nomad::Assets)

target_link_libraries(
    nomad
    PUBLIC
    Nomad::Assets
    Nomad::Language
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    box2d
    PRIVATE
    Nomad::Project
    Boost::program_options
)
