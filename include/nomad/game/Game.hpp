// Copyright (c) 2023-2026 Jean-François Bilodeau (@jfbilodeau).

#pragma once

#include <nomad/Nomad.hpp>
#include <nomad/log/Logger.hpp>

#include <nomad/geometry/Point.hpp>
#include <nomad/geometry/PointF.hpp>

#include <nomad/game/Color.hpp>
#include <nomad/game/GameEvent.hpp>
#include <nomad/game/GameEventQueue.hpp>
#include <nomad/game/GameExecutionContext.hpp>
#include <nomad/game/InputManager.hpp>

#include <nomad/script/RuntimeValue.hpp>
#include <nomad/script/Variable.hpp>

#include <SDL3/SDL.h>

#include <array>
#include <functional>

namespace nomad {

// Forward declarations
class DebugConsole;
class Canvas;
class ThisEntityVariableContext;
class InputManager;
class Runtime;
class Resource;
class ResourceManager;
class Scene;
class Function;
class SimpleVariableContext;
class Closure;

class GameException final : public NomadException {
public:
    explicit GameException(const NomadString& message) : NomadException(message) {}
};

struct GameOptions {
    NomadString resourcePath;
    bool debug = false;
};

constexpr NomadFloat DEBUG_CONSOLE_MINIMUM_SCALE = 0.8f;
constexpr NomadFloat DEBUG_CONSOLE_MAXIMUM_SCALE = 2.0f;
constexpr NomadFloat DEBUG_CONSOLE_DEFAULT_SCALE = 1.25f;

const NomadString DEBUG_CONSOLE_VISIBLE_VARIABLE = "game.debug.console.visible";
const NomadString DEBUG_CONSOLE_SCALE_VARIABLE = "game.debug.console.scale";

class Game {
public:
    explicit Game(const GameOptions* options);
    Game(const Game&) = delete;
    ~Game();

    void initialize();

    [[nodiscard]] Runtime* getRuntime() const;
    [[nodiscard]] Canvas* getCanvas() const;
    [[nodiscard]] ResourceManager* getResources() const;

    void setDebug(bool debug);
    [[nodiscard]] bool isDebug() const;

    [[nodiscard]] const VariableMap* getSceneVariables() const;
    [[nodiscard]] const VariableMap* getEntityVariables() const;

    void setWindowSize(NomadInteger width, NomadInteger height);
    [[nodiscard]] const Point& getWindowSize() const;

    void centerWindow() const;

    void setResolution(NomadInteger width, NomadInteger height);
    [[nodiscard]] const Point& getResolution() const;

    void setFps(NomadIndex fps);
    [[nodiscard]]
    NomadIndex getFps() const;

    [[nodiscard]] const NomadString& getOrganization() const;
    void setOrganization(const NomadString& organization);

    [[nodiscard]] const NomadString& getName() const;
    void setName(const NomadString& name);

    [[nodiscard]] NomadString getResourcePath() const;
    [[nodiscard]] NomadString getStatePath() const;
    [[nodiscard]] NomadString getSavePath() const;
    [[nodiscard]] NomadString getSettingsPath() const;

    [[nodiscard]] NomadString makeResourcePath(const NomadString& resourceName) const;
    [[nodiscard]] NomadString makeStatePath(const NomadString& stateName) const;
    [[nodiscard]] NomadString makeSavePath(const NomadString& saveName) const;
    [[nodiscard]] NomadString makeSettingsPath(const NomadString& settingsName) const;

    [[nodiscard]] bool fileExists(const NomadString& fileName) const;
    void deleteFile(const NomadString& fileName) const;

    // Game state (`inventory.*`) and settings (`settings.*`) persistence.
    bool saveGame(const NomadString& saveName);
    bool loadGame(const NomadString& saveName);
    [[nodiscard]] bool saveExists(const NomadString& saveName) const;
    bool saveSettings();
    bool loadSettings();

    // Debug settings. Changes are saved to `debug.json` automatically.
    void setDebugConsoleVisible(bool visible);
    [[nodiscard]] bool isDebugConsoleVisible() const;
    void setDebugConsoleScale(NomadFloat scale);
    [[nodiscard]] NomadFloat getDebugConsoleScale() const;

    void run();
    void quit();

    [[nodiscard]] uint64_t getUpdateDuration() const;
    [[nodiscard]] uint64_t getRenderDuration() const;

