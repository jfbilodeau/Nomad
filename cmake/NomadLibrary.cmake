# Build nomad as a library target
add_library(nomad STATIC ${NOMAD_SOURCES} ${NOMAD_INCLUDE})
add_library(Nomad::Engine ALIAS nomad)

if(NOMAD_ENABLE_SANITIZERS)
    target_link_libraries(nomad PRIVATE nomad_sanitizers)
endif()

if(MSVC)
    target_compile_options(nomad PRIVATE /W4 /WX)
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "/W0;/WX-")
    # Boost.Spirit templates instantiated by Tokenizer.cpp trigger C4459 outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp PROPERTIES COMPILE_OPTIONS "/wd4459")
else()
    target_compile_options(nomad PRIVATE -Wall -Wextra -Wconversion -Werror)
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "-w;-Wno-error")
    # Boost.Spirit templates instantiated by Tokenizer.cpp trigger conversion warnings outside Nomad source.
    set_source_files_properties(${NOMAD_SOURCE_DIR}/nomad/compiler/Tokenizer.cpp PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion;-Wno-error=float-conversion")
    # Boost.JSON templates instantiated by these sources trigger conversion warnings outside Nomad source.
    set_source_files_properties(
        ${NOMAD_SOURCE_DIR}/nomad/game/ActionManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Game.persistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/InputManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMapData.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/VariablePersistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/ResourceManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/resource/SpriteAtlas.cpp
        ${NOMAD_SOURCE_DIR}/nomad/script/Documentation.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion"
    )
endif()

target_include_directories(
    nomad
    PUBLIC
    $<BUILD_INTERFACE:${NOMAD_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
    ${dearimgui_SOURCE_DIR}  # DearImGui is not configured by CMake. Need to manually add include dir.
)

target_compile_definitions(
    nomad
    PRIVATE
    _LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION
    $<$<CONFIG:Debug>:NOMAD_DEBUG>
)

target_link_libraries(
    nomad
    PUBLIC
    Boost::exception
    Boost::json
    Boost::program_options
    Boost::spirit
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
    box2d
)
