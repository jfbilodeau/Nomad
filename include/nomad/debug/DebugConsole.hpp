// Copyright (c) 2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>

#include <nomad/game/Game.hpp>

#include <nomad/log/MemorySink.hpp>
#include <nomad/script/Interpreter.hpp>

#include <SDL3/SDL.h>

#include <array>
#include <deque>
#include <memory>
#include <vector>

struct ImGuiStyle;
struct ImVec2;

namespace nomad {

class Entity;
class Game;
class Scene;
class Function;
class Type;
class VirtualMachine;

struct FrameProfile {
    NomadInteger frameNumber = 0;
    NomadInteger frameDuration = 0;
    NomadInteger updateDuration = 0;
    NomadInteger renderDuration = 0;
    NomadInteger inputDuration = 0;
    NomadInteger totalDuration = 0;
    NomadInteger tempHeapSize = 0;
    NomadInteger maxHeapSize = 0;
};

class DebugConsole {
public:
    DebugConsole(Game* game, SDL_Window* window, SDL_Renderer* renderer);
    DebugConsole(const DebugConsole&) = delete;
    DebugConsole& operator=(const DebugConsole&) = delete;
    ~DebugConsole();

    void activate();
    void deactivate();
    void toggle();

    // Applied on the next rendered frame. Clamped to the supported range.
    void setUiScale(float scale);

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool wantsKeyboard() const;
    [[nodiscard]] bool wantsMouse() const;

    void processEvent(const SDL_Event& event) const;
    [[nodiscard]] bool beginGameRender();
    void endGameRender() const;
    void render();

    void pushFrameProfile(const FrameProfile& frameProfile);
    [[nodiscard]] bool consumeStepRequest();

private:
    struct ConsoleEntry {
        bool error;
        NomadString text;
    };

    // What the "Scenes and Entities" tree currently points at. Drives which
    // variables the inspector lists.
    enum class SelectionKind {
        Game,
        Scene,
        Entity,
    };

    struct VariableRow {
        enum class Source {
            Context,
            Dynamic,
            Console,
        };

        NomadString name;
        const Type* type = nullptr;
        NomadString value;
        Source source = Source::Context;
        NomadId contextId = NOMAD_INVALID_ID;
        NomadId variableId = NOMAD_INVALID_ID;
        bool editable = false;
    };

    void ensureGameTexture();
    void applyUiScale(float scale);
    [[nodiscard]] float getToolbarHeight() const;
    void renderToolbar();
    void renderGameView() const;
    void renderResourceCharts(
        const ImVec2& contentTopLeft,
        const ImVec2& available,
        const ImVec2& imageTopLeft,
        const ImVec2& imageSize
    ) const;
    void renderEntityInspector();
    void renderVariableInspector();
    void renderFunctionInspector();
    void renderConsole();
    void renderConsoleSplitter();
    void renderLogs();

    void executeNativeFunction();
    void collectVariableRows(std::vector<VariableRow>& rows) const;
    void collectContextVariableRows(std::vector<VariableRow>& rows) const;
    void collectDynamicVariableRows(std::vector<VariableRow>& rows) const;
    void collectConsoleVariableRows(std::vector<VariableRow>& rows) const;
    [[nodiscard]] bool applyVariableEdit(const VariableRow& row, const NomadString& text);
    [[nodiscard]] Scene* getSelectedScene() const;
    [[nodiscard]] Entity* getSelectedEntity() const;
    void selectGame();
    void selectScene(Scene* scene);
    void selectEntity(Entity* entity);

    Game* m_game;
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    SDL_Texture* m_gameTexture = nullptr;
    MemorySink m_memorySink;
    Interpreter m_interpreter;
    std::unique_ptr<VirtualMachine> m_inspectionVirtualMachine;
    std::unique_ptr<ImGuiStyle> m_baseStyle;

    bool m_active = false;
    bool m_stepRequested = false;
    bool m_wasFullscreen = false;
    float m_uiScale = 1.0f;
    float m_requestedUiScale = DEBUG_CONSOLE_DEFAULT_SCALE;
    float m_consoleSplitFraction = 0.60f;
    int m_savedWindowX = 0;
    int m_savedWindowY = 0;
    int m_savedWindowWidth = 0;
    int m_savedWindowHeight = 0;
    NomadInteger m_textureWidth = 0;
    NomadInteger m_textureHeight = 0;

    NomadId m_selectedSceneId = NOMAD_INVALID_ID;
    NomadId m_selectedEntityId = NOMAD_INVALID_ID;
    NomadId m_selectedFunctionId = NOMAD_INVALID_ID;
    SelectionKind m_selectionKind = SelectionKind::Game;

    NomadString m_editingVariableName;
    bool m_editFocusRequested = false;

    std::array<char, 1024> m_nativeFunctionBuffer{};
    std::array<char, 256> m_editBuffer{};
    std::array<char, 128> m_variableFilter{};
    std::array<char, 128> m_entityFilter{};
    std::array<char, 128> m_functionFilter{};
    std::vector<NomadString> m_nativeFunctionHistory;
    std::vector<ConsoleEntry> m_consoleEntries;
    std::deque<FrameProfile> m_frameProfiles;
};

} // namespace nomad