    [[nodiscard]] NomadId getFunctionId(const NomadString& functionName) const;
    void executeFunction(NomadId functionId, Scene* scene, Entity* thisEntity, Entity* otherEntity = nullptr);
    void executeFunction(NomadId functionId, Scene* scene, Entity* entity, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue);
    void executeFunction(NomadId functionId, Scene* scene, Entity* entity, const std::vector<Entity*>& others, const std::vector<RuntimeValue>& args);
    void executeFunction(NomadId functionId, Scene* scene, Entity* entity, const std::vector<Entity*>& others, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue);
    void executeFunction(const Closure* closure, Scene* scene, Entity* entity, const std::vector<Entity*>& others = {}, const std::vector<RuntimeValue>& args = {});
    // The caller owns the copy placed in `returnValue` and must free it through the function's return type.
    void executeFunction(const Closure* closure, Scene* scene, Entity* entity, const std::vector<Entity*>& others, const std::vector<RuntimeValue>& args, RuntimeValue& returnValue);

    [[nodiscard]] bool executePredicate(NomadId functionId) const;

    NomadId createScene(
        const NomadString& sceneName,
        NomadId functionId,
        std::unique_ptr<Closure> postCreateClosure = nullptr
    );
    void removeScene(const Scene* scene);
    [[nodiscard]] Scene* getSceneById(NomadId id) const;
    [[nodiscard]] Scene* getSceneByName(const NomadString& name) const;
    void forEachScene(const std::function<void(Scene*)>& callback) const;

    NomadId addEntityToScene(
        NomadId sceneId,
        NomadId initFunctionId,
        NomadFloat x,
        NomadFloat y,
        NomadFloat width,
        NomadFloat height,
        NomadInteger layer,
        const NomadString& text,
        std::unique_ptr<Closure> postCreateClosure = nullptr
    );
    void removeEntityFromScene(NomadId sceneId, NomadId entityId);

    void dispatchEvent(const EventDispatch& dispatch);
    void triggerEvent(const NomadString& eventName);
    void triggerSceneEvent(NomadId sceneId, const NomadString& eventName);
    void triggerSceneLayerEvent(NomadId sceneId, NomadInteger layerNumber, const NomadString& eventName);
    void triggerEntityEvent(NomadId sceneId, NomadId entityId, const NomadString& eventName, std::vector<RuntimeValue> args = {});
    void triggerEntityEvent(NomadId eventId, NomadId sceneId, NomadId entityId, std::vector<RuntimeValue> args);
    void scheduleEntityEvent(NomadId sceneId, NomadId entityId, NomadInteger frameCount, NomadBoolean repeat, const NomadString& eventName);
    void scheduleEntityCallback(NomadId sceneId, NomadId entityId, NomadInteger frameCount, NomadBoolean repeat, NomadId callback);

    [[nodiscard]] bool running() const;
    [[nodiscard]] bool isPaused() const;

    void pause();
    void resume();

    [[nodiscard]] NomadInteger getTicks() const;

    void setClearColor(const Color& color);
    [[nodiscard]] const Color& getClearColor() const;

    [[nodiscard]] GameExecutionContext* getCurrentContext();
    [[nodiscard]] const GameExecutionContext* getCurrentContext() const;

    void setLanguage(const NomadString& languageCode);
    [[nodiscard]] const NomadString& getLanguage() const;
    NomadString& getText(const NomadString& key, NomadString& text) const;

    [[noreturn]] void raiseError(const NomadString& message);

private:
    void initSdl();
    void initSdlTtf();
    void initResourcePath();
    void initText() const;
    void initRuntime();
    void initWindowCallbacks();

    void initFunctions();
    void initGameFunctions();
    void initInputFunctions();
    void initOtherEntityFunctions();
    void initSceneFunctions();
    void initSystemFunctions();
    void initThisEntityFunctions();
    void initWindowFunctions();
    void initVariableContext();
    void initResourceManager();

    // Init dynamic variables
    void initDynamicVariables();
    void initGameDynamicVariables();
    void initInputDynamicVariables();
    void initOtherDynamicVariables();
    void initSceneDynamicVariables() const;
    void initThisDynamicVariables();
    void initWindowDynamicVariables();

    void initDebugConsole();

    [[nodiscard]] NomadString getSettingsFilePath() const;
    [[nodiscard]] NomadString getDebugSettingsFilePath() const;
    void ensureDebugSettingsLoaded();
    void loadDebugSettings();
    void saveDebugSettings() const;
    void applyDebugConsoleVisible(bool visible);
    void applyDebugConsoleScale(NomadFloat scale);

    void createPathToFile(const NomadString& fileName) const;
    void loadTextIntoConst(const NomadString& languageCode) const;

