set(DEAR_IMGUI_SOURCE
    ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${dearimgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp

    ${dearimgui_SOURCE_DIR}/imgui.cpp
    ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
    ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
    ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
)

add_library(nomad-game SHARED
    ${DEAR_IMGUI_SOURCE}

    ${NOMAD_SOURCE_DIR}/nomad/game/ActionManager.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/Alignment.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/Canvas.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/Color.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/Entity.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/EntityVariableContext.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/EventRegistration.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/Game.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/Game.persistence.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GameConfiguration.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GameEvent.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GameEventQueue.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GameExecutionContext.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GameModule.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GamePackaging.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/GameResourceCheck.cpp
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

    ${NOMAD_SOURCE_DIR}/nomad/game/debug/DebugConsole.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/debug/DebugDraw.cpp

    ${NOMAD_SOURCE_DIR}/nomad/game/functions/AudioFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/GameFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/InputFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/OtherEntityFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/SceneFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/SystemFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/ThisEntityFunctions.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/functions/WindowFunctions.cpp

    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Animation.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Audio.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/AudioDiscovery.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/AudioObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Font.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/FontObject.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/RectanglePacker.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Resource.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/ResourceManager.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Sprite.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/SpriteCatalog.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/SpriteDiscovery.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/SpritePacker.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Text.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/resource/Texture.cpp

    ${NOMAD_SOURCE_DIR}/nomad/game/variables/GameDynamicVariables.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/variables/InputDynamicVariables.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/variables/OtherDynamicVariables.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/variables/SceneDynamicVariables.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/variables/ThisDynamicVariables.cpp
    ${NOMAD_SOURCE_DIR}/nomad/game/variables/WindowDynamicVariables.cpp
)
nomad_configure_library_target(nomad-game)
target_link_libraries(nomad-game PUBLIC
    Nomad::Engine
    nomad-system
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_mixer::SDL3_mixer
    SDL3_ttf::SDL3_ttf
    box2d
)
target_include_directories(nomad-game PRIVATE ${dearimgui_SOURCE_DIR})

if(MSVC)
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "/W0;/WX-")
else()
    set_source_files_properties(${DEAR_IMGUI_SOURCE} PROPERTIES COMPILE_OPTIONS "-w;-Wno-error")
    # Boost.JSON templates instantiate conversion warnings outside Nomad source.
    set_source_files_properties(
        ${NOMAD_SOURCE_DIR}/nomad/game/ActionManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/Game.persistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/InputManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/TileMapData.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/VariablePersistence.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/resource/ResourceManager.cpp
        ${NOMAD_SOURCE_DIR}/nomad/game/resource/SpriteCatalog.cpp
        PROPERTIES COMPILE_OPTIONS "-Wno-error=conversion"
    )
endif()