    void compileFunctions();

    void runInitFunction();

    void render(Canvas* canvas) const;
    void update();

    void processGameEvents();
    [[nodiscard]]
    bool processGameEventCreateScene(const GameEventCreateScene& event);
    [[nodiscard]]
    bool processGameEventRemoveScene(const GameEventRemoveScene& event);
    [[nodiscard]]
    bool processGameEventCreateEntity(const GameEventCreateEntity& event);
    [[nodiscard]]
    bool processGameDispatchEvent(const GameEventDispatch& event) const;
    [[nodiscard]]
    bool processGameEventTriggerEvent(const GameEventTriggerEvent& event) const;
    [[nodiscard]]
    bool processGameEventRemoveEntity(const GameEventRemoveEntity& event) const;
    [[nodiscard]]
    bool processGameEventTriggerSceneEvent(const GameEventTriggerSceneEvent& event) const;
    [[nodiscard]]
    bool processGameEventTriggerSceneLayerEvent(const GameEventTriggerSceneLayerEvent& event) const;
    [[nodiscard]]
    bool processGameEventTriggerEntityEvent(const GameEventTriggerEntityEvent& event) const;
    [[nodiscard]]
    bool processGameEventTriggerEntityCallback(const GameEventTriggerEntityCallback& event);

    void processInput(SDL_KeyboardEvent& event);
    [[nodiscard]] bool processSystemInput(const SDL_KeyboardEvent& event);
    void processInput(SDL_MouseButtonEvent& event);
    void processInput(SDL_GamepadButtonEvent& event) const;
    void dispatchInputEvent(const InputEvent& event) const;
    void executeWindowCallback(const std::shared_ptr<Closure>& callback, const std::vector<RuntimeValue>& args = {});

    [[nodiscard]] NomadId getNextSceneId();
    // The current scene being processed
    [[nodiscard]] Scene* getCurrentScene() const;

    NomadString m_title = "Nomad";
    Point m_resolution = {800, 600};
    Point m_windowSize = m_resolution;
    NomadIndex m_fps = 30;

    const VariableMap* m_sceneVariableMap = nullptr;
    const VariableMap* m_thisEntityVariableMap = nullptr;
    const VariableMap* m_otherEntityVariableMap = nullptr;
    const VariableMap* m_inventoryVariableMap = nullptr;

    // Owned by the runtime.
    SimpleVariableContext* m_inventoryContext = nullptr;
    SimpleVariableContext* m_settingsContext = nullptr;

    bool m_debugSettingsLoaded = false;
    bool m_debugConsoleVisible = false;
    NomadFloat m_debugConsoleScale = DEBUG_CONSOLE_DEFAULT_SCALE;

    PointF m_mousePosition;
    struct MouseButtonState {
        bool pressed = false;
        bool held = false;
        bool released = false;
    };
    std::array<MouseButtonState, 5> m_mouseButtonState = {
        MouseButtonState{},
        MouseButtonState{},
        MouseButtonState{},
    };

    PointF m_mouseLastPosition;
    std::unique_ptr<ResourceManager> m_resourceManager;

    NomadId m_sceneIdCounter = 0;
    std::vector<std::unique_ptr<Scene>> m_scenes;

    NomadString m_organization;
    NomadString m_name;

    GameOptions m_options;
    bool m_running = false;
    bool m_paused = false;

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    std::unique_ptr<Canvas> m_canvas;
    Color m_clearColor = Colors::White;
    // Using shared pointers for window event closures to allows closures to be replaced in the event handler.
    std::shared_ptr<Closure> m_onWindowResize;
    std::shared_ptr<Closure> m_onWindowMove;
    std::shared_ptr<Closure> m_onWindowGainFocus;
    std::shared_ptr<Closure> m_onWindowLoseFocus;
    std::shared_ptr<Closure> m_onWindowMaximize;
    std::shared_ptr<Closure> m_onWindowMinimize;
    std::shared_ptr<Closure> m_onWindowRestore;
    std::shared_ptr<Closure> m_onWindowClose;

    // Performance
    NomadInteger m_currentFrame = 0;
    uint64_t m_renderDuration = 0;
    uint64_t m_updateDuration = 0;

    InputManager* m_inputManager = nullptr;

    std::unique_ptr<DebugConsole> m_debugConsole;

    std::unique_ptr<Runtime> m_runtime;
    GameExecutionContext m_currentContext;

    NomadString m_language = "en";

    GameEventQueue m_eventQueue;
};

} // nomad
